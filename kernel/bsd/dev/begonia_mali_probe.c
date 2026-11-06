/* SPDX-License-Identifier: MIT
 * MT6785/G76 opt-in power/reset/identity experiment. No job submission.
 * Register facts: pinned Xiaomi clk-mt6785[-pg].c and Mali job-manager layout.
 * Never entered by normal UI builds. Requires measured, enabled MT6359 rails.
 */
#include <kern/clock.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
extern vm_offset_t ml_io_map(vm_offset_t,vm_size_t);
static uint32_t rd(vm_offset_t b,unsigned o) { return *(volatile uint32_t *)(b+o); }
static void wr(vm_offset_t b,unsigned o,uint32_t v) {
 *(volatile uint32_t *)(b+o)=v;__asm__ volatile("dsb sy":::"memory");
}
static int wait_bits(vm_offset_t b,unsigned o,uint32_t mask,uint32_t value) {
 for(unsigned i=0;i<5000;i++) {if((rd(b,o)&mask)==value)return 0;delay(10);}
 printf("BEGONIA GPU: timeout offset %x mask %x value %x got %x\n",o,mask,value,rd(b,o));return -1;
}
static int domain_on(vm_offset_t spm,unsigned control,uint32_t status,unsigned srams) {
 wr(spm,control,rd(spm,control)|4);
 wr(spm,control,rd(spm,control)|8);
 if(wait_bits(spm,0x160,status,status) || wait_bits(spm,0x164,status,status))return -1;
 wr(spm,control,rd(spm,control)&~16u);
 wr(spm,control,rd(spm,control)&~2u);
 wr(spm,control,rd(spm,control)|1);
 for(unsigned i=0;i<srams;i++) {
  wr(spm,control,rd(spm,control)&~(1u<<(8+i)));
  if(wait_bits(spm,control,1u<<(12+i),0))return -1;
 }
 return 0;
}
void begonia_mali_probe(uint32_t gpu_uv,uint32_t sram_uv,int rails_enabled) {
 uint32_t enabled=0;
 if(!PE_parse_boot_argn("begonia_gpu_probe",&enabled,sizeof(enabled)) || enabled!=1)return;
 if(!rails_enabled || gpu_uv<575000 || gpu_uv>850000 || sram_uv<850000 ||
    sram_uv>1000000 || sram_uv<gpu_uv+100000 || sram_uv>gpu_uv+300000) {
  printf("BEGONIA GPU: rail precondition failed (%u/%u enabled%d); untouched\n",gpu_uv,sram_uv,rails_enabled);return;
 }
 vm_offset_t spm=ml_io_map(0x10006000,0x1000),top=ml_io_map(0x10000000,0x1000);
 vm_offset_t infra=ml_io_map(0x10001000,0x1000);
 if(!spm || !top || !infra)return;
 const uint32_t mask=(1u<<4)|(1u<<7)|(1u<<20)|(1u<<22)|(1u<<23)|(1u<<13);
 if((rd(spm,0x160)|rd(spm,0x164))&mask) {
  printf("BEGONIA GPU: inherited MFG domain state differs; untouched\n");return;
 }
 if((rd(infra,0x228)&(3u<<21))!=(3u<<21) ||
    (rd(infra,0x258)&(7u<<19))!=(7u<<19)) {
  printf("BEGONIA GPU: inherited bus-protection state differs; untouched\n");return;
 }
 /* MFG clock: documented crystal parent0, ungated; do not raise MFGPLL. */
 wr(top,0x58,(3u<<16)|(1u<<23));wr(top,0x4,1u<<14);
 wr(spm,0,0x0b160001);
 if(domain_on(spm,0x328,1u<<4,0) || domain_on(spm,0x32c,1u<<7,2))return;
 wr(infra,0x2a4,3u<<21);wr(infra,0x2ac,7u<<19);
 if(wait_bits(infra,0x228,3u<<21,0) || wait_bits(infra,0x258,7u<<19,0))return;
 const unsigned controls[]={0x330,0x334,0x338,0x350};
 const unsigned statusbits[]={20,22,23,13};
 for(unsigned i=0;i<4;i++)if(domain_on(spm,controls[i],1u<<statusbits[i],1))return;
 vm_offset_t cfg=ml_io_map(0x13fbf000,0x1000),gpu=ml_io_map(0x13040000,0x4000);
 if(!cfg || !gpu)return;
 wr(cfg,8,1);
 uint32_t id=rd(gpu,0);
 printf("BEGONIA GPU: ID=%08x shader=%x tiler=%x L2=%x at crystal clock\n",
    id,rd(gpu,0x100),rd(gpu,0x110),rd(gpu,0x120));
 if((id>>16)!=0x7211) {printf("BEGONIA GPU: identity mismatch; reset skipped\n");return;}
 wr(gpu,0x28,0);wr(gpu,0x24,0xffffffff);wr(gpu,0x30,1);
 if(wait_bits(gpu,0x20,1u<<8,1u<<8))return;
 wr(gpu,0x24,0xffffffff);
 printf("BEGONIA GPU: G76 soft reset complete; no GPU jobs or Metal service published\n");
}
