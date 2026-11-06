#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/bsd/dev/begonia_gpu_vm.h"
static uint64_t memory[32][512];static unsigned allocated;static int fail;
static int allocate(void *ctx,struct beg_gpu_vm_table *table) {
 (void)ctx;if(fail || allocated==32)return -1;
 table->entries=memory[allocated];table->physical=0x100000000ull+allocated*16384ull;
 allocated++;return 0;
}
static void reset(struct beg_gpu_vm *vm,unsigned limit) {
 memset(memory,0,sizeof memory);memset(vm,0,sizeof *vm);allocated=0;fail=0;
 vm->allocate=allocate;vm->limit=limit;
}
int main(void) {
 struct beg_gpu_vm vm;reset(&vm,32);
 const uint64_t addresses[]={0,0x2000000,0x1ff000,0x200000,0x40000000,0x100000000ull,0xfffffff000ull};
 for(unsigned i=0;i<sizeof addresses/sizeof addresses[0];i++) {
  uint64_t physical=0x110000000ull+4096*i;
  assert(!beg_gpu_vm_map(&vm,addresses[i],physical));
  assert(beg_gpu_vm_translate(&vm,addresses[i]+123)==physical+123);
 }
 assert(beg_gpu_vm_map(&vm,0,0x120000000ull)==-2);
 assert(beg_gpu_vm_translate(&vm,0)==0x110000000ull);
 assert(!beg_gpu_vm_unmap(&vm,0));assert(!beg_gpu_vm_translate(&vm,0));
 assert(beg_gpu_vm_unmap(&vm,0));assert(!beg_gpu_vm_map(&vm,0,0x120000000ull));
 assert(beg_gpu_vm_translate(&vm,0)==0x120000000ull);
 assert(!beg_gpu_vm_translate(&vm,0x12345000));
 assert(beg_gpu_vm_map(&vm,1,0x110000000ull));
 assert(beg_gpu_vm_map(&vm,1ull<<48,0x110000000ull));
 assert(beg_gpu_vm_map(&vm,0x1000,1ull<<40));
 reset(&vm,3);assert(beg_gpu_vm_map(&vm,0,0x110000000ull));
 assert(!beg_gpu_vm_translate(&vm,0));assert(vm.count==3);
 vm.limit=4;assert(!beg_gpu_vm_map(&vm,0,0x110000000ull));
 reset(&vm,32);fail=1;assert(beg_gpu_vm_map(&vm,0,0x110000000ull));assert(!vm.count);
 reset(&vm,32);assert(!beg_gpu_vm_map(&vm,0,0x110000000ull));
 vm.tables[0].entries[1]=0x12345003;assert(beg_gpu_vm_map(&vm,1ull<<39,0x110000000ull));
 puts("G76 VM checks pass: disjoint VA regions, owned tables, overlap refusal, allocation failure and bounds");
}
