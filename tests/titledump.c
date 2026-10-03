/* Run title frames from a snapshot, for re/emu/titletest.py.
 * stdin: "rng0..3 type hold pos flash clock flip frames" then the 64 bytes of slot 2 in hex.
 * stdout per frame: state, then the disc spans and the ship primitives drawn. */
#include "ep_title.h"

#include <stdio.h>

int main(void)
{
    static ep_title t;
    unsigned r0, r1, r2, r3, type, hold, pos, flash, frames;
    unsigned long clock, flip;
    if (scanf("%u %u %u %u %u %u %u %u %lu %lu %u", &r0, &r1, &r2, &r3, &type, &hold, &pos, &flash, &clock,
              &flip, &frames) != 11)
        return 1;
    ep_title_init(&t);
    t.mcga = 1;
    t.rng = (ep_rng){ { (uint16_t)r0, (uint16_t)r1, (uint16_t)r2, (uint16_t)r3 } };
    t.ship_type = (uint8_t)type;
    t.hold = (uint16_t)hold;
    t.list_pos = (uint8_t)pos;
    t.flash = (uint8_t)flash;
    t.clock = (uint32_t)clock;
    t.flip = (uint32_t)flip;
    for (int k = 0; k < 64; k++) {
        unsigned b;
        if (scanf("%2x", &b) != 1) return 1;
        t.space.obj[EP_TITLE_SLOT].b[k] = (uint8_t)b;
    }
    for (unsigned f = 0; f < frames; f++) {
        ep_title_frame(&t, 0);
        printf("%u %u %u %u %u %u %u %u %lu %lu ", t.rng.w[0], t.rng.w[1], t.rng.w[2], t.rng.w[3],
               t.ship_type, t.hold, t.list_pos, t.flash, (unsigned long)t.clock, (unsigned long)t.flip);
        for (int k = 0; k < 64; k++) printf("%02x", t.space.obj[EP_TITLE_SLOT].b[k]);
        printf(" |");
        for (int k = 0; k < t.disc.n; k++)
            printf(" %d,%d,%d", t.disc.span[k].x, t.disc.span[k].w, t.disc.span[k].row);
        printf(" |");
        for (int k = 0; k < t.render.nprim; k++) {
            const ep_prim *p = &t.render.prim[k];
            int n = p->kind == EP_PRIM_TRI ? 3 : p->kind == EP_PRIM_QUAD ? 4 : 2;
            printf(" %d:%d", p->kind, p->colour);
            for (int j = 0; j < 2 * n; j++) printf(",%d", p->pt[j]);
        }
        printf("\n");
    }
    return 0;
}
