/* Elite Plus star dust, reconstructed from ELITE.EXE.
 *
 * 30 particles at ds:5314, 7 bytes each (x, y as signed words with the integer part in the
 * high byte, life, a spare byte, colour), and a shadow copy at ds:53e6 (the position before
 * this frame in jump mode, respawn life, "just respawned" flag in the spare byte). The
 * particles move with the speed and the steering according to the view, are respawned by the
 * flight generator when they leave the box, and are drawn as pixels (streaks in jump mode). */
#ifndef EP_DUST_H
#define EP_DUST_H

#include "ep_game.h"

/* 4fa3: one frame of the dust (draws into g->render) */
void ep_dust_frame(ep_game *g);

/* 5374: scatter the dust anew (view change, flight start) */
void ep_dust_reset(ep_game *g);

#endif
