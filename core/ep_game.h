/* Elite Plus game state, reconstructed from ELITE.EXE: everything the core keeps between
 * frames. Grows as subsystems are reconstructed; tests/statemap.c lists where each field
 * lives in the original's data segment, so tests can load the original's state and compare. */
#ifndef EP_GAME_H
#define EP_GAME_H

#include "ep_commander.h"
#include "ep_objects.h"
#include "ep_render.h"
#include "ep_rng.h"

#include <stdint.h>

typedef struct {
    ep_commander cmdr; /* ds:82db */
    ep_space space;    /* ds:76de objects, 76b5 count, 76be rotation slots, 76d8 angles ... */
    ep_rng rng;        /* ds:0205 */
    uint32_t clock;    /* ds:45e0: timer ticks */
    uint32_t flip;     /* ds:267c: tick count at the last frame flip */
    ep_render render;  /* its vertex buffer (ds:28e6) carries over between ships */
} ep_game;

#endif
