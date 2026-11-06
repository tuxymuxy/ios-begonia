#ifndef BEGONIA_GPU_JOB_H
#define BEGONIA_GPU_JOB_H
#include <stdint.h>
#include <stddef.h>
/* Confine the descriptor, completion fields and write target to one owned page.
 * Caller must supply a newly zeroed kernel allocation, not user-provided memory. */
static int begonia_gpu_write_descriptor(uint32_t *words,size_t size,uint64_t pa)
{
    if(!words || size<4096 || size>65536 || (size&4095) || (pa&4095) ||
       pa<0x100000000ull || pa>0x1b7e00000ull-size)return -1;
    uint64_t target=pa+0x800;
    words[4]=0x00010005;
    words[8]=(uint32_t)target;words[9]=target>>32;
    words[10]=6;words[12]=0xbec07601;
    words[0x800/4]=0;
    return 0;
}
#endif
