#include <assert.h>
#include <stdio.h>
#include "../kernel/bsd/dev/begonia_clock_policy.h"
int main(void) {
 uint32_t a=0,b=0;
 assert(!begonia_clock_values(800000,900000,0x8116989d,0x821789d8,&a,&b));
 assert((a&0x7fc00000)==(0x8116989d&0x7fc00000));
 assert((b&0x7fc00000)==(0x821789d8&0x7fc00000));
 uint64_t ll=(uint64_t)(a&0x3fffff)*26000000/(16384*2);
 uint64_t cci=(uint64_t)(b&0x3fffff)*26000000/(16384*4);
 assert(ll<=1275000000 && ll>1274999000);
 assert(cci<=875000000 && cci>874999000);
 assert(begonia_clock_values(775000,900000,0x8116989d,0x821789d8,&a,&b));
 assert(begonia_clock_values(800000,875000,0x8116989d,0x821789d8,&a,&b));
 assert(begonia_clock_values(800000,1100000,0x8116989d,0x821789d8,&a,&b));
 assert(begonia_clock_values(800000,900000,0x8116989c,0x821789d8,&a,&b));
 puts("Clock policy passes: stock targets, preserved fields, rail bounds, unexpected PLL refusal");
}
