/* Elite Plus objects in space, reconstructed from ELITE.EXE.
 *
 * The object table (ds:76de) is kept as raw 64-byte slots like the original's, with named
 * offsets for the fields that are understood; this keeps the core comparable byte for byte
 * while the rest of the slot is still being worked out.
 *
 * update_objects (4154) rotates every active object into camera space by the player's three
 * rotation slots, marks the ones in range and in view, and draws the ships farthest first.
 */
#ifndef EP_OBJECTS_H
#define EP_OBJECTS_H

#include "ep_render.h"

#include <stdint.h>

#define EP_OBJECTS 40 /* slots up to the original's table end; the count is ds:76b5 */

enum {
    EP_OBJ_FLAGS = 0x00,   /* bit 0 active, 1..5 type, 6 in range, 7 in view */
    EP_OBJ_POS_HI = 0x01,  /* +01/02/03: high bytes of the 24-bit position */
    EP_OBJ_POS = 0x04,     /* +04/06/08: low words of the position relative to the player */
    EP_OBJ_ANGLE = 0x0a,   /* +0a/0c/0e */
    EP_OBJ_CAM = 0x10,     /* +10/12/14: camera-space position */
    EP_OBJ_FLAGS1E = 0x1e, /* bit 1 hit this frame, 5/6 scanner blink, 60h = not drawn */
    EP_OBJ_TIMER = 0x34,   /* counts up to 0 when nonzero (exploding) */
    EP_OBJ_ZHI = 0x3c,     /* high byte of camera z before the extra rotation */
    EP_OBJ_DIST = 0x3e,    /* (x² + y² + z²) >> 22 */
};

typedef struct {
    uint8_t b[64];
} ep_object;

/* A rotation slot (ds:76be + 4 n): sine and cosine of an 11-bit angle. */
typedef struct {
    int16_t sin, cos;
} ep_rot;

typedef struct {
    ep_object obj[EP_OBJECTS];
    uint8_t count;            /* ds:76b5 */
    uint16_t player_angle[3]; /* ds:76d8, 76da, 76dc */
    uint16_t extra_angle;     /* ds:b0de */
    uint8_t in_flight;        /* ds:af18: scanner and compass only in flight */
    ep_rot rot[6];            /* ds:76be.. */
} ep_space;

/* 6d26: slot from an angle (2048 steps per turn) */
ep_rot ep_rot_from_angle(uint16_t angle);

/* 6d83: rotate (a, b) by a slot: a' = a cos - b sin, b' = b cos + a sin, each operand
 * doubled first and each product rounded as the original does. */
void ep_rotate_pair(const ep_rot *r, int16_t *a, int16_t *b);

/* 6e01: rotate a point by the player's slots 0..2 */
void ep_rotate_by_player(const ep_space *s, int16_t p[3]);

/* 6ee8: right shifts that bring the largest |coordinate| (24-bit) below 9400 */
uint8_t ep_planet_scale(const ep_object *o);

/* 433c: planet or sun into camera space: position scaled down by ep_planet_scale (kept in
 * +0a), rotated like any object, flags |= c0 (no range or view test). */
void ep_planet_to_camera(ep_space *s, ep_object *o);

/* 4694: apparent size of an object of the given size (100 planet, 50 sun) from its camera
 * position and scale, 0..255 */
uint16_t ep_apparent_size(const ep_object *o, uint16_t size);

/* update_objects (4154), for ship types (0..29); planets, the sun, the scanner, scooping and
 * explosions are not reconstructed yet. Ships drawn are appended to r. Returns the number of
 * ships drawn, their slot numbers in order in drawn[]. */
int ep_update_objects(ep_space *s, ep_render *r, int drawn[EP_OBJECTS]);

#endif
