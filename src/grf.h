/* The pictures of ELITE.GRF (your own copy of the game: never distributed), the 256-colour set
 * the MCGA mode uses: 139 images, each a width (bit 15: colour 0 is transparent), a height and
 * a byte per pixel, PackBits-packed one after the other (re/NOTES.md, re/tools/grf.py). */
#ifndef GRF_H
#define GRF_H

#include <stddef.h>
#include <stdint.h>

#define GRF_IMAGES 139

typedef struct {
    int w, h;
    int transparent; /* colour 0 not drawn */
    uint8_t *px;     /* w x h, row by row */
} grf_image;

/* 1 if the file was read and every picture decoded; otherwise 0, and the pictures as before */
int grf_load(const char *path);

/* the same from the file's bytes */
int grf_parse(const uint8_t *data, size_t len);

/* NULL if there is no such picture (or no file) */
const grf_image *grf_get(int id);

#endif
