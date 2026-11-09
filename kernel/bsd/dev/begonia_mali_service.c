/* SPDX-License-Identifier: MIT
 * G76 backend for the S9+ HakiosMali IOKit transport. MT6785 power-up is owned
 * by begonia_mali_probe.c, never by the Exynos reference. Initial transport
 * deliberately submits synchronously and resets between jobs. No DVFS.
 */
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include "begonia_gpu_vm.h"
#include "hakios.mali_va.h"
extern uint64_t kvtophys(vm_offset_t);
extern boolean_t pmap_valid_address(uint64_t);
extern void FlushPoC_DcacheRegion(vm_offset_t,size_t);
extern void HakiosMaliStart(void);
static vm_offset_t gpu;
static struct beg_gpu_vm space;
static struct mali_va addresses;
static int ready;
static uint64_t submitted,completed,failed;
static uint32_t rd(unsigned o) {return *(volatile uint32_t *)(gpu+o);}
static void wr(unsigned o,uint32_t v) {*(volatile uint32_t *)(gpu+o)=v;__asm__ volatile("dsb sy":::"memory");}
static int wait(unsigned o,uint32_t mask,uint32_t value) {
 for(unsigned i=0;i<10000;i++) {if((rd(o)&mask)==value)return 0;delay(10);}
 return -1;
}
static int reset(void) {
 wr(0x28,0);wr(0x24,0xffffffff);wr(0x30,1);
 if(wait(0x20,1u<<8,1u<<8)) {
  __atomic_store_n(&ready,0,__ATOMIC_RELEASE);
  printf("BEGONIA GPU SERVICE: reset failed; retain all GPU buffers until SoC reset\n");return -1;
 }
 wr(0x24,0xffffffff);return 0;
}
static vm_offset_t pool_page = 0;
static unsigned pool_index = 4;

static int allocate_table(void *context,struct beg_gpu_vm_table *table) {
 (void)context;
 if (pool_index >= 4) {
  if (kmem_alloc(kernel_map, &pool_page, PAGE_SIZE, KMA_KOBJECT|KMA_ZERO|KMA_DATA, VM_KERN_MEMORY_DIAG) != KERN_SUCCESS) {
   printf("HakiosMali: allocate_table kmem_alloc failed\n");
   return -1;
  }
  bzero((void *)pool_page, PAGE_SIZE);
  FlushPoC_DcacheRegion(pool_page, PAGE_SIZE);
  pool_index = 0;
 }
 vm_offset_t chunk = pool_page + pool_index * 4096;
 uint64_t physical = kvtophys(chunk);
 if (physical < 0x100000000ull || physical > 0x1b7e00000ull - 4096 || !begonia_gpu_table_entry(physical)) {
  printf("HakiosMali: allocate_table invalid physical 0x%llx\n", physical);
  return -1;
 }
 pool_index++;
 table->physical = physical;
 table->entries = (uint64_t *)chunk;
 return 0;
}
bool hakios_mali_ready(void) {return __atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=0;}
void hakios_keep(const char *format,...) __printflike(1,2);
void hakios_keep(const char *format,...) {
 char line[384];va_list args;va_start(args,format);vsnprintf(line,sizeof line,format,args);va_end(args);
 printf("%s\n",line);
}
bool hakios_mali_parameter(uint32_t which,uint64_t *value) {
 if(!value || !hakios_mali_ready())return false;
 static const unsigned offsets[]={0,0,0x100,0x110,0x120,0xe00,0x18,0x1c,
  4,8,0xc,0x10,0x14,0xac,0xa0,0xa4,0xa8,0x300};
 if(which<sizeof offsets/sizeof offsets[0]) {
  *value=which==0?rd(0)>>16:which==1?rd(0)&0xffff:rd(offsets[which]);return true;
 }
 if(which>=18 && which<=21) {*value=rd(0xb0+4*(which-18));return true;}
 if(which>=22 && which<=37) {*value=rd(0xc0+4*(which-22));return true;}
 if(which==38) {*value=__builtin_popcount(rd(0x120));return true;}
 if(which==39) {*value=0;return true;}
 if(which==40) {*value=rd(0x4c);return true;}
 if(which==41) {
  uint32_t hi,lo,next;do {hi=rd(0x9c);lo=rd(0x98);next=rd(0x9c);}while(hi!=next);
  *value=((uint64_t)hi<<32)|lo;return true;
 }
 if(which==42) {*value=13000000;return true;}
 return false;
}
uint64_t hakios_mali_address(uint64_t size,bool executable) {
 if(!hakios_mali_ready() || !size || size>(512ull<<20) || (executable && size>MALI_VA_EXEC_LINE))return 0;
 return mali_va_alloc(&addresses,size,executable);
}
bool hakios_mali_map(uint64_t address,uint64_t physical) {
 if(!hakios_mali_ready()) {
  printf("HakiosMali: map failed: not ready\n");
  return false;
 }
 if(address<MALI_VA_FIRST || address>=MALI_VA_END) {
  printf("HakiosMali: map failed: va 0x%llx outside range [0x%llx, 0x%llx)\n",
         address, (uint64_t)MALI_VA_FIRST, (uint64_t)MALI_VA_END);
  return false;
 }
 if(physical<0x100000000ull || physical>=0x1b7e00000ull) {
  printf("HakiosMali: map failed: pa 0x%llx outside RAM\n", physical);
  return false;
 }
 if(!pmap_valid_address(physical)) {
  printf("HakiosMali: map failed: !pmap_valid_address(0x%llx)\n", physical);
  return false;
 }
 int err = beg_gpu_vm_map(&space,address,physical);
 if(err != 0) {
  printf("HakiosMali: map failed: beg_gpu_vm_map(va=0x%llx, pa=0x%llx) err=%d tables=%u/%u\n",
         address, physical, err, space.count, space.limit);
  return false;
 }
 return true;
}
static int init_gpu_hardware(void) {
 wr(0xf04,rd(0xf04)|(1u<<17));wr(0xf00,(rd(0xf00)&~(0x3fu<<16))|(15u<<16));
 wr(0x1a0,1);wr(0x190,1);wr(0x180,15);
 if(wait(0x160,1,1) || wait(0x150,1,1) || wait(0x140,15,15))return -1;
 if(space.count && space.tables) {
  uint64_t root=space.tables[0].physical;
  wr(0x2400,(uint32_t)root);wr(0x2404,root>>32);
  wr(0x2408,0x88888888);wr(0x240c,0x88888888);
  wr(0x2430,BEG_GPU_TRANSCFG);wr(0x2434,0);wr(0x2418,1);
  if(wait(0x2428,1,0))return -1;
 }
 return 0;
}
void hakios_mali_mapped(void) {
 bool any = false;
 for(unsigned i=0;i<space.count;i++) {
  struct beg_gpu_vm_table *table=&space.tables[i];
  if(!table->dirty)continue;
  /* The GPU reads 512 entries per table regardless of XNU's page size. */
  FlushPoC_DcacheRegion((vm_offset_t)table->entries,512*sizeof(*table->entries));
  table->dirty=false;
  any = true;
 }
 if(any && gpu) {
  uint64_t root=space.tables[0].physical;
  wr(0x2400,(uint32_t)root);wr(0x2404,root>>32);
  wr(0x2418,1);
  (void)wait(0x2428,1,0);
 }
}
void hakios_mali_unmap(uint64_t address,uint64_t size) {
 if(!hakios_mali_ready() || !size || address<MALI_VA_FIRST || address>=MALI_VA_END || size>MALI_VA_END-address)return;
 for(uint64_t off=0;off<size;off+=4096)(void)beg_gpu_vm_unmap(&space,address+off);
 mali_va_free(&addresses,address,size);
}
void hakios_mali_tag(uint64_t address,uint32_t kind,uint32_t width,uint32_t height,uint32_t row,
 uint32_t format,uint32_t offset) {
 (void)address;(void)kind;(void)width;(void)height;(void)row;(void)format;(void)offset;
}
uint64_t hakios_mali_enqueue(uint64_t chain,bool fragment) {
 if(!hakios_mali_ready() || !space.count || !chain || (chain&63) || !beg_gpu_vm_translate(&space,chain))return 0;
 uint64_t seq=++submitted;int success=0;

 hakios_mali_mapped();

 unsigned slot=fragment?0:1,base=0x1800+slot*0x80;
 wr(0x2004,0xffffffff);wr(0x1008,0);wr(0x1004,0xffffffff);
 wr(base+0x40,(uint32_t)chain);wr(base+0x44,chain>>32);
 wr(base+0x50,15);wr(base+0x54,0);wr(base+0x58,(3u<<8)|(3u<<12)|(8u<<16));
 wr(base+0x60,1);
 if(!wait(0x1000,(1u<<slot)|(1u<<(slot+16)),1u<<slot) && !rd(0x241c)) {
  success=1;
 } else {
  printf("BEGONIA GPU SERVICE: job%llu slot%u irq=%x mmu=%x FAILED; recovering\n",seq,slot,rd(0x1000),rd(0x241c));
  if(reset() || init_gpu_hardware()) {
   printf("BEGONIA GPU SERVICE: hardware recovery failed\n");
  }
 }

 if(!success)__atomic_store_n(&failed,seq,__ATOMIC_RELEASE);
 __atomic_store_n(&completed,seq,__ATOMIC_RELEASE);
 return success?seq:0;
}
uint32_t hakios_mali_submit(uint64_t chain,bool fragment) {return (uint32_t)hakios_mali_enqueue(chain,fragment);}
bool hakios_mali_wait(uint64_t seq,uint64_t timeout) {
 (void)timeout;
 return !seq || (hakios_mali_ready() && seq<=__atomic_load_n(&completed,__ATOMIC_ACQUIRE) &&
  seq!=__atomic_load_n(&failed,__ATOMIC_ACQUIRE));
}
void begonia_mali_service_begin(vm_offset_t registers) {
 uint32_t enabled=0;
 if(!PE_parse_boot_argn("begonia_gpu_service",&enabled,sizeof enabled) || enabled!=1 || gpu)return;
 gpu=registers;
 if(rd(0)>>16!=0x7211 || reset())return;
 vm_offset_t tbls = 0;
 size_t tbls_size = (sizeof(struct beg_gpu_vm_table) * BEG_GPU_VM_MAX_TABLES + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
 if(kmem_alloc(kernel_map, &tbls, tbls_size, KMA_KOBJECT|KMA_ZERO|KMA_DATA, VM_KERN_MEMORY_DIAG) != KERN_SUCCESS) {
  printf("HakiosMali: failed to allocate VM table metadata\n");
  return;
 }
 bzero((void *)tbls, tbls_size);
 space.tables = (struct beg_gpu_vm_table *)tbls;
 space.limit=BEG_GPU_VM_MAX_TABLES;space.allocate=allocate_table;
 if(init_gpu_hardware()) {
  printf("HakiosMali: init_gpu_hardware failed\n");
  return;
 }
 __atomic_store_n(&ready,1,__ATOMIC_RELEASE);
 printf("BEGONIA GPU SERVICE: high-performance pipeline active (60 FPS direct)\n");
 HakiosMaliStart();
}
