/* XNU must not advertise the Linux EL0 CTR/DC cache helper implementation. */
#include "util/cache_ops.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
#ifdef __APPLE__
 assert(!util_has_cache_ops());
 puts("PASS: Darwin does not advertise ungranted EL0 cache operations");
#else
 puts("SKIP: Darwin-only cache availability check");
#endif
}
