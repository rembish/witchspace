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
    EP_CMD_SCREEN = 2,  /* a screen is up (or a dialog waits): the caller idles it */
    EP_CMD_PAUSE = 3,   /* the pause menu is up: ep_pause_idle, then ep_resume */
    EP_CMD_RESUME = 4,  /* the pause menu closed: ep_resume carries on with what it interrupted */
    EP_CMD_TITLE = 5,   /* the game was abandoned: back to the title */
    EP_CMD_QUIT = 6     /* quit to DOS */
};

/* what the pause interrupted */
enum { EP_RESUME_NONE = 0, EP_RESUME_FLIGHT, EP_RESUME_IDLE };

/* 0425: the pause menu (Esc): its title, the options' bar */
int ep_pause_open(ep_game *g);

/* 0480..0490: a pass of the pause menu; EP_CMD_RESUME when space closed it */
int ep_pause_idle(ep_game *g);

/* 0299: set up the bar for the current screen, redraw the icons that changed */
void ep_key_bar(ep_game *g);

/* 03c0: the key latched in g->in.last_key, if any: space, Esc, or a bar key */
int ep_commands(ep_game *g);

/* a1cf (F5): the next view; from another screen, back to the space view (EP_CMD_RESTART) */
int ep_view_command(ep_game *g);

/* a0ed: the hyperspace countdown (arriving at 0) and the escape capsule countdown */
void ep_countdowns(ep_game *g);

/* 6aeb: the escape capsule leaves: the ship stays behind as a hulk, the cargo is lost */
void ep_escape_capsule(ep_game *g);

/* 80fb: the player's missile leaves for the locked target */
void ep_launch_missile(ep_game *g);

/* 6ab2: the energy bomb: every ship on the scanner explodes */
void ep_energy_bomb(ep_game *g);

#endif
