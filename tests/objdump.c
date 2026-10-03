/* Read object tables (one per line: count, player angles 0..2, extra angle, then count x 64
 * bytes in hex) and print, for each, the slots drawn, the primitives and the slot bytes after
 * update_objects, for re/emu/objtest.py. */
#include "ep_objects.h"

#include <stdio.h>

int main(void)
{
    static ep_space s;
    static ep_render r;
    unsigned count, p0, p1, p2, x;
    while (scanf("%u %u %u %u %u", &count, &p0, &p1, &p2, &x) == 5) {
        s.count = (uint8_t)count;
        s.player_angle[0] = (uint16_t)p0;
        s.player_angle[1] = (uint16_t)p1;
        s.player_angle[2] = (uint16_t)p2;
        s.extra_angle = (uint16_t)x;
        for (unsigned i = 0; i < count; i++)
            for (int k = 0; k < 64; k++) {
                unsigned b;
                if (scanf("%2x", &b) != 1) return 1;
                s.obj[i].b[k] = (uint8_t)b;
            }
        int drawn[EP_OBJECTS];
        r.nprim = 0;
        int nd = ep_update_objects(&s, &r, drawn);
        printf("drawn");
        for (int k = 0; k < nd; k++) printf(" %d", drawn[k]);
        printf(" |");
        for (int k = 0; k < r.nprim; k++) {
            const ep_prim *p = &r.prim[k];
            int n = p->kind == EP_PRIM_TRI ? 3 : p->kind == EP_PRIM_QUAD ? 4 : 2;
            printf(" %d:%d", p->kind, p->colour);
            for (int j = 0; j < 2 * n; j++) printf(",%d", p->pt[j]);
        }
        printf(" |");
        for (unsigned i = 0; i < count; i++) {
            printf(" ");
            for (int k = 0; k < 64; k++) printf("%02x", s.obj[i].b[k]);
        }
        printf("\n");
    }
    return 0;
}
