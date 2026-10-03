/* Elite Plus objects in flight: update_objects (4154) over the whole game state, with the
 * planet, the sun and the scanner, reconstructed from ELITE.EXE. */
#ifndef EP_WORLD_H
#define EP_WORLD_H

#include "ep_game.h"

/* update_objects (4154). Ships drawn go to g->render, planet and sun spans to g->circles,
 * slot numbers drawn (planets/sun first, then ships) to drawn[]; returns their number. */
int ep_world_update(ep_game *g, int drawn[EP_OBJECTS]);

/* 3130: the 3D view cleared (on the page being drawn) and the two sprites at its lower
 * corners put back */
void ep_view_clear(ep_game *g);

/* 301a: the frame is shown (EP_EV_FLIP). The original first waits until two timer ticks
 * after the last flip; that wait is the caller's, the core takes the clock as it is. */
void ep_view_flip(ep_game *g);

#endif
