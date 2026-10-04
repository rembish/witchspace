/* ELITE.GRF, the 256-colour pictures, for the witchspace frontend's screen (see grf.h). The
 * core reads only the widths (ep_data_grf); the pixels are the frontend's to draw. */
#include "grf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static grf_image images[GRF_IMAGES];

typedef struct {
    const uint8_t *d;
    size_t n, p;
} stream;

static int next(stream *s) { return s->p < s->n ? s->d[s->p++] : 0; }

/* 33a6: n >= 0, n + 1 bytes as they are; n < 0, the next byte 1 - n times (a run may pass
 * the end: the rest is dropped) */
static void unpack(stream *s, uint8_t *out, size_t len)
{
    size_t k = 0;
    while (k < len) {
        int c = next(s);
        if (c < 0x80) {
            for (int i = 0; i <= c; i++) {
                int b = next(s);
                if (k < len) out[k++] = (uint8_t)b;
            }
        } else {
            int b = next(s);
            for (int i = 0; i < 1 - (c - 256); i++)
                if (k < len) out[k++] = (uint8_t)b;
        }
    }
}

int grf_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(d);
        return 0;
    }
    fclose(f);
    /* the header's second entry: the MCGA set (paragraphs, offset, count) */
    uint32_t off = (uint32_t)(d[8 + 2] | d[8 + 3] << 8 | d[8 + 4] << 16 | (uint32_t)d[8 + 5] << 24);
    int count = d[8 + 6] | d[8 + 7] << 8;
    if (count > GRF_IMAGES) count = GRF_IMAGES;
    stream s = { d, (size_t)n, off };
    for (int i = 0; i < count; i++) {
        int lo = next(&s), hi = next(&s), h = next(&s);
        grf_image *im = &images[i];
        im->w = (lo | hi << 8) & 0x7fff;
        im->h = h;
        im->transparent = hi >> 7;
        im->px = calloc((size_t)(im->w * im->h) + 1, 1);
        unpack(&s, im->px, (size_t)(im->w * im->h));
    }
    free(d);
    return 1;
}

const grf_image *grf_get(int id)
{
    if (id < 0 || id >= GRF_IMAGES || !images[id].px) return NULL;
    return &images[id];
}
