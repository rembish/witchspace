/* Read circles (one per line: x y r mask outline rng0..3, decimal) and print the spans drawn
 * and the RNG state after each, for re/emu/circletest.py. */
#include "ep_circle.h"

#include <stdio.h>

int main(void)
{
    static ep_spans out;
    int x, y, r, mask, outline;
    unsigned w0, w1, w2, w3;
    while (scanf("%d %d %d %d %d %u %u %u %u", &x, &y, &r, &mask, &outline, &w0, &w1, &w2, &w3) == 9) {
        ep_rng rng = { { (uint16_t)w0, (uint16_t)w1, (uint16_t)w2, (uint16_t)w3 } };
        out.n = 0;
        ep_draw_circle(&rng, (int16_t)x, (int16_t)y, (int16_t)r, (uint16_t)mask, outline, &out);
        for (int k = 0; k < out.n; k++) printf("%d,%d,%d ", out.span[k].x, out.span[k].w, out.span[k].row);
        printf("| %u %u %u %u\n", rng.w[0], rng.w[1], rng.w[2], rng.w[3]);
    }
    return 0;
}
