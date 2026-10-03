/* Elite Plus commands: the function-key bar and what its keys do, reconstructed from ELITE.EXE.
 *
 * Twelve keys (F1..F12, or 1..9, 0, -, =) each run the command whose id the bar shows in that
 * slot. Every frame the current screen fills in the ids it wants (in flight: by the equipment
 * fitted), and slots that changed get their icon redrawn (EP_EV_ICON).
 */
#ifndef EP_COMMANDS_H
#define EP_COMMANDS_H

#include "ep_game.h"

/* what a command did to the frame loop */
enum {
    EP_CMD_STAY = 0,    /* carry on with this frame */
    EP_CMD_RESTART = 1, /* back to the top of the flight loop (a040): the rest of the frame is skipped */
    EP_CMD_SCREEN = 2   /* a screen that is not reconstructed yet (EP_EV_UNPORTED says which) */
};

/* 0299: set up the bar for the current screen, redraw the icons that changed */
void ep_key_bar(ep_game *g);

/* 03c0: the key latched in g->in.last_key, if any: space, Esc, or a bar key */
int ep_commands(ep_game *g);

/* a0ed: the hyperspace countdown (arriving at 0) and the escape capsule countdown */
void ep_countdowns(ep_game *g);

/* 6aeb: the escape capsule leaves: the ship stays behind as a hulk, the cargo is lost */
void ep_escape_capsule(ep_game *g);

/* 80fb: the player's missile leaves for the locked target */
void ep_launch_missile(ep_game *g);

/* 6ab2: the energy bomb: every ship on the scanner explodes */
void ep_energy_bomb(ep_game *g);

#endif
