/* Elite Plus laser hits, reconstructed from ELITE.EXE: target choice, damage, kills and
 * their rewards, the beam. */
#ifndef EP_COMBAT_H
#define EP_COMBAT_H

#include "ep_game.h"

/* 4f20: the flight generator, a twist of the commander's seed words (ds:830f); returns the
 * old w0 + w1 */
uint16_t ep_flight_random(ep_game *g);

/* 6fca: the commander's cash as text (ds:82fb) */
void ep_cash_text(ep_commander *c);

/* ac52: the laser shot fired this frame (ds:b0e4): hit, damage, kill, beam */
void ep_laser_hits(ep_game *g);

#endif
