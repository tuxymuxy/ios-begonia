#ifndef BEGONIA_CLOCK_POLICY_H
#define BEGONIA_CLOCK_POLICY_H
#include <stdint.h>
/* Conservative first operating points from captured MT6785 Linux DT. */
static int begonia_clock_values(uint32_t cpu_uv,uint32_t sram_uv,
    uint32_t ll,uint32_t cci,uint32_t *new_ll,uint32_t *new_cci)
{
    if(cpu_uv<800000 || cpu_uv>850000 || sram_uv<900000 ||
       sram_uv>1000000 || sram_uv<cpu_uv+100000 || sram_uv>cpu_uv+250000)
        return -1;
    if((ll&0x7fffffff)!=0x0116989d || (cci&0x7fffffff)!=0x021789d8)
        return -1;
    uint32_t a=(uint64_t)1275000000*2*16384/26000000;
    uint32_t b=(uint64_t)875000000*4*16384/26000000;
    *new_ll=(ll&~0x3fffffu)|a|0x80000000u;
    *new_cci=(cci&~0x3fffffu)|b|0x80000000u;
    return 0;
}
#endif
