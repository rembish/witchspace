/* Elite Plus title screen, reconstructed from ELITE.EXE (9e80..a004).
 *
 * ep_title_open (9e9a) puts the title up: the objects cleared, the title music, the intro
 * picture (3ae5: sprite 89h until a key or 1000 ticks), the cockpit, the header, the ship in
 * slot 2, then the credits over it (af73: until a key or 750 ticks). Both waits return
 * EP_WAIT_TIME and go on through ep_station_key (ffh: no key, the clock looked at).
 *
 * ep_title_frame (9f21..9ffa) is a pass: one ship turns in front of a red disc and the title
 * list cycles: it closes in by 80 a frame down to the type's closest distance, holds for 120
 * frames, backs off by 100 a frame to 5000 and is replaced by the next type. The disc is a
 * jittered circle, so the title steps the main RNG. The commands are read as in flight; space
 * starts the game (EP_CMD_START: ep_start_game). The frame wait before the flip (301a: two
 * ticks after the last one) is the caller's.
 */
#ifndef EP_TITLE_H
#define EP_TITLE_H

#include "ep_game.h"

#include <stdint.h>

/* the object slot the title's ship turns in */
#define EP_TITLE_SLOT 2

/* 9e9a: the intro picture up and the music started; returns EP_WAIT_TIME (the rest of the
 * opening goes on through ep_station_key) */
int ep_title_open(ep_game *g);

/* the waits of ep_title_open, through ep_station_key */
int ep_title_key(ep_game *g, uint8_t key);

/* one pass (9f21..9ffa); returns EP_CMD_START when space was pressed, EP_CMD_QUIT once the
 * exit question was answered Y (f.leave 2), otherwise the commands' EP_CMD_* */
int ep_title_frame(ep_game *g);

#endif
