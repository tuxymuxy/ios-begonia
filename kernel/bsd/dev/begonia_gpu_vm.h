/* SPDX-License-Identifier: MIT
 * Owned four-level G76 tables. No identity mappings; caller owns every page.
 * Caller serializes access and must quiesce/reset GPU before releasing tables.
 */
#ifndef BEGONIA_GPU_VM_H
#define BEGONIA_GPU_VM_H
#include "begonia_gpu_mmu.h"
#define BEG_GPU_VM_MAX_TABLES 512
struct beg_gpu_vm_table { uint64_t physical; uint64_t *entries; };
struct beg_gpu_vm {
 struct beg_gpu_vm_table tables[BEG_GPU_VM_MAX_TABLES];
 unsigned count,limit;
 int (*allocate)(void *context,struct beg_gpu_vm_table *table);
 void *context;
};
static inline int beg_gpu_vm_new_table(struct beg_gpu_vm *vm) {
 if(!vm || !vm->allocate || !vm->limit || vm->limit>BEG_GPU_VM_MAX_TABLES || vm->count>=vm->limit)return -1;
 struct beg_gpu_vm_table table={0};
 if(vm->allocate(vm->context,&table))return -1;
 /* Allocation callback retains rejected allocations for caller cleanup. */
 if(!table.entries || !begonia_gpu_table_entry(table.physical))return -1;
 for(unsigned i=0;i<vm->count;i++)if(vm->tables[i].physical==table.physical)return -1;
 for(unsigned i=0;i<512;i++)if(table.entries[i])return -1;
 unsigned n=vm->count++;vm->tables[n]=table;return (int)n;
}
static inline int beg_gpu_vm_map(struct beg_gpu_vm *vm,uint64_t va,uint64_t physical) {
 unsigned index[4];uint64_t leaf=begonia_gpu_leaf_entry(physical);
 if(!vm || !leaf || begonia_gpu_indices(va,index))return -1;
 if(!vm->count && beg_gpu_vm_new_table(vm)!=0)return -1;
 unsigned table=0;
 for(unsigned level=0;level<3;level++) {
  uint64_t *entry=&vm->tables[table].entries[index[level]];
  if(!*entry) {
   int next=beg_gpu_vm_new_table(vm);if(next<0)return -1;
   *entry=begonia_gpu_table_entry(vm->tables[next].physical);table=(unsigned)next;
  } else {
   unsigned next=0;
   if((*entry&4095)!=3)return -1;
   for(;next<vm->count;next++)if(vm->tables[next].physical==(*entry&~4095ull))break;
   if(next==vm->count)return -1;table=next;
  }
 }
 uint64_t *entry=&vm->tables[table].entries[index[3]];
 if(*entry)return -2;
 *entry=leaf;return 0;
}
static inline uint64_t beg_gpu_vm_translate(const struct beg_gpu_vm *vm,uint64_t va) {
 if(!vm || !vm->count || va>=(1ull<<48))return 0;
 unsigned index[4];if(begonia_gpu_indices(va&~4095ull,index))return 0;
 unsigned table=0;
 for(unsigned level=0;level<4;level++) {
  uint64_t entry=vm->tables[table].entries[index[level]];
  if(!entry)return 0;
  if(level==3)return (entry&((1ull<<40)-4096))|(va&4095);
  if((entry&4095)!=3)return 0;
  unsigned next=0;for(;next<vm->count;next++)if(vm->tables[next].physical==(entry&~4095ull))break;
  if(next==vm->count)return 0;table=next;
 }
 return 0;
}
static inline int beg_gpu_vm_unmap(struct beg_gpu_vm *vm,uint64_t va) {
 unsigned index[4];if(!vm || !vm->count || begonia_gpu_indices(va,index))return -1;
 unsigned table=0;
 for(unsigned level=0;level<3;level++) {
  uint64_t entry=vm->tables[table].entries[index[level]];unsigned next=0;
  if((entry&4095)!=3)return -1;
  for(;next<vm->count;next++)if(vm->tables[next].physical==(entry&~4095ull))break;
  if(next==vm->count)return -1;table=next;
 }
 uint64_t *entry=&vm->tables[table].entries[index[3]];if(!*entry)return -1;
 *entry=0;return 0;
}
#endif
