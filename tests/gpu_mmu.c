#include <assert.h>
#include <stdio.h>
#include "../kernel/bsd/dev/begonia_gpu_mmu.h"
int main(void) {
 unsigned index[4];
 assert(!begonia_gpu_indices(0x100000,index));
 assert(index[0]==0 && index[1]==0 && index[2]==0 && index[3]==256);
 uint64_t va=(511ull<<39)|(13ull<<30)|(17ull<<21)|(19ull<<12);
 assert(!begonia_gpu_indices(va,index));
 assert(index[0]==511 && index[1]==13 && index[2]==17 && index[3]==19);
 assert(begonia_gpu_indices(1ull<<48,index));
 assert(begonia_gpu_indices(0x100001,index));
 assert(begonia_gpu_table_entry(0x100004000)==0x100004003);
 assert(begonia_gpu_leaf_entry(0x100004000)==0x100004743);
 assert(!begonia_gpu_leaf_entry(1ull<<40));
 assert(!begonia_gpu_leaf_entry(0x100004001));
 assert(BEG_GPU_TRANSCFG==0x420001c6);
 puts("G76 MMU checks pass: four levels, VA48/PA40,4K alignment and permissions");
}
