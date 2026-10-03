/* Elite Plus game state, reconstructed from ELITE.EXE: everything the core keeps between
 * frames. Grows as subsystems are reconstructed; tests/statemap.c lists where each field
 * lives in the original's data segment, so tests can load the original's state and compare. */
#ifndef EP_GAME_H
#define EP_GAME_H

#include "ep_circle.h"
#include "ep_commander.h"
#include "ep_objects.h"
#include "ep_render.h"
#include "ep_rng.h"

#include <stdint.h>

/* Things the original does that the core reports instead of doing: sounds, and code paths
 * not reconstructed yet (so tests notice when a state reaches them). */
enum { EP_EV_SOUND = 1, EP_EV_SURFACE_SOUND, EP_EV_UNPORTED };

typedef struct {
    uint8_t kind;
    uint16_t arg; /* sound id (4c98), size (4e1a), or the original's address */
} ep_event;

#define EP_MAX_EVENTS 64

/* Flight variables (data segment addresses) */
typedef struct {
    uint8_t hyperspace;       /* ds:83a4: in the hyperspace tunnel, nothing to draw */
    uint16_t approach;        /* ds:83ae: frames left falling into the sun */
    uint8_t approach_size;    /* ds:83ad: sun size while falling */
    uint8_t sun_size;         /* ds:54c1: apparent size of the sun last frame (temperature) */
    uint8_t altitude;         /* ds:54c3: 2 x (127 - apparent size of the planet), 254 when far */
    uint16_t surface;         /* ds:0aa4: solar activity (random sounds near the sun) */
    uint16_t surface_count;   /* ds:0aa6 */
    uint16_t atmosphere;      /* ds:83b5 */
    uint16_t message;         /* ds:8058: message shown (data address of the text) */
    uint16_t message_time;    /* ds:805a */
    uint8_t dead;             /* ds:76bd */
    uint8_t no_crash;         /* ds:ae23: launch tunnel frames left (no crashing meanwhile) */
    uint16_t message_shown;   /* ds:8056: last message drawn */
    uint8_t leak_countdown;   /* ds:83a5: frames until a fuel leak starts */
    uint8_t leak;             /* ds:83a6: frames of fuel leak left */
    uint16_t energy;          /* ds:54c8 */
    uint8_t energy_drain;     /* ds:b139 */
    uint8_t laser_temp;       /* ds:54c2 */
    uint8_t laser_hold;       /* ds:b3d3: fire held, waiting for release */
    uint8_t pulse_phase;      /* ds:b125 */
    uint8_t laser_fired;      /* ds:b0e3: type of laser fired this frame */
    uint8_t firing;           /* ds:b0e4 */
    uint8_t warn_time;        /* ds:81f4: frames the current warning still shows */
    uint8_t warn_index;       /* ds:81f5: 0 missile, 1 altitude, 2 temperature, 3 energy */
    uint16_t warn_message;    /* ds:81f2 */
    uint8_t missile_alert;    /* ds:8892 */
    uint16_t speed;           /* ds:af56: 4..48 */
    uint8_t moved;            /* ds:af58: speed or attitude changed: recompute the velocity */
    uint8_t autopilot;        /* ds:af14: docking computer flying */
    uint16_t autopilot_in;    /* ds:af15 */
    int8_t roll, pitch;       /* ds:09d1, 09d2: steering, -23..23 */
    int8_t last_x, last_y;    /* ds:09d3, 09d4: arrow keys last frame */
    int8_t accel_x, accel_y;  /* ds:09d5, 09d6: held arrow keys build up */
    uint16_t steer;           /* ds:09d7: the steering word of this frame */
    uint8_t opt_reverse_stop; /* ds:b134: reversing stops the turn */
    uint8_t opt_self_centre;  /* ds:b135: steering returns to centre */
    uint8_t opt_invert_pitch; /* ds:b136 */
    uint8_t opt_invert_both;  /* ds:b137 */
    uint16_t pitch_angle[2];  /* ds:af4c, af4e: angles found while pitching */
    int16_t velocity[3];      /* ds:af50, af52, af54 */
    uint8_t jump_speed;       /* ds:b0dd: speed x 32 */
    uint8_t scoop_lock;       /* ds:b126 */
    uint8_t video;            /* ds:10bc: 0 EGA, 1 VGA, 2 MCGA */
} ep_flight;

/* Input as the keyboard handler keeps it (ds:020d: per scancode 0 down, 80h up) and the
 * flight key bindings (ds:b251.., pointers into that table in the original; scancodes here) */
typedef struct {
    uint8_t key[128];                                    /* ds:020d */
    uint8_t faster, slower, up, down, left, right, fire; /* ds:b251 .. b25d */
    uint8_t control;                                     /* ds:8f2c: 0 keyboard, 1 joystick, 2 mouse */
} ep_input;

typedef struct {
    ep_commander cmdr; /* ds:82db */
    ep_space space;    /* ds:76de objects, 76b5 count, 76be rotation slots, 76d8 angles ... */
    ep_rng rng;        /* ds:0205 */
    uint32_t clock;    /* ds:45e0: timer ticks */
    uint32_t flip;     /* ds:267c: tick count at the last frame flip */
    ep_render render;  /* its vertex buffer (ds:28e6) carries over between ships */
    ep_flight f;
    ep_input in;
    /* output of the last update */
    ep_circle_buf circles; /* planet and sun spans */
    ep_event event[EP_MAX_EVENTS];
    int nevents;
} ep_game;

void ep_event_add(ep_game *g, uint8_t kind, uint16_t arg);

#endif
