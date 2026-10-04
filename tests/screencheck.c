/* What the frontend's renderer puts on screen, checked pixel by pixel where the primitives alone
 * cannot say it: a dust particle of each colour lands as the MCGA pixel of 2989's table
 * (ds:2656, the colour's low 3 bits), not a game colour. Needs the game's data (tests/testdata.c).
 *   ep_screencheck */
#include "screen.h"

#include "ep_render.h"
#include "ep_tables.h"

#include <stdio.h>

int main(void)
{
    static ep_game g;
    screen_init();
    int bad = 0;
    for (int c = 0; c < 16; c++) {
        g.render.nprim = 0;
        ep_render_dust(&g.render, (uint8_t)c, (int16_t)(10 + 3 * c), 20);
        screen_draw(&g);
        uint8_t want = ep_ds_initial[0x2656 + (c & 7)];
        uint8_t got = screen_px[9 + 20][8 + 10 + 3 * c]; /* the view is at 8, 9 on the screen */
        if (got != want) {
            printf("dust colour %d: pixel %d, want %d\n", c, got, want);
            bad++;
        }
    }
    printf("%s\n", bad ? "screen: FAILED" : "screen: dust colours as the original's");
    return bad ? 1 : 0;
}
