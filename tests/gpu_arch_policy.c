#include <assert.h>
#include <stdio.h>
#include "../userland/gpu/begonia_gpu_support.h"
int main(void) {
 assert(!begonia_use_direct_valhall(0x7211));
 assert(!begonia_use_direct_valhall(0));
 assert(!begonia_use_direct_valhall(0xa002));
 assert(!begonia_use_direct_valhall(0x9000));
 assert(begonia_use_direct_valhall(0x9091));
 assert(begonia_use_direct_valhall(0x9093));
 puts("GPU ISA policy passes: G76/unknown bypass Valhall; known G57 retains direct path");
}
