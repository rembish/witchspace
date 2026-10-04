/* Elite Plus ship rendering, reconstructed from ELITE.EXE.
 *
 * Ships are flat-shaded polyhedra. draw_ship (43ce) builds a rotation matrix from the ship's
 * and the player's angles, draw_model (3c90) transforms and projects the vertices into a
 * persistent vertex buffer, culls face groups by their normal and emits triangles, quads and
 * lines with a colour each. The renderer here produces those primitives; drawing them is up
 * to the frontend. Screen coordinates are the original's 3D view (centre 152, 62).
 *
 * ep_render is also the core's one display list: the scanner, dust, circles, sprites, text and
 * rectangles the other modules draw are appended here as primitives, in the order the
 * original draws them.
 */
#ifndef EP_RENDER_H
#define EP_RENDER_H

#include <stdint.h>

#define EP_MAX_VERTS 64    /* the buffer at ds:28e6 has room for more than any model uses */
#define EP_MAX_PRIMS 32768 /* room for a whole replayed sequence (50 frames of rings) */
#define EP_TEXT_POOL 32767 /* bytes of the strings of the text primitives */

/* Q15 matrix, row-major, as the original's 9-word matrices */
typedef struct {
    int16_t m[9];
} ep_mat;

/* One transformed vertex, the original's 10-byte record: camera X, Y, Z, screen x, y. The
 * buffer persists across calls because a projection overflow leaves fields stale. */
typedef struct {
    int16_t x, y, z, sx, sy;
} ep_vertex;

/* Coordinates: TRI, QUAD, LINE, CLIPPED_LINE, PIXEL and SPANS are in the 3D view (304 x 124,
 * at 8, 9 on the 320 x 200 screen); SPRITE, TEXT and RECT are screen coordinates. */
enum {
    EP_PRIM_TRI = 0,
    EP_PRIM_QUAD = 2,
    EP_PRIM_LINE = 4,
    EP_PRIM_CLIPPED_LINE = 6,
    EP_PRIM_PIXEL = 8,
    EP_PRIM_SPRITE = 10,
    EP_PRIM_TEXT = 12,
    EP_PRIM_RECT = 14,
    EP_PRIM_SPANS = 16, /* a filled circle: pt[0] its first span in g->circles (unsigned), pt[1] how many */
    EP_PRIM_BLIP = 18,  /* a scanner blip (screen): colour the object's type, pt[0] x, pt[1] the
                         * foot's row, pt[2] the stick's height (signed); the head is 2 wide */
    EP_PRIM_DUST = 20   /* a dust particle (view), a pixel whose colour (0..15) is not a game colour:
                         * 2989 takes its low 3 bits into its own table of MCGA pixels (ds:2656) */
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
    uint8_t text[EP_TEXT_POOL]; /* EP_PRIM_TEXT: pt[2] offset, pt[3] length here */
    int ntext;
    int16_t pen_x, pen_y; /* where the next text goes on (bx, cx after 2e6d) */
    uint8_t pen_colour;   /* ds:10a2 */
    uint8_t dl;           /* DL as the drawing leaves it: a sprite its width's low byte (3777),
                           * text its last glyph's last row address (2e52); the AI reads it */
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

/* A circle's spans (ep_circle.h), where it was drawn among the other primitives */
void ep_render_spans(ep_render *r, uint8_t colour, int first, int count);

/* A scanner blip (2995) */
void ep_render_blip(ep_render *r, uint8_t type, int16_t x, int16_t y, int8_t h);

/* A filled quadrilateral (1a7a), four points in order */
void ep_render_quad(ep_render *r, uint8_t colour, const int16_t pt[8]);

/* A line (as 261b gets it: end point first) */
void ep_render_line(ep_render *r, uint8_t colour, int16_t x0, int16_t y0, int16_t x1, int16_t y1);

/* A line clipped to the 3D view when drawn (2576) */
void ep_render_clipped_line(ep_render *r, uint8_t colour, int16_t x0, int16_t y0, int16_t x1, int16_t y1);

/* a dust pixel (2973): colour is the particle's colour byte, one point */
void ep_render_pixel(ep_render *r, uint8_t colour, int16_t x, int16_t y);
/* a dust particle at (x, y): EP_PRIM_DUST */
void ep_render_dust(ep_render *r, uint8_t colour, int16_t x, int16_t y);

/* a sprite (3411): colour is the sprite number, one point */
void ep_render_sprite(ep_render *r, uint8_t sprite, int16_t x, int16_t y);

/* text (2e6d; shadowed: 2ec0): pt = x, y, offset and length in r->text, shadow. The bytes
 * keep the original's codes: 1 c = colour c from here, 2 x y (words) = move to (x, y);
 * characters 20h..7ah are glyphs, others are skipped. */
void ep_render_text(ep_render *r, uint8_t colour, int16_t x, int16_t y, const uint8_t *s, int len,
                    int shadow);

/* 2fd4: a filled rectangle: pt = x, y, width, height */
void ep_render_rect(ep_render *r, uint8_t colour, int16_t x, int16_t y, int16_t w, int16_t h);

/* 2f84: the width of a text in pixels, up to its end or a move (code 2) */
uint16_t ep_text_width(const uint8_t *s);

/* 2e6d: text at the pen, which then stands after it (moves included); 2e5f: the text starts
 * with its own x, y (words) and colour; shadow: 2ec0 (2eb2 with the header) */
void ep_text(ep_render *r, const uint8_t *s, int len, int shadow);
/* 2e5f: s starts with x, y (words) and a colour byte, the pen set from them; then as ep_text */
void ep_text_header(ep_render *r, const uint8_t *s, int len, int shadow);
/* where (and in which colour) the next ep_text goes */
void ep_pen(ep_render *r, int16_t x, int16_t y, uint8_t colour);

/* draw_ship (43ce): the slot's matrix (player angles, extra angle, then the ship's own) and
 * its model at twice the camera position. Nothing for types 30 and 31, a slot with both
 * flags1e bits 5 and 6 set, or a position too far to double. Appends to r->prim. */
void ep_draw_ship(ep_render *r, const ep_ship_view *v);

#endif
