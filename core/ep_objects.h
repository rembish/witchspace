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

#define EP_OBJECTS 36 /* ds:76de..7fdd; the count in use is ds:76b5 (36 in flight, 3 on the title) */

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
    uint8_t ship_slots;       /* ds:7fde: slots 2.. for ships and the station */
    uint8_t debris_slots;     /* ds:7fdf: slots 20.. for debris */
    ep_rot rot[6];            /* ds:76be.. */
    /* the scanner blip the last ep_object_rotate drew (2995): its type, x, the row of its
     * foot and the height of its stick (screen pixels, up when negative) */
    struct {
        uint8_t drawn, type;
        int16_t x, y;
        int8_t h;
    } blip;
} ep_space;

/* 6d26: slot from an angle (2048 steps per turn) */
ep_rot ep_rot_from_angle(uint16_t angle);

/* 6d83: rotate (a, b) by a slot: a' = a cos - b sin, b' = b cos + a sin, each operand
 * doubled first and each product rounded as the original does. */
void ep_rotate_pair(const ep_rot *r, int16_t *a, int16_t *b);

/* 6e01: rotate a point by the player's slots 0..2 */
void ep_rotate_by_player(const ep_space *s, int16_t p[3]);

/* 6e1c: angle (2048 per turn) of the direction (x, y), as the original's arctangent */
uint16_t ep_atan2(int16_t x, int16_t y);

/* 6ee8: right shifts that bring the largest |coordinate| (24-bit) below 9400 */
uint8_t ep_planet_scale(const ep_object *o);

/* 433c: planet or sun into camera space: position scaled down by ep_planet_scale (kept in
 * +0a), rotated like any object, flags |= c0 (no range or view test). */
void ep_planet_to_camera(ep_space *s, ep_object *o);

/* 4694: apparent size of an object of the given size (100 planet, 50 sun) from its camera
 * position and scale, 0..255 */
uint16_t ep_apparent_size(const ep_object *o, uint16_t size);

/* 4264: in range (24-bit position fits 16 bits, each |coordinate| < 12000, squared distance
 * below 0895h << 16); sets +3e and flag bit 6 */
int ep_object_in_range(ep_object *o);

/* 4317: rotate a position into camera space (+3c = high byte of z), the scanner (in flight),
 * then the extra rotation by -ds:b0de */
void ep_object_rotate(ep_space *s, ep_object *o, int16_t p[3]);

/* The renderer's view of a ship slot */
ep_ship_view ep_ship_view_of(const ep_space *s, const ep_object *o);

uint16_t ep_abs16(uint16_t v);

#endif
