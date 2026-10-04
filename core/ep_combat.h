/* Elite Plus combat, reconstructed from ELITE.EXE: the player's laser (target choice, damage,
 * kills and their rewards, the beam), enemy fire and damage to the player, collisions and
 * docking, the missile lock; with the flight generator and the cash as text, which they use. */
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

/* ad4f: what killing this ship earns (or costs) */
void ep_kill_reward(ep_game *g, ep_object *o);

/* a3f4: an armed missile (target_note 1) locks onto the ship in the crosshair */
void ep_missile_lock(ep_game *g);

/* 67ab: damage to the player: the fore shield takes it first, then energy; sets ds:76bd
 * (g->f.dead) when the energy runs out. Nothing in the launch tunnel (ds:ae23). */
void ep_damage(ep_game *g, uint16_t amount);

/* 66d6: collisions with the player: ramming, crashing into the station, docking */
void ep_collisions(ep_game *g);

/* ae50: an enemy laser hit the player this frame (ds:7612): its beam, shield and energy */
void ep_enemy_fire(ep_game *g);

#endif
