/* Elite Plus between systems: flight start, arrival, hyperspace, reconstructed from ELITE.EXE.
 * Notes: re/FLIGHT.md. */
#ifndef EP_TRAVEL_H
#define EP_TRAVEL_H

#include "ep_game.h"

/* 64d0: a fresh flight in the current system: the system seed, every slot cleared, dust,
 * speed and views reset, then (not in witchspace) the sun, planet and station */
void ep_flight_start(ep_game *g);

/* 666b: flight start, then sun, planet and station moved by a random offset and the player
 * turned to face the station */
void ep_new_system(ep_game *g);

/* 7489: the rings of a hyperspace jump start over */
void ep_rings_start(ep_game *g);

/* 7499: one frame of the rings: each waits, then grows and is drawn as an outline circle
 * (which steps the main RNG when ds:108f is set) */
void ep_rings_frame(ep_game *g);

/* 7500: a misjump: stranded in witchspace halfway to the target */
void ep_witchspace(ep_game *g);

/* 753c: after a jump: mission 4's target, the convoy countdown, and the jump count that
 * starts missions 1..6 */
void ep_jump_missions(ep_game *g);

/* 72d8: the hyperspace (or galactic) jump arrives: fuel and legal status, the new current
 * system, maybe a misjump, 50 frames of rings, the new system, missions, the arrival message */
void ep_arrive(ep_game *g);

#endif
