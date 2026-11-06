/* Novatek NT36672A report decoding, independent of the SPI transport.
 * Layout follows Xiaomi NT36672A/nt36xxx.c (six bytes per contact).
 * Input is the 65-byte payload AFTER the SPI address and dummy byte.
 */
#ifndef BEGONIA_TOUCH_REPORT_H
#define BEGONIA_TOUCH_REPORT_H
#include <stdint.h>
#include <stddef.h>
#define BEGONIA_TOUCH_SLOTS 10
struct begonia_contact { uint16_t x, y; uint8_t down; };
struct begonia_touch_state { struct begonia_contact slot[BEGONIA_TOUCH_SLOTS]; };
typedef void (*begonia_touch_emit)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
/* Types match hakios.input.h: DOWN=1, MOVE=2, UP=3. Return a negative reason for
 * malformed/recovery packets; leave previous state intact in that case. */
enum { BEG_TOUCH_INVALID=-1, BEG_TOUCH_RECOVERY=-2, BEG_TOUCH_WATCHDOG=-3,
    BEG_TOUCH_ID=-4, BEG_TOUCH_DUPLICATE=-5, BEG_TOUCH_BOUNDS=-6 };
static int
begonia_touch_decode(struct begonia_touch_state *state, const uint8_t *p,
    size_t size, uint16_t xmax, uint16_t ymax, begonia_touch_emit emit, void *ctx)
{
    struct begonia_touch_state next = {0};
    unsigned i, seen = 0, recover = 1, watchdog = 1;
    if (size != 65 || !xmax || !ymax) return -1;
    for (i = 0; i < 6; i++) {
        if (p[i] != 0x77) recover = 0;
        if (p[i] != 0xfd && p[i] != 0xfe) watchdog = 0;
    }
    if (recover) return BEG_TOUCH_RECOVERY;
    if (watchdog) return BEG_TOUCH_WATCHDOG;
    for (i = 0; i < BEGONIA_TOUCH_SLOTS; i++) {
        const uint8_t *r = p + 6*i;
        unsigned id = r[0] >> 3, status = r[0] & 7;
        uint16_t x, y;
        if (status != 1 && status != 2) continue;
        if (id < 1 || id > BEGONIA_TOUCH_SLOTS) return BEG_TOUCH_ID;
        if (seen & (1u << (id-1))) return BEG_TOUCH_DUPLICATE;
        seen |= 1u << (id-1);
        x = ((uint16_t)r[1] << 4) | (r[3] >> 4);
        y = ((uint16_t)r[2] << 4) | (r[3] & 15);
        if (x > xmax || y > ymax) return BEG_TOUCH_BOUNDS;
        next.slot[id-1] = (struct begonia_contact){x,y,1};
    }
    for (i = 0; i < BEGONIA_TOUCH_SLOTS; i++) {
        struct begonia_contact *a = &state->slot[i], *b = &next.slot[i];
        if (b->down && (!a->down || a->x != b->x || a->y != b->y))
            emit(ctx, a->down ? 2 : 1, i, (uint32_t)b->x*65535/xmax, (uint32_t)b->y*65535/ymax);
        else if (a->down && !b->down)
            emit(ctx, 3, i, (uint32_t)a->x*65535/xmax, (uint32_t)a->y*65535/ymax);
    }
    *state = next;
    return 0;
}
#endif
