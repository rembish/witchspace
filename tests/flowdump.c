/* Drive the core from the title through a new game to the launch, for re/emu/flowtest.py.
 *   ep_flowdump FRAMES HOUR MINUTE SECOND HUNDREDTHS IN OUT
 * IN: the data segment at the title's first frame. Space comes in title frame FRAMES, F1 at the
 * status screen's first pass. Writes the data segment as the core models it to OUT. */
#include "ep_commands.h"
#include "ep_dsmap.h"
#include "ep_sound.h"
#include "ep_station.h"
#include "ep_title.h"

#include <stdio.h>
#include <stdlib.h>

static uint8_t ds[EP_DS_SIZE];

/* the timer runs while the original waits, as machine.py's does */
static void wait(ep_game *g, uint32_t until, int show)
{
    (void)show;
    while (g->clock < until) ep_timer_tick(g);
}

int main(int argc, char **argv)
{
    if (argc != 8) return 2;
    FILE *f = fopen(argv[6], "rb");
    if (!f || fread(ds, 1, sizeof ds, f) != sizeof ds) return 2;
    fclose(f);
    static ep_game g;
    ep_ds_load(&g, ds);
    g.wait = wait;
    int frames = atoi(argv[1]), r = EP_CMD_STAY;
    for (int k = 1; k <= frames; k++) {
        if (k == frames) g.in.last_key = 0x20;
        ep_output_begin(&g);
        r = ep_title_frame(&g);
        if (r != EP_CMD_STAY) break;
    }
    if (r != EP_CMD_START) {
        printf("the title did not start the game (%d)\n", r);
        return 1;
    }
    int w = ep_start_game(&g, (uint8_t)atoi(argv[2]), (uint8_t)atoi(argv[3]), (uint8_t)atoi(argv[4]),
                          (uint8_t)atoi(argv[5]));
    if (w != EP_WAIT_NONE) {
        printf("a dialog at the start (%d)\n", w);
        return 1;
    }
    g.in.last_key = 0x97; /* F1 */
    r = ep_station_idle(&g);
    if (r != EP_CMD_RESTART) printf("F1 at the station gave %d\n", r);
    ep_ds_store(&g, ds);
    f = fopen(argv[7], "wb");
    if (!f || fwrite(ds, 1, sizeof ds, f) != sizeof ds) return 2;
    fclose(f);
    return 0;
}
