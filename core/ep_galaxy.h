/* Elite Plus galaxy generation, reconstructed from ELITE.EXE.
 *
 * Every system is derived from three 16-bit seed words. A galaxy starts at its seed from the
 * game's table and steps to the next system with four "twists" (a Fibonacci-like update).
 * The derived values are the same bytes the original stores at ds:8345..8354; display code
 * turns them into text (economy and government names, tech level + 1, population / 10 etc.).
 */
#ifndef EP_GALAXY_H
#define EP_GALAXY_H

#include <stdint.h>

#define EP_GALAXIES 8
#define EP_SYSTEMS  256
#define EP_NAME_MAX 8

typedef struct {
    uint16_t w[3]; /* ds:5503, 5505, 5507 */
} ep_seed;

typedef struct {
    uint8_t x, y;          /* chart position: seed byte 3, seed byte 1 / 2 (as the chart uses) */
    uint8_t government;    /* ds:8345, 0 Anarchy .. 7 Corporate State */
    uint8_t economy;       /* ds:8346, 0 Poor Agricultural .. 7 Rich Industrial */
    uint8_t tech;          /* ds:8347, shown as tech + 1 */
    uint8_t population;    /* ds:8348, tenths of a billion */
    uint8_t species[4];    /* ds:8349..834c, species[0] = 0xff for Human Colonials */
    uint16_t productivity; /* ds:834d, M CR */
    uint16_t radius;       /* ds:834f, km */
    uint16_t desc_seed[2]; /* ds:8351 = w0 ^ w1, ds:8353 = w0 ^ w1 ^ w2 */
    char name[EP_NAME_MAX + 1];
} ep_system;

/* Advance a seed by one twist (5e0f). Four twists step to the next system. */
void ep_twist(ep_seed *s);

/* Seed of system 0 of a galaxy (5e25). */
ep_seed ep_galaxy_seed(int galaxy);

/* Seed of system n of a galaxy (610e). */
ep_seed ep_system_seed(int galaxy, int n);

/* The original's name buffer ds:8338..8341: up to 8 letters, a terminator, and the length in
 * the last byte. Kept as raw bytes because some text codes read it with stale contents. */
#define EP_NAMEBUF 10

/* planet_name (6130) on the raw buffer: two to four digrams. Twists *s four times. */
void ep_planet_name(ep_seed *s, uint8_t nb[EP_NAMEBUF]);

/* Name of the system with seed s, generated into a zeroed buffer. Twists *s four times. */
void ep_system_name(ep_seed *s, char out[EP_NAME_MAX + 1]);

/* Everything about the system with seed s (5f00..5fe0, then the name). */
void ep_system_data(const ep_seed *s, ep_system *out);

#endif
