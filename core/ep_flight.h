/* Elite Plus flight loop subsystems (a040..a091), reconstructed from ELITE.EXE. Each works on
 * the whole game state; names follow what they do in the original. */
#ifndef EP_FLIGHT_H
#define EP_FLIGHT_H

#include "ep_game.h"

/* 702a: the warnings (7129), then the message line (ds:8058) and its countdown; when it
 * runs out, the view's name */
void ep_message_tick(ep_game *g);

/* 75d5: fuel leak: countdown ds:83a5, then 51 frames losing 5 fuel each, with a message */
void ep_fuel_leak(ep_game *g);

/* a52f: energy drain while ds:b139 is set */
void ep_energy_drain(ep_game *g);

/* 4f4b: the laser fitted in the current view (0..3), or -1 */
int ep_view_laser(const ep_game *g);

/* 1038: the joystick's roll (AL) and pitch (AH), +-23, from in.joy_* and its centre */
uint16_t ep_joystick_steering(ep_game *g);

/* a183: firing the laser */
void ep_laser_fire(ep_game *g);

/* a0cc: the launch tunnel; returns 1 when it ends (the original returns to the docked
 * screens) */
int ep_tunnel_tick(ep_game *g);

/* a63d: the player's controls: speed, steering (roll, and pitch re-deriving the three
 * attitude angles), the velocity, and moving everything by it */
void ep_controls(ep_game *g);

/* 0f27: the steering from the arrow keys (they build up, and return to centre), as
 * pitch << 8 | roll; joystick and mouse are not reconstructed yet */
uint16_t ep_steering(ep_game *g);

/* a768: the player's velocity from the attitude and speed, when ds:af58 says it changed */
void ep_player_velocity(ep_game *g);

/* a7b1: everything moves by minus the player's velocity */
void ep_player_move(ep_game *g);

/* 1221: Tribbles breed, eat the cargo and, once there are enough, crawl over the screen
 * (sprites 5d/5e into g->render) */
void ep_tribbles_tick(ep_game *g);

/* afa9: mass-locked (the jump drive cannot run): the station's zone, the sun or planet
 * within 16 bits, or a ship on the scanner but rocks; also when there are fewer than 3 slots */
int ep_mass_locked(const ep_game *g);

/* a5ee: the jump drive stays on only at full speed and away from masses */
void ep_jump_drive(ep_game *g);

/* 549f, the dashboard's state: the condition (585c), cooling, recharging and equipment loss
 * (579d), the station zone (6a45), the ECM icon flag; the gauges are the frontend's */
void ep_dashboard_tick(ep_game *g);

#endif
