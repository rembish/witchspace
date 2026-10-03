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
    uint8_t hyperspace;     /* ds:83a4: in the hyperspace tunnel, nothing to draw */
    uint16_t approach;      /* ds:83ae: frames left falling towards the planet */
    uint8_t approach_size;  /* ds:83ad: planet size while falling */
    uint8_t planet_size;    /* ds:54c1: apparent size of the planet last frame */
    uint8_t sun_heat;       /* ds:54c3: 2 x (127 - apparent size of the sun), 254 when far */
    uint16_t surface;       /* ds:0aa4: surface activity (random sounds near the planet) */
    uint16_t surface_count; /* ds:0aa6 */
    uint16_t atmosphere;    /* ds:83b5 */
    uint16_t message;       /* ds:8058: message shown (data address of the text) */
    uint16_t message_time;  /* ds:805a */
    uint8_t dead;           /* ds:76bd */
    uint8_t no_crash;       /* ds:ae23 */
    uint8_t scoop_lock;     /* ds:b126 */
    uint8_t video;          /* ds:10bc: 0 EGA, 1 VGA, 2 MCGA */
} ep_flight;

typedef struct {
    ep_commander cmdr; /* ds:82db */
    ep_space space;    /* ds:76de objects, 76b5 count, 76be rotation slots, 76d8 angles ... */
    ep_rng rng;        /* ds:0205 */
    uint32_t clock;    /* ds:45e0: timer ticks */
    uint32_t flip;     /* ds:267c: tick count at the last frame flip */
    ep_render render;  /* its vertex buffer (ds:28e6) carries over between ships */
    ep_flight f;
    /* output of the last update */
    ep_circle_buf circles; /* planet and sun spans */
    ep_event event[EP_MAX_EVENTS];
    int nevents;
} ep_game;

void ep_event_add(ep_game *g, uint8_t kind, uint16_t arg);

#endif
