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
    int short_of_data; /* the stream ended before the pictures did */
} stream;

static int next(stream *s)
{
    if (s->p < s->n) return s->d[s->p++];
    s->short_of_data = 1;
    return 0;
}

/* 33a6: n >= 0, n + 1 bytes as they are; n < 0, the next byte 1 - n times (a run may pass
 * the end: the rest is dropped) */
static void unpack(stream *s, uint8_t *out, size_t len)
{
    size_t k = 0;
    while (k < len && !s->short_of_data) {
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

static void free_images(grf_image *set)
{
    for (int i = 0; i < GRF_IMAGES; i++) free(set[i].px);
    memset(set, 0, sizeof(grf_image) * GRF_IMAGES);
}

int grf_parse(const uint8_t *d, size_t n)
{
    /* the header's second entry: the MCGA set (paragraphs, offset, count) */
    if (n < 16) return 0;
    uint32_t off = (uint32_t)d[10] | (uint32_t)d[11] << 8 | (uint32_t)d[12] << 16 | (uint32_t)d[13] << 24;
    int count = d[14] | d[15] << 8;
    if (off >= n || count <= 0) return 0;
    if (count > GRF_IMAGES) count = GRF_IMAGES;
    static grf_image fresh[GRF_IMAGES];
    stream s = { d, n, off, 0 };
    for (int i = 0; i < count; i++) {
        int lo = next(&s), hi = next(&s), h = next(&s);
        grf_image *im = &fresh[i];
        im->w = (lo | hi << 8) & 0x7fff;
        im->h = h;
        im->transparent = hi >> 7;
        im->px = calloc((size_t)(im->w * im->h) + 1, 1);
        if (!im->px) {
            free_images(fresh);
            return 0;
        }
        unpack(&s, im->px, (size_t)(im->w * im->h));
        if (s.short_of_data) { /* a truncated file: keep what was there before */
            free_images(fresh);
            return 0;
        }
    }
    free_images(images);
    memcpy(images, fresh, sizeof images);
    memset(fresh, 0, sizeof fresh);
    return 1;
}

int grf_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    long n = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    if (n <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    uint8_t *d = malloc((size_t)n);
    int ok = d && fread(d, 1, (size_t)n, f) == (size_t)n && grf_parse(d, (size_t)n);
    fclose(f);
    free(d);
    return ok;
}

const grf_image *grf_get(int id)
{
    if (id < 0 || id >= GRF_IMAGES || !images[id].px) return NULL;
    return &images[id];
}
