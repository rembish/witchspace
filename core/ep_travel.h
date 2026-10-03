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

#endif
