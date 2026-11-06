#include <assert.h>
#include <stdio.h>
#include "../kernel/bsd/dev/begonia_touch_diagnostics.h"
int main(void) {
 struct begonia_touch_diagnostics d={0};
 d.magic=BEG_TOUCH_DIAG_MAGIC;d.version=1;d.bytes=sizeof(d);d.sequence=2;
 d.polls=12345;d.checksum=begonia_touch_diag_checksum(&d);
 assert(begonia_touch_diag_valid(&d));
 d.polls++;assert(!begonia_touch_diag_valid(&d));d.polls--;
 d.sequence++;d.checksum=begonia_touch_diag_checksum(&d);assert(!begonia_touch_diag_valid(&d));
 d.sequence++;d.checksum=begonia_touch_diag_checksum(&d);assert(begonia_touch_diag_valid(&d));
 d.bytes--;assert(!begonia_touch_diag_valid(&d));
 assert(BEG_TOUCH_DIAG_PHYS>=0x1b7e00000ull+128+0x40000+1536*1024);
 assert(BEG_TOUCH_DIAG_PHYS+4096==0x1b8000000ull);
 puts("Touch record checks pass: ABI, checksum, torn write and reserved-memory bounds");
}
