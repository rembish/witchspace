/* Elite Plus main random number generator (ds:0205..020b), reconstructed from ELITE.EXE.
 *
 * Two 32-bit halves A (0205 high, 0207 low) and B (0209, 020b): A' = A + B, B' = A with its
 * words swapped. The game steps it inline in many places (the copy protection question,
 * planet edges, combat); drawing a planet steps it once per span, so rendering is part of
 * the deterministic state.
 */
#ifndef EP_RNG_H
#define EP_RNG_H

#include <stdint.h>

typedef struct {
    uint16_t w[4]; /* ds:0205, 0207, 0209, 020b */
} ep_rng;

/* Initial state before seeding (entry, 0047). */
ep_rng ep_rng_init(void);

/* Seeding at start-up (005f): `steps` steps (0 means 256) from the initial state. */
void ep_rng_seed(ep_rng *r, uint8_t steps);

/* One step; returns the old A (high word in the upper half). */
uint32_t ep_rng_step(ep_rng *r);

#endif
