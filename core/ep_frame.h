/* Elite Plus flight frame (the loop at a040), reconstructed from ELITE.EXE.
 *
 * One frame calls the subsystems in the original's order. The view clearing, gauges,
 * crosshair and frame wait are the frontend's; the function-key bar (0299) and the commands
 * (03c0) come with the docked screens.
 */
#ifndef EP_FRAME_H
#define EP_FRAME_H

#include "ep_game.h"

/* a040..a061: flash, missile lock, dashboard, dust, objects, enemy fire, laser hits, fuel
 * leak, message, then DL as the crosshair leaves it (10h when this view has a laser; else
 * the last value tracked, an approximation: the original's comes from the drawing code) */
void ep_frame_before_ai(ep_game *g);

/* a064..a06d: AI, controls, collisions, Tribbles */
void ep_frame_from_ai(ep_game *g);

/* both halves (a040..a070, up to the docking test) */
void ep_flight_frame(ep_game *g);

#endif
