/* SPDX-License-Identifier: MIT
 * MT6785/G76 opt-in power/reset/identity experiment. Separate bounded
 * selftest can submit one write-value job confined to a private kernel page.
 * Register facts: pinned Xiaomi clk-mt6785[-pg].c and Mali job-manager layout.
 * Never entered by normal UI builds. Requires measured, enabled MT6359 rails.
 */
#include <kern/clock.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <vm/vm_kern.h>
#include <libkern/libkern.h>
#include "begonia_gpu_job.h"
#include "begonia_gpu_mmu.h"
#include "begonia_gpu_fixture_data.h"
extern uint64_t kvtophys(vm_offset_t);
extern void FlushPoC_DcacheRegion(vm_offset_t,size_t);
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
 printf("BEGONIA GPU: power control %x status-bit %x\n",control,status);
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
static int write_value_test(vm_offset_t gpu) {
 uint32_t enabled=0;int succeeded=0;
 if(!PE_parse_boot_argn("begonia_gpu_selftest",&enabled,sizeof(enabled)) || enabled!=1)return 0;
 /* GPU internal blocks are reset-off even after the SPM domains come up. */
 wr(gpu,0x1a0,1);wr(gpu,0x190,1);wr(gpu,0x180,15);
 if(wait_bits(gpu,0x160,1,1) || wait_bits(gpu,0x150,1,1) || wait_bits(gpu,0x140,15,15))return 0;
 vm_offset_t page=0;
 if(kmem_alloc(kernel_map,&page,PAGE_SIZE,KMA_KOBJECT|KMA_ZERO|KMA_DATA,VM_KERN_MEMORY_DIAG)!=KERN_SUCCESS)return 0;
 uint64_t physical=kvtophys(page);
 if(begonia_gpu_write_descriptor((uint32_t *)page,PAGE_SIZE,physical)) {
  printf("BEGONIA GPU: selftest page outside owned managed RAM bounds\n");
  kmem_free(kernel_map,page,PAGE_SIZE);return 0;
 }
 FlushPoC_DcacheRegion(page,PAGE_SIZE);
 /* AS0 identity mode2, no translated page tables; every memory attribute WB. */
 wr(gpu,0x2400,0);wr(gpu,0x2404,0);
 wr(gpu,0x2408,0x88888888);wr(gpu,0x240c,0x88888888);
 wr(gpu,0x2430,2);wr(gpu,0x2434,0);wr(gpu,0x2418,1);
 if(wait_bits(gpu,0x2428,1,0))goto reset;
 wr(gpu,0x1008,0);wr(gpu,0x1004,0xffffffff);
 wr(gpu,0x18c0,(uint32_t)physical);wr(gpu,0x18c4,physical>>32);
 wr(gpu,0x18d0,15);wr(gpu,0x18d4,0);
 wr(gpu,0x18d8,(3u<<8)|(3u<<12)|(8u<<16));
 printf("BEGONIA GPU: submitting isolated WRITE_VALUE job\n");
 wr(gpu,0x18e0,1);
 (void)wait_bits(gpu,0x1000,(1u<<1)|(1u<<17),1u<<1);
 FlushPoC_DcacheRegion(page,PAGE_SIZE);
 succeeded=*(volatile uint32_t *)(page+0x800)==0xbec07601 &&
   (rd(gpu,0x1000)&((1u<<1)|(1u<<17)))==(1u<<1) && !rd(gpu,0x241c);
 printf("BEGONIA GPU: selftest irq=%x slot=%x mmu=%x word=%08x expected=bec07601\n",
  rd(gpu,0x1000),rd(gpu,0x18a4),rd(gpu,0x241c),*(volatile uint32_t *)(page+0x800));
reset:
 /* A timed-out GPU must not retain DMA access to a freed page. */
 wr(gpu,0x24,0xffffffff);wr(gpu,0x30,1);
 if(!wait_bits(gpu,0x20,1u<<8,1u<<8)) {
  kmem_free(kernel_map,page,PAGE_SIZE);
 } else {printf("BEGONIA GPU: reset timeout; retaining selftest page until SoC reset\n");return 0;}
 return succeeded;
}

static int mapped_write_test(vm_offset_t gpu) {
 uint32_t enabled=0;int succeeded=0;
 if(!PE_parse_boot_argn("begonia_gpu_mapped",&enabled,sizeof(enabled)) || enabled!=1)return 0;
 vm_offset_t pages[5]={0};uint64_t physical[5]={0};unsigned allocated=0;int armed=0;
 const uint64_t address=0x100000;unsigned indices[4];
 if(begonia_gpu_indices(address,indices))return 0;
 wr(gpu,0x1a0,1);wr(gpu,0x190,1);wr(gpu,0x180,15);
 if(wait_bits(gpu,0x160,1,1) || wait_bits(gpu,0x150,1,1) || wait_bits(gpu,0x140,15,15))return 0;
 for(unsigned i=0;i<5;i++) {
  if(kmem_alloc(kernel_map,&pages[i],PAGE_SIZE,KMA_KOBJECT|KMA_ZERO|KMA_DATA,VM_KERN_MEMORY_DIAG)!=KERN_SUCCESS)goto finish;
  allocated++;physical[i]=kvtophys(pages[i]);
  if(physical[i]<0x100000000ull || physical[i]>0x1b7e00000ull-PAGE_SIZE ||
     !begonia_gpu_table_entry(physical[i]))goto finish;
 }
 for(unsigned i=0;i<3;i++)((uint64_t *)pages[i])[indices[i]]=begonia_gpu_table_entry(physical[i+1]);
 ((uint64_t *)pages[3])[indices[3]]=begonia_gpu_leaf_entry(physical[4]);
 if(begonia_gpu_write_descriptor((uint32_t *)pages[4],PAGE_SIZE,physical[4]))goto finish;
 ((uint32_t *)pages[4])[8]=(uint32_t)(address+0x800);
 ((uint32_t *)pages[4])[9]=0;
 for(unsigned i=0;i<5;i++)FlushPoC_DcacheRegion(pages[i],PAGE_SIZE);
 armed=1;
 wr(gpu,0x2400,(uint32_t)physical[0]);wr(gpu,0x2404,physical[0]>>32);
 wr(gpu,0x2408,0x88888888);wr(gpu,0x240c,0x88888888);
 wr(gpu,0x2430,BEG_GPU_TRANSCFG);wr(gpu,0x2434,0);wr(gpu,0x2418,1);
 if(wait_bits(gpu,0x2428,1,0))goto finish;
 wr(gpu,0x2004,0xffffffff);wr(gpu,0x1008,0);wr(gpu,0x1004,0xffffffff);
 wr(gpu,0x18c0,(uint32_t)address);wr(gpu,0x18c4,0);
 wr(gpu,0x18d0,15);wr(gpu,0x18d4,0);wr(gpu,0x18d8,(3u<<8)|(3u<<12)|(8u<<16));
 printf("BEGONIA GPU: submitting VA48 four-level mapped WRITE_VALUE job\n");
 wr(gpu,0x18e0,1);(void)wait_bits(gpu,0x1000,(1u<<1)|(1u<<17),1u<<1);
 FlushPoC_DcacheRegion(pages[4],PAGE_SIZE);
 succeeded=*(volatile uint32_t *)(pages[4]+0x800)==0xbec07601 &&
   (rd(gpu,0x1000)&((1u<<1)|(1u<<17)))==(1u<<1) && !rd(gpu,0x241c);
 printf("BEGONIA GPU: mapped selftest irq=%x slot=%x mmu=%x faultaddr=%x/%x word=%08x expected=bec07601\n",
  rd(gpu,0x1000),rd(gpu,0x18a4),rd(gpu,0x241c),rd(gpu,0x2424),rd(gpu,0x2420),*(volatile uint32_t *)(pages[4]+0x800));
finish:
 if(armed) {
  wr(gpu,0x24,0xffffffff);wr(gpu,0x30,1);
  if(wait_bits(gpu,0x20,1u<<8,1u<<8)) {
   printf("BEGONIA GPU: mapped reset timeout; retaining %u pages until SoC reset\n",allocated);return 0;
  }
 }
 for(unsigned i=0;i<allocated;i++)kmem_free(kernel_map,pages[i],PAGE_SIZE);
 return succeeded;
}

static int shader_compute_test(vm_offset_t gpu) {
 uint32_t enabled=0;int succeeded=0;
 if(!PE_parse_boot_argn("begonia_gpu_shader",&enabled,sizeof(enabled)) || enabled!=1)return 0;
#if BEG_GPU_FIXTURE_PRESENT
 /* One own-source compute chain. Every GPU VA maps only private allocations;
  * no identity mapping, IOSurface, user pointer, or live display buffer. */
 vm_offset_t pages[36]={0},output=0,job=0;uint64_t physical[36]={0};
 unsigned allocated=0,next=4;int armed=0;
 const unsigned count=BEG_GPU_FIXTURE_PAGES+4;
 unsigned index[4];
 if(PAGE_SIZE!=16384 || count>36 || begonia_gpu_indices(BEG_GPU_FIXTURE_JC&~4095ull,index))return 0;
 /* G76 features in Linux Panfrost v6.16: TLS hashing and IDVS group size.
  * Do not copy the G57-specific varying-algorithm workaround (bit29). */
 wr(gpu,0xf04,rd(gpu,0xf04)|(1u<<17));
 wr(gpu,0xf00,(rd(gpu,0xf00)&~(0x3fu<<16))|(15u<<16));
 wr(gpu,0x1a0,1);wr(gpu,0x190,1);wr(gpu,0x180,15);
 if(wait_bits(gpu,0x160,1,1) || wait_bits(gpu,0x150,1,1) || wait_bits(gpu,0x140,15,15))return 0;
 for(unsigned i=0;i<count;i++) {
  if(kmem_alloc(kernel_map,&pages[i],PAGE_SIZE,KMA_KOBJECT|KMA_ZERO|KMA_DATA,VM_KERN_MEMORY_DIAG)!=KERN_SUCCESS)goto finish;
  allocated++;physical[i]=kvtophys(pages[i]);
  if(physical[i]<0x100000000ull || physical[i]>0x1b7e00000ull-PAGE_SIZE ||
     !begonia_gpu_table_entry(physical[i]))goto finish;
 }
 for(unsigned i=0;i<3;i++)((uint64_t *)pages[i])[index[i]]=begonia_gpu_table_entry(physical[i+1]);
 for(unsigned b=0;b<sizeof(beg_gpu_blobs)/sizeof(beg_gpu_blobs[0]);b++) {
  const struct beg_gpu_blob *blob=&beg_gpu_blobs[b];
  if(!blob->size || blob->size%PAGE_SIZE || blob->va%PAGE_SIZE)goto finish;
  for(unsigned off=0;off<blob->size;off+=PAGE_SIZE) {
   uint64_t va=blob->va+off;unsigned indices[4];
   if(next>=count || begonia_gpu_indices(va,indices) ||
      indices[0]!=index[0] || indices[1]!=index[1] || indices[2]!=index[2])goto finish;
   memcpy((void *)pages[next],blob->bytes+off,PAGE_SIZE);
   for(unsigned sub=0;sub<4;sub++) {
    if(((uint64_t *)pages[3])[indices[3]+sub])goto finish;
    ((uint64_t *)pages[3])[indices[3]+sub]=begonia_gpu_leaf_entry(physical[next]+4096*sub);
   }
   if(va<=BEG_GPU_FIXTURE_OUTPUT && BEG_GPU_FIXTURE_OUTPUT+256<=va+PAGE_SIZE)
    output=pages[next]+BEG_GPU_FIXTURE_OUTPUT-va;
   if(va<=BEG_GPU_FIXTURE_JC && BEG_GPU_FIXTURE_JC+256<=va+PAGE_SIZE)
    job=pages[next]+BEG_GPU_FIXTURE_JC-va;
   next++;
  }
 }
 if(next!=count || !output || !job)goto finish;
 for(unsigned i=0;i<64;i++)if(((uint32_t *)output)[i]!=0xdeadbeef)goto finish;
 for(unsigned i=0;i<count;i++)FlushPoC_DcacheRegion(pages[i],PAGE_SIZE);
 armed=1;
 wr(gpu,0x2400,(uint32_t)physical[0]);wr(gpu,0x2404,physical[0]>>32);
 wr(gpu,0x2408,0x88888888);wr(gpu,0x240c,0x88888888);
 wr(gpu,0x2430,BEG_GPU_TRANSCFG);wr(gpu,0x2434,0);wr(gpu,0x2418,1);
 if(wait_bits(gpu,0x2428,1,0))goto finish;
 wr(gpu,0x2004,0xffffffff);wr(gpu,0x1008,0);wr(gpu,0x1004,0xffffffff);
 wr(gpu,0x18c0,(uint32_t)BEG_GPU_FIXTURE_JC);wr(gpu,0x18c4,BEG_GPU_FIXTURE_JC>>32);
 wr(gpu,0x18d0,15);wr(gpu,0x18d4,0);wr(gpu,0x18d8,(3u<<8)|(3u<<12)|(8u<<16));
 printf("BEGONIA GPU: submitting own compute shader,64 results expected i*3+7\n");
 wr(gpu,0x18e0,1);(void)wait_bits(gpu,0x1000,(1u<<1)|(1u<<17),1u<<1);
 for(unsigned i=0;i<count;i++)FlushPoC_DcacheRegion(pages[i],PAGE_SIZE);
 unsigned matches=0;for(unsigned i=0;i<64;i++)if(((volatile uint32_t *)output)[i]==i*3+7)matches++;
 printf("BEGONIA GPU: shader irq=%x slot=%x mmu=%x faultaddr=%x/%x exception=%x matches=%u/64 first=%x last=%x\n",
  rd(gpu,0x1000),rd(gpu,0x18a4),rd(gpu,0x241c),rd(gpu,0x2424),rd(gpu,0x2420),
  *(volatile uint32_t *)job,matches,*(volatile uint32_t *)output,((volatile uint32_t *)output)[63]);
 succeeded=matches==64 && rd(gpu,0x1000)==2 && !rd(gpu,0x241c) && *(volatile uint32_t *)job==1;
finish:
 if(armed) {
  wr(gpu,0x24,0xffffffff);wr(gpu,0x30,1);
  if(wait_bits(gpu,0x20,1u<<8,1u<<8)) {
   printf("BEGONIA GPU: shader reset timeout; retaining %u pages until SoC reset\n",allocated);return 0;
  }
 }
 for(unsigned i=0;i<allocated;i++)kmem_free(kernel_map,pages[i],PAGE_SIZE);
#else
 printf("BEGONIA GPU: own-source shader fixture absent; test skipped\n");
#endif
 return succeeded;
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
  printf("BEGONIA GPU: inherited MFG state %08x/%08x differs; untouched\n",rd(spm,0x160),rd(spm,0x164));return;
 }
 printf("BEGONIA GPU: inherited protection %08x/%08x; normalizing owned MFG bits\n",rd(infra,0x228),rd(infra,0x258));
 /* Domains are confirmed off. The loader leaves only part of the GPU's
  * protection asserted. Normalize those GPU-only requests before power-up. */
 wr(infra,0x2a8,7u<<19);
 if(wait_bits(infra,0x258,7u<<19,7u<<19))return;
 wr(infra,0x2a0,3u<<21);
 if(wait_bits(infra,0x228,3u<<21,3u<<21))return;
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
 printf("BEGONIA GPU: domains ready, ungating BG3D before ID read\n");
 wr(cfg,8,1);
 uint32_t id=rd(gpu,0);
 printf("BEGONIA GPU: ID=%08x shader=%x tiler=%x L2=%x at crystal clock\n",
    id,rd(gpu,0x100),rd(gpu,0x110),rd(gpu,0x120));
 if((id>>16)!=0x7211) {printf("BEGONIA GPU: identity mismatch; reset skipped\n");return;}
 wr(gpu,0x28,0);wr(gpu,0x24,0xffffffff);wr(gpu,0x30,1);
 if(wait_bits(gpu,0x20,1u<<8,1u<<8))return;
 wr(gpu,0x24,0xffffffff);
 printf("BEGONIA GPU: G76 soft reset complete; no Metal service published\n");
 if(write_value_test(gpu) && mapped_write_test(gpu) && shader_compute_test(gpu)) {
  extern void begonia_mali_service_begin(vm_offset_t);
  begonia_mali_service_begin(gpu);
 }
}
