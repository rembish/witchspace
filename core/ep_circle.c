/* Elite Plus filled circles, reconstructed from ELITE.EXE (see ep_circle.h).
 *
 * Kept close to the machine code: the span table is the original's stack buffer (words, with
 * the four write pointers of 2b4c..2bed), and screen rows are the EGA video offsets the code
 * computes (40 bytes a row, the 3D view starting at 168h).
 */
#include "ep_circle.h"

#include <string.h>

#define VIEW_W    0x130
#define ROW_BYTES 0x28
#define VIEW_TOP  0x168
#define VIEW_END  0x14c8

typedef struct {
    ep_rng *rng;
    uint16_t mask;
    ep_spans *out;
} span_ctx;

static void emit(span_ctx *c, int16_t bx, int16_t cx, int16_t di)
{
    /* 1514: draw (an empty span draws one pixel) */
    if (cx < 0) return;
    if (cx == 0) cx = 1;
    if (c->out->n < EP_MAX_SPANS) {
        ep_span *s = &c->out->span[c->out->n++];
        s->x = bx;
        s->w = cx;
        s->row = (int16_t)((di - VIEW_TOP) / ROW_BYTES);
    }
}

/* [107e]: 1514 directly, or 14b0 (jitter by the RNG, then clip at 1507) with a mask */
static void span(span_ctx *c, int16_t bx, int16_t cx, int16_t di)
{
    if (!c->mask) {
        emit(c, bx, cx, di);
        return;
    }
    uint32_t old = ep_rng_step(c->rng);
    uint16_t ax = (uint16_t)(old >> 16) & c->mask, dx = (uint16_t)old & c->mask;
    bx = (int16_t)(uint16_t)((uint16_t)bx - ax);
    cx = (int16_t)(uint16_t)((uint16_t)cx + ax + dx);
    if (bx < 0) { /* 14ed */
        cx = (int16_t)(cx + bx);
        if (cx <= 0) return;
        bx = 0;
    }
    int16_t room = (int16_t)(VIEW_W - bx - cx); /* 150b */
    if (room <= 0) {
        cx = (int16_t)(cx + room);
        if (cx <= 0) return;
    }
    emit(c, bx, cx, di);
}

/* 2cf4: outline mode clip */
static void outline_span(span_ctx *c, int16_t bx, int16_t cx, int16_t di)
{
    di = (int16_t)(di - ROW_BYTES);
    if (di < VIEW_TOP || di >= VIEW_END) return;
    if (bx < 0) return;
    if ((int16_t)(bx + cx) >= VIEW_W) return;
    span(c, bx, cx, di);
}

void ep_draw_circle(ep_rng *rng, int16_t x, int16_t y, int16_t r, uint16_t mask, int outline, ep_spans *out)
{
    /* 2ab9: reject */
    if ((uint16_t)r >= 0x1f0 || r <= 0) return;
    if ((uint16_t)x >= VIEW_W) { /* 2a8f */
        if (x >= 0 ? (int16_t)(x - r) >= VIEW_W : (int16_t)(x + r) <= 0) return;
    }
    if ((uint16_t)y >= 0x7c) { /* 2aa4 */
        if (y >= 0 ? (int16_t)(y - r) >= 0x7c : (int16_t)(y + r) <= 0) return;
    }
    int16_t ry = (int16_t)(r - (int16_t)((uint16_t)r >> 3));
    span_ctx c = { rng, mask, out };

    /* 2b1d..2bed: midpoint circle into the span table (word offsets into buf) */
    static uint16_t buf[0xf80 / 2];
    memset(buf, 0, sizeof buf);
    int16_t di = 0, si = (int16_t)(r << 2), bx = 0, cx = r;
    int16_t dx = (int16_t)(3 - 2 * r);
    int p9a = 0, p9c = si, p9e = si, pa0 = 2 * si;
#define PUT(p, a, b) (buf[(p) / 2] = (uint16_t)(a), buf[(p) / 2 + 1] = (uint16_t)(b))
    dx = (int16_t)(dx + di + 6); /* entry jumps to 2b7b */
    for (;;) {
        di = (int16_t)(di + 4); /* 2b80 */
        bx++;
        if (si > di) {
            int16_t ax = (int16_t)(x - cx); /* 2b4c */
            p9c -= 4;
            PUT(p9c, ax, cx << 1);
            PUT(p9e, ax, cx << 1);
            p9e += 4;
            if (dx < 0) { /* 2b77 */
                dx = (int16_t)(dx + di + 6);
                continue;
            }
        } else {
            if (si == di) {
                int16_t ax = (int16_t)(x - cx); /* 2b8a */
                p9c -= 4;
                PUT(p9c, ax, cx << 1);
                PUT(p9e, ax, cx << 1);
                p9e += 4;
            }
            break;
        }
        int16_t ax = (int16_t)(x - bx); /* 2bb7 */
        PUT(p9a, ax, bx << 1);
        p9a += 4;
        pa0 -= 4;
        PUT(pa0, ax, bx << 1);
        dx = (int16_t)(dx + di - si + 10);
        si = (int16_t)(si - 4);
        cx--;
    }
#undef PUT

    /* 2bef: draw */
    int16_t row = (int16_t)(y - ry);
    int16_t vdi = (int16_t)(row * ROW_BYTES + VIEW_TOP + 1);
    int bp = 0;
    if (!outline) {
        uint16_t count = (uint16_t)(r * 2);
        do {
            int16_t sx = (int16_t)buf[bp], sw = (int16_t)buf[bp + 1];
            bp += 2;
            if ((uint16_t)row < 0x7c) {
                if ((count & 7) == 3) goto next; /* the dropped eighth row */
                if (sx < 0) {
                    sw = (int16_t)(sw + sx);
                    if (sw <= 0) goto advance;
                    sx = 0;
                }
                int16_t over = (int16_t)(sx + sw - VIEW_W);
                if (over >= 0) {
                    sw = (int16_t)(sw - over);
                    if (sw <= 0) goto advance;
                }
                span(&c, sx, sw, vdi);
            }
        advance:
            vdi = (int16_t)(vdi + ROW_BYTES);
            row++;
        next:;
        } while (--count);
        return;
    }
    /* 2c70: outline: the first row, then per row the left and right edge steps from the row
     * above (ordered unsigned), then the last row again */
    int16_t bottom = (int16_t)(r + y);
    if (bottom >= 0xc8) bottom = 0xc8;
    uint16_t count = (uint16_t)(bottom - y + r);
    outline_span(&c, (int16_t)buf[0], (int16_t)buf[1], vdi);
    bp = 2;
    vdi = (int16_t)(vdi + ROW_BYTES);
    if (!--count) return; /* 2ca8: no closing row */
    do {
        uint16_t a0 = buf[bp], b0 = buf[bp - 2];
        if (a0 >= b0) {
            uint16_t t = a0;
            a0 = b0;
            b0 = t;
        }
        outline_span(&c, (int16_t)a0, (int16_t)(uint16_t)(b0 - a0), vdi);
        a0 = (uint16_t)(buf[bp + 1] + buf[bp]);
        b0 = (uint16_t)(buf[bp - 1] + buf[bp - 2]);
        bp += 2;
        if (a0 >= b0) {
            uint16_t t = a0;
            a0 = b0;
            b0 = t;
        }
        outline_span(&c, (int16_t)a0, (int16_t)(uint16_t)(b0 - a0), vdi);
        vdi = (int16_t)(vdi + ROW_BYTES);
    } while (--count);
    outline_span(&c, (int16_t)buf[bp - 2], (int16_t)buf[bp - 1], vdi);
}
