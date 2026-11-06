#ifndef BEGONIA_TOUCH_DIAGNOSTICS_H
#define BEGONIA_TOUCH_DIAGNOSTICS_H
#include <stdint.h>
#include <stddef.h>
/* Last4KiB of the existing2MiB no-map log reservation; outside mirror payload. */
#define BEG_TOUCH_DIAG_PHYS 0x1b7fff000ull
#define BEG_TOUCH_DIAG_MAGIC 0x4843554f54474542ull
struct begonia_touch_diagnostics {
 uint64_t magic,epoch;
 uint32_t version,sequence,stage,polls,bad,events;
 uint32_t reasons[7];
 uint8_t last[8];
 uint32_t irq;
 uint64_t longest_read_ns,last_time;
 uint32_t checksum,bytes;
};
_Static_assert(sizeof(struct begonia_touch_diagnostics)==104,"diagnostic ABI");
_Static_assert(offsetof(struct begonia_touch_diagnostics,checksum)==96,"checksum boundary");
static uint32_t begonia_touch_diag_checksum(const struct begonia_touch_diagnostics *d) {
 const uint8_t *p=(const uint8_t *)d;uint32_t hash=0x811c9dc5;
 for(size_t i=0;i<offsetof(struct begonia_touch_diagnostics,checksum);i++)hash=(hash^p[i])*0x01000193;
 return hash;
}
static inline int begonia_touch_diag_valid(const struct begonia_touch_diagnostics *d) {
 return d->magic==BEG_TOUCH_DIAG_MAGIC && d->version==1 && !(d->sequence&1) &&
        d->bytes==sizeof(*d) && d->checksum==begonia_touch_diag_checksum(d);
}
#endif
