/* SPDX-License-Identifier: MIT
 * Owned four-level G76 tables. No identity mappings; caller owns every page.
 * Caller serializes access and must quiesce/reset GPU before releasing tables.
 */
#ifndef BEGONIA_GPU_VM_H
#define BEGONIA_GPU_VM_H
#include <stdbool.h>
#include "begonia_gpu_mmu.h"
#define BEG_GPU_VM_MAX_TABLES 4096
struct beg_gpu_vm_table {
 uint64_t physical;
 uint64_t *entries;
 bool dirty;
};
struct beg_gpu_vm {
 struct beg_gpu_vm_table *tables;
 unsigned count,limit;
 int (*allocate)(void *context,struct beg_gpu_vm_table *table);
 void *context;
};
static inline int beg_gpu_vm_new_table(struct beg_gpu_vm *vm) {
 if(!vm || !vm->tables || !vm->allocate || !vm->limit || vm->limit>BEG_GPU_VM_MAX_TABLES || vm->count>=vm->limit) {
  printf("beg_gpu_vm_new_table: count %u >= limit %u\n", vm ? vm->count : 0, vm ? vm->limit : 0);
  return -1;
 }
 struct beg_gpu_vm_table table={0};
 if(vm->allocate(vm->context,&table)) {
  printf("beg_gpu_vm_new_table: allocate callback failed\n");
  return -1;
 }
 /* Allocation callback retains rejected allocations for caller cleanup. */
 if(!table.entries || !begonia_gpu_table_entry(table.physical)) {
  printf("beg_gpu_vm_new_table: bad entries %p or table_entry 0x%llx\n", table.entries, table.physical);
  return -1;
 }
 for(unsigned i=0;i<vm->count;i++)if(vm->tables[i].physical==table.physical) {
  printf("beg_gpu_vm_new_table: duplicate physical table 0x%llx\n", table.physical);
  return -1;
 }
 for(unsigned i=0;i<512;i++)if(table.entries[i]) {
  printf("beg_gpu_vm_new_table: dirty entry at %u: 0x%llx\n", i, table.entries[i]);
  return -1;
 }
 table.dirty=true;
 unsigned n=vm->count++;vm->tables[n]=table;return (int)n;
}
static inline int beg_gpu_vm_map(struct beg_gpu_vm *vm,uint64_t va,uint64_t physical) {
 unsigned index[4];uint64_t leaf=begonia_gpu_leaf_entry(physical);
 if(!vm || !leaf || begonia_gpu_indices(va,index)) {
  printf("beg_gpu_vm_map: leaf=0x%llx or indices fail for va=0x%llx\n", leaf, va);
  return -1;
 }
 if(!vm->count && beg_gpu_vm_new_table(vm)!=0) {
  printf("beg_gpu_vm_map: initial root table allocation failed\n");
  return -1;
 }
 unsigned table=0;
 for(unsigned level=0;level<3;level++) {
  uint64_t *entry=&vm->tables[table].entries[index[level]];
  if(!*entry) {
   int next=beg_gpu_vm_new_table(vm);
   if(next<0) {
    printf("beg_gpu_vm_map: new_table failed at level %u for va 0x%llx\n", level, va);
    return -1;
   }
   *entry=begonia_gpu_table_entry(vm->tables[next].physical);
   vm->tables[table].dirty=true;
   table=(unsigned)next;
  } else {
   unsigned next=0;
   if((*entry&4095)!=3) {
    printf("beg_gpu_vm_map: entry at level %u bad flags 0x%llx\n", level, *entry);
    return -1;
   }
   for(;next<vm->count;next++)if(vm->tables[next].physical==(*entry&~4095ull))break;
   if(next==vm->count) {
    printf("beg_gpu_vm_map: level %u next table 0x%llx not found\n", level, *entry&~4095ull);
    return -1;
   }
   table=next;
  }
 }
 uint64_t *entry=&vm->tables[table].entries[index[3]];
 if(*entry) {
  printf("beg_gpu_vm_map: leaf collision at va 0x%llx: old=0x%llx new=0x%llx\n", va, *entry, leaf);
  return -2;
 }
 *entry=leaf;vm->tables[table].dirty=true;return 0;
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
 *entry=0;vm->tables[table].dirty=true;return 0;
}
#endif
