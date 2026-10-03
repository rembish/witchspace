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

/* 67ab: damage to the player (shields, then energy; dying at 0) */
/* ad4f: what killing this ship earns (or costs) */
void ep_kill_reward(ep_game *g, ep_object *o);

/* a3f4: an armed missile (target_note 1) locks onto the ship in the crosshair */
void ep_missile_lock(ep_game *g);

void ep_damage(ep_game *g, uint16_t amount);

/* 66d6: collisions with the player: ramming, crashing into the station, docking */
void ep_collisions(ep_game *g);

/* ae50: an enemy laser hit the player this frame (ds:7612): its beam, shield and energy */
void ep_enemy_fire(ep_game *g);

#endif
