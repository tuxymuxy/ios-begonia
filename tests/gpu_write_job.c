#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/bsd/dev/begonia_gpu_job.h"
int main(void) {
 static uint32_t page[4096];
 uint64_t base=0x100004000ull;
 assert(!begonia_gpu_write_descriptor(page,sizeof page,base));
 assert(((uint64_t)page[9]<<32|page[8])==base+0x800);
 assert(page[4]==0x10005 && page[10]==6 && page[12]==0xbec07601);
 assert(page[0x800/4]==0);
 uint32_t before=page[4];
 assert(begonia_gpu_write_descriptor(page,2048,base));
 assert(begonia_gpu_write_descriptor(page,sizeof page,base+1));
 assert(begonia_gpu_write_descriptor(page,sizeof page,0x40000000));
 assert(begonia_gpu_write_descriptor(page,sizeof page,0x1b7e00000));
 assert(begonia_gpu_write_descriptor(NULL,sizeof page,base));
 assert(page[4]==before);
 assert(!begonia_gpu_write_descriptor(page,sizeof page,0x1b7e00000ull-sizeof page));
 puts("GPU job checks pass: 64bit address, page bounds, alignment, mirror exclusion");
}
