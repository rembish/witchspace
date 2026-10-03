/* The pictures of ELITE.GRF (your own copy of the game: never distributed), the 256-colour set
 * the MCGA mode uses: 139 images, each a width (bit 15: colour 0 is transparent), a height and
 * a byte per pixel, PackBits-packed one after the other (re/NOTES.md, re/tools/grf.py). */
#ifndef GRF_H
#define GRF_H

#include <stdint.h>

#define GRF_IMAGES 139

typedef struct {
    int w, h;
    int transparent; /* colour 0 not drawn */
    uint8_t *px;     /* w x h, row by row */
} grf_image;

/* 1 if the file was read */
int grf_load(const char *path);

/* NULL if there is no such picture (or no file) */
const grf_image *grf_get(int id);

#endif
