/* Elite Plus main random number generator (see ep_rng.h). */
#include "ep_rng.h"

ep_rng ep_rng_init(void)
{
    ep_rng r = {{0x1234, 0xdfab, 0x5678, 0xf2e7}};
    return r;
}

void ep_rng_seed(ep_rng *r, uint8_t steps)
{
    do ep_rng_step(r);
    while (--steps);
}

uint32_t ep_rng_step(ep_rng *r)
{
    uint16_t ahi = r->w[0], alo = r->w[1];
    uint32_t lo = (uint32_t)alo + r->w[3];
    r->w[1] = (uint16_t)lo;
    r->w[0] = (uint16_t)(ahi + r->w[2] + (lo >> 16));
    r->w[2] = alo;
    r->w[3] = ahi;
    return (uint32_t)ahi << 16 | alo;
}
