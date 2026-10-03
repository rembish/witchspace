/* Elite Plus filled circles (planets, the sun), reconstructed from ELITE.EXE.
 *
 * draw_circle (2ab9) builds a midpoint circle as a table of 2r spans, then draws them top to
 * bottom over the 3D view (304 x 124), skipping every eighth row for the 7/8 aspect. With a
 * detail mask (ds:108f) each span first steps the main RNG and is jittered by the masked old
 * RNG words, which gives planets their ragged edge; that makes the number of spans drawn part
 * of the game state. The outline mode (ds:1091) draws only the edges between rows.
 */
#ifndef EP_CIRCLE_H
#define EP_CIRCLE_H

#include "ep_rng.h"

#include <stdint.h>

#define EP_MAX_SPANS 16384

typedef struct {
    int16_t x, w; /* after clipping, w >= 1 */
    int16_t row;  /* 3D view row (screen line 9 + row) */
} ep_span;

typedef struct {
    ep_span span[EP_MAX_SPANS];
    int n;
    uint16_t table[0xf80 / 2]; /* the original's span table (stack buffer at 2b1d) */
} ep_circle_buf;

/* Circle at (x, y) in 3D view pixels, radius r. mask: ds:108f (0, or 1/3/7 for the jitter).
 * outline: ds:1091. mcga: the MCGA span routine (1675, patched in by set_video_mode) makes
 * jittered spans one pixel wider than the EGA/VGA one (14b0); the RNG use is the same.
 * Appends to buf->span; steps rng once per span drawn with a mask. */
void ep_draw_circle(ep_rng *rng, int16_t x, int16_t y, int16_t r, uint16_t mask, int outline, int mcga,
                    ep_circle_buf *buf);

#endif
