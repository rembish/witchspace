/* Read ship views (one per line: flags0 angle0..2 cam0..2 flags1e player0..2 extra, decimal)
 * and print the primitives draw_ship emits for each, for re/emu/rendertest.py. The vertex
 * buffer persists from line to line, as in the original. */
#include "ep_render.h"

#include <stdio.h>

int main(void)
{
    static ep_render r;
    int f0, a0, a1, a2, c0, c1, c2, f1e, p0, p1, p2, x;
    while (scanf("%d %d %d %d %d %d %d %d %d %d %d %d", &f0, &a0, &a1, &a2, &c0, &c1, &c2, &f1e, &p0, &p1,
                 &p2, &x) == 12) {
        ep_ship_view v = { (uint8_t)f0,
                           { (uint16_t)a0, (uint16_t)a1, (uint16_t)a2 },
                           { (int16_t)c0, (int16_t)c1, (int16_t)c2 },
                           (uint8_t)f1e,
                           { (uint16_t)p0, (uint16_t)p1, (uint16_t)p2 },
                           (uint16_t)x };
        r.nprim = 0;
        ep_draw_ship(&r, &v);
        for (int k = 0; k < r.nprim; k++) {
            const ep_prim *p = &r.prim[k];
            int n = p->kind == EP_PRIM_TRI ? 3 : p->kind == EP_PRIM_QUAD ? 4 : 2;
            printf("%d:%d", p->kind, p->colour);
            for (int j = 0; j < 2 * n; j++) printf(" %d", p->pt[j]);
            printf(";");
        }
        printf("\n");
    }
    return 0;
}
