#include <assert.h>
#include <stdio.h>
#include "../userland/gpu/begonia_gpu_params.h"
int main(void) {
 uint64_t value;
 assert(!begonia_gpu_mock_param(0,&value) && value==0x7211);
 assert(!begonia_gpu_mock_param(2,&value) && value==15);
 assert(!begonia_gpu_mock_param(14,&value) && value==768);
 assert(!begonia_gpu_mock_param(15,&value) && value==384);
 assert(!begonia_gpu_mock_param(23,&value) && value==0x4fe);
 assert(!begonia_gpu_mock_param(42,&value) && value==13000000);
 assert(begonia_gpu_mock_param(43,&value));
 assert(begonia_gpu_mock_param(44,&value));
 assert(begonia_gpu_mock_param(1000,&value));
 assert(begonia_gpu_mock_param(0,NULL));
 puts("Measured G76 profile checks pass: MC4, limits, slots, frequency and unsupported fields");
}
