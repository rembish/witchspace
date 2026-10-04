/* Elite Plus flight frame (the loop at a040), reconstructed from ELITE.EXE.
 *
 * One frame calls the subsystems in the original's order, all of them in the core: the view
 * cleared (3130), the dashboard's gauges (549f), the crosshair (4f34), the function-key bar
 * (0299) and the commands (03c0, ep_commands.c), and the frame shown (301a), whose wait for
 * the timer is the frontend's (g->wait). Drawing comes out as primitives in g->render.
 */
#ifndef EP_FRAME_H
#define EP_FRAME_H

#include "ep_game.h"

/* a040..a061: flash, missile lock, view cleared, dashboard, dust, objects, enemy fire, laser
 * hits, fuel leak, message, crosshair; then g->f.reg_dl is DL as the drawing left it
 * (g->render.dl), which the AI goes on with (re/FLIGHT.md) */
void ep_frame_before_ai(ep_game *g);

/* a064..a06d: AI, controls, collisions, Tribbles, then the frame shown (301a) */
void ep_frame_from_ai(ep_game *g);

/* 6bc9: the player's ship blows up: debris (and a canister with cargo aboard) flying the
 * way the ship was going */
void ep_death(ep_game *g);

/* what became of the frame */
enum {
    EP_FRAME_NEXT = 0, /* go on flying (the next frame starts at a040) */
    EP_FRAME_DOCKED,   /* docking succeeded (7613) or the escape capsule arrived: the station */
    EP_FRAME_SCREEN,   /* a command put up a screen (EP_CMD_SCREEN): the caller idles it */
    EP_FRAME_OVER,     /* the death sequence is over: back to the title */
    EP_FRAME_PAUSED    /* the pause menu is up (ep_pause_idle), then ep_flight_resume */
};

/* the rest of the frame the pause menu interrupted (EP_FRAME_*) */
int ep_flight_resume(ep_game *g);

/* a040..a0c9: one frame of the flight loop: the key bar, both halves, then the laser, the
 * commands, the jump drive, the countdowns, the tunnel, the energy drain and the death */
int ep_flight_frame(ep_game *g);

#endif
