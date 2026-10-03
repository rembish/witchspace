/* Elite Plus title screen, reconstructed from ELITE.EXE (title_loop, 9e80).
 *
 * One ship in object slot 2 turns in front of a red disc and cycles through the title list:
 * it closes in by 80 a frame down to the type's closest distance, holds for 120 frames, backs
 * off by 100 a frame to 5000 and is replaced by the next type. The disc is a jittered circle
 * drawn every frame, so the title steps the main RNG. A frame lasts two timer ticks.
 */
#ifndef EP_TITLE_H
#define EP_TITLE_H

#include "ep_circle.h"
#include "ep_game.h"

#include <stdint.h>

#define EP_TITLE_SLOT 2

typedef struct {
    ep_game g;         /* rng, space, clock, flip; g.render has the ship */
    uint8_t ship_type; /* ds:b1bb */
    uint16_t hold;     /* ds:b25f: frames at the closest point */
    uint8_t list_pos;  /* ds:b261 - b263 */
    uint8_t flash;     /* ds:1b3e: flashing colour step, 0..5 */
    /* output of the last frame */
    ep_circle_buf disc;
} ep_title;

/* The state title_loop sets up before its first frame (9ec8..9f19); rng and clock are the
 * caller's (they depend on what ran before). */
void ep_title_init(ep_title *t);

/* One pass of the loop (9f21..9ffa). Returns 1 when space was pressed (start the game). */
int ep_title_frame(ep_title *t, int space);

#endif
