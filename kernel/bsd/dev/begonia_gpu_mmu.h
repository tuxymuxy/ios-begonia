#ifndef BEGONIA_GPU_MMU_H
#define BEGONIA_GPU_MMU_H
#include <stdint.h>
/* G76 MMU_FEATURES0x2830:48-bit GPU VA,40-bit physical,4KiB pages. */
#define BEG_GPU_TRANSCFG (6u | (7u<<6) | (2u<<24) | (1u<<30))
static inline int begonia_gpu_indices(uint64_t address,unsigned index[4]) {
 if(address>=(1ull<<48) || (address&4095))return -1;
 for(unsigned level=0;level<4;level++)index[level]=(address>>(39-9*level))&511;
 return 0;
}
static inline uint64_t begonia_gpu_table_entry(uint64_t physical) {
 return physical && physical<(1ull<<40) && !(physical&4095)?physical|3ull:0;
}
static inline uint64_t begonia_gpu_leaf_entry(uint64_t physical) {
 uint64_t entry=begonia_gpu_table_entry(physical);
 return entry?entry|(1ull<<10)|(3ull<<8)|(1ull<<6):0;
}
#endif
