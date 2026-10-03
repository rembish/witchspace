/* The original's screen, drawn from the core's output: a 320 x 200 picture of MCGA pixel
 * values (the 256-colour mode, ds:10bc = 2) and the palette it is shown with. The core says
 * what is drawn (primitives in the order drawn, events among them); this draws it as the
 * original's routines would, one pixel at a time. */
#ifndef SCREEN_H_INCLUDED
#define SCREEN_H_INCLUDED

#include "ep_game.h"

#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 200

extern uint8_t screen_px[SCREEN_H][SCREEN_W];

/* the colour tables as the MCGA mode sets them (384f) */
void screen_init(void);

/* draw the output the core has added since the last ep_output_begin */
void screen_draw(const ep_game *g);

/* the picture as 0xAARRGGBB, with the palette loaded last */
void screen_rgba(uint32_t *out);

#endif
