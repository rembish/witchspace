/* Run title frames from a snapshot, for re/emu/titletest.py.
 * stdin: "rng0..3 type hold pos flash clock flip frames" then the 64 bytes of slot 2 in hex.
 * stdout per frame: state, then the disc spans and the ship primitives drawn. */
#include "ep_title.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    static ep_game g;
    unsigned r0, r1, r2, r3, type, hold, pos, flash, frames;
    unsigned long clock, flip;
    if (scanf("%u %u %u %u %u %u %u %u %lu %lu %u", &r0, &r1, &r2, &r3, &type, &hold, &pos, &flash, &clock,
              &flip, &frames) != 11)
        return 1;
    g.space.count = 3;
    g.in.last_key = 0xff;
    g.f.video = 2;
    g.rng = (ep_rng){ { (uint16_t)r0, (uint16_t)r1, (uint16_t)r2, (uint16_t)r3 } };
    g.f.title_ship = (uint8_t)type;
    g.f.title_hold = (uint16_t)hold;
    g.f.title_list = (uint16_t)(0xb263 + pos);
    g.f.flash = (uint8_t)flash;
    g.clock = (uint32_t)clock;
    g.flip = (uint32_t)flip;
    for (int k = 0; k < 64; k++) {
        unsigned b;
        if (scanf("%2x", &b) != 1) return 1;
        g.space.obj[EP_TITLE_SLOT].b[k] = (uint8_t)b;
    }
    for (unsigned f = 0; f < frames; f++) {
        g.render.nprim = 0;
        g.render.ntext = 0;
        g.circles.n = 0;
        g.nevents = 0;
        if (g.clock < g.flip + 2) g.clock = g.flip + 2; /* 301a waits two ticks after the last flip */
        ep_title_frame(&g);
        printf("%u %u %u %u %u %u %u %u %lu %lu ", g.rng.w[0], g.rng.w[1], g.rng.w[2], g.rng.w[3],
               g.f.title_ship, g.f.title_hold, g.f.title_list - 0xb263, g.f.flash, (unsigned long)g.clock,
               (unsigned long)g.flip);
        for (int k = 0; k < 64; k++) printf("%02x", g.space.obj[EP_TITLE_SLOT].b[k]);
        printf(" |");
        for (int k = 0; k < g.circles.n; k++)
            printf(" %d,%d,%d", g.circles.span[k].x, g.circles.span[k].w, g.circles.span[k].row);
        printf(" |");
        for (int k = 0; k < g.render.nprim; k++) { /* the ship's */
            const ep_prim *p = &g.render.prim[k];
            if (p->kind != EP_PRIM_TRI && p->kind != EP_PRIM_QUAD && p->kind != EP_PRIM_LINE) continue;
            int n = p->kind == EP_PRIM_TRI ? 3 : p->kind == EP_PRIM_QUAD ? 4 : 2;
            printf(" %d:%d", p->kind, p->colour);
            for (int j = 0; j < 2 * n; j++) printf(",%d", p->pt[j]);
        }
        printf("\n");
    }
    return 0;
}
