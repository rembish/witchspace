/* Elite Plus at the station (and the screens brought up in flight), reconstructed from
 * ELITE.EXE.
 *
 * A screen is drawn once (text and sprite primitives), then idles: the function-key bar and
 * the commands run until one switches screens. The original waits for keys inside some
 * screens (a promotion, the Tribble offer, mission briefings); here those are resumable:
 * the entry returns EP_WAIT_* and ep_station_key carries on with the key pressed.
 */
#ifndef EP_STATION_H
#define EP_STATION_H

#include "ep_game.h"

enum {
    EP_WAIT_NONE = 0, /* the screen is up, idling */
    EP_WAIT_KEY,      /* any key goes on */
    EP_WAIT_YN        /* Y/y or N/n (other keys are ignored) */
};

/* 8bea up to its idle loop (8dac): on arrival the promotion, the Tribble offer and the
 * mission briefings, each maybe waiting for a key; then the commander's status */
int ep_status_screen(ep_game *g);

/* a key for the screen that waits (EP_WAIT_*); returns what it waits for next */
int ep_station_key(ep_game *g, uint8_t key);

/* 8c2d..8da4: the commander's status (current and target systems, fuel, cash, condition,
 * legal status, rating, Tribbles or mission cargo, the ship and its equipment) */
void ep_status_view(ep_game *g);

/* 9d06: the ship and its equipment as sprites */
void ep_status_picture(ep_game *g);

/* 8dbc: the rating's text for a kill count */
uint16_t ep_rating_text(const ep_game *g, uint16_t kills);

#endif
