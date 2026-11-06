#ifndef BEGONIA_GPU_SUPPORT_H
#define BEGONIA_GPU_SUPPORT_H
#include <stdint.h>
/* The reference direct ISA compiler targets G57. Other GPUs use Mesa/NIR. */
static inline int begonia_use_direct_valhall(uint32_t product)
{
    return product==0x9091 || product==0x9093;
}
#endif
