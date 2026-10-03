/* Elite Plus ship rendering, reconstructed from ELITE.EXE.
 *
 * Ships are flat-shaded polyhedra. draw_ship (43ce) builds a rotation matrix from the ship's
 * and the player's angles, draw_model (3c90) transforms and projects the vertices into a
 * persistent vertex buffer, culls face groups by their normal and emits triangles, quads and
 * lines with a colour each. The renderer here produces those primitives; drawing them is up
 * to the frontend. Screen coordinates are the original's 3D view (centre 152, 62).
 */
#ifndef EP_RENDER_H
#define EP_RENDER_H

#include <stdint.h>

#define EP_MAX_VERTS 64 /* the buffer at ds:28e6 has room for more than any model uses */
#define EP_MAX_PRIMS 128

/* Q15 matrix, row-major, as the original's 9-word matrices */
typedef struct {
    int16_t m[9];
} ep_mat;

/* One transformed vertex, the original's 10-byte record: camera X, Y, Z, screen x, y. The
 * buffer persists across calls because a projection overflow leaves fields stale. */
typedef struct {
    int16_t x, y, z, sx, sy;
} ep_vertex;

enum {
    EP_PRIM_TRI = 0,
    EP_PRIM_QUAD = 2,
    EP_PRIM_LINE = 4,
    EP_PRIM_CLIPPED_LINE = 6,
    EP_PRIM_PIXEL = 8,
    EP_PRIM_SPRITE = 10
};

typedef struct {
    uint8_t kind;   /* EP_PRIM_* */
    uint8_t colour; /* game colour (ds:10a2), mapped to a pixel value by the video mode */
    int16_t pt[8];  /* x, y pairs: 3, 4 or 2 points */
} ep_prim;

typedef struct {
    ep_vertex vtx[EP_MAX_VERTS];
    ep_prim prim[EP_MAX_PRIMS];
    int nprim;
} ep_render;

/* The parts of an object slot (ds:76de + 64 n) and the globals draw_ship reads. */
typedef struct {
    uint8_t flags0;           /* +00: bit 0 active, bits 1..5 type */
    uint16_t angle[3];        /* +0a, +0c, +0e */
    int16_t cam[3];           /* +10, +12, +14: position relative to the player, rotated */
    uint8_t flags1e;          /* +1e */
    uint16_t player_angle[3]; /* ds:76d8, 76da, 76dc */
    uint16_t extra_angle;     /* ds:b0de */
} ep_ship_view;

/* Q15 product as the original takes it: high word of the 32-bit product, shifted left once. */
int16_t ep_qmul(int16_t a, int16_t b);

void ep_mat_rot_x(uint16_t angle, ep_mat *m);                   /* 3f4d */
void ep_mat_rot_y(uint16_t angle, ep_mat *m);                   /* 3f99 */
void ep_mat_rot_z(uint16_t angle, ep_mat *m);                   /* 3fe5 */
void ep_mat_mul(const ep_mat *a, const ep_mat *b, ep_mat *out); /* 4031: out = a b */

/* draw_model (3c90): model `type` at camera position pos with orientation m. Appends to
 * r->prim. */
void ep_draw_model(ep_render *r, int type, const int16_t pos[3], const ep_mat *m);

/* A line (as 261b gets it: end point first) */
void ep_render_line(ep_render *r, uint8_t colour, int16_t x0, int16_t y0, int16_t x1, int16_t y1);

/* A line clipped to the 3D view when drawn (2576) */
void ep_render_clipped_line(ep_render *r, uint8_t colour, int16_t x0, int16_t y0, int16_t x1, int16_t y1);

/* draw_ship (43ce). Appends to r->prim. */
/* a dust pixel (2973): colour is the particle's colour byte, one point */
void ep_render_pixel(ep_render *r, uint8_t colour, int16_t x, int16_t y);

/* a sprite (3411): colour is the sprite number, one point */
void ep_render_sprite(ep_render *r, uint8_t sprite, int16_t x, int16_t y);

void ep_draw_ship(ep_render *r, const ep_ship_view *v);

#endif
