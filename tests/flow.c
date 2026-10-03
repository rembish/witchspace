/* Checks of the core's flow that the difftests cannot reach (the original's timer does not
 * run under the emulator): the title's waits end by the clock as well as by a key. */
#include "ep_sound.h"
#include "ep_station.h"
#include "ep_title.h"

#include <stdio.h>
#include <string.h>

static int failed;

#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #c);                                                   \
            failed = 1;                                                                                      \
        }                                                                                                    \
    } while (0)

static void title_waits(void)
{
    static ep_game g;
    g.space.count = 3;
    g.in.last_key = 0xff;
    g.clock = 5000;
    CHECK(ep_title_open(&g) == EP_WAIT_TIME);
    for (int t = 0; t < 1000; t++) ep_timer_tick(&g);
    CHECK(ep_station_key(&g, 0xff) == EP_WAIT_TIME && g.f.title_step == 1); /* 3b18: not yet past */
    ep_timer_tick(&g);
    CHECK(ep_station_key(&g, 0xff) == EP_WAIT_TIME && g.f.title_step == 2); /* the credits */
    CHECK(g.f.note_ticks == 0x2ee);
    for (int t = 0; t < 0x2ed; t++) ep_timer_tick(&g);
    CHECK(ep_station_key(&g, 0xff) == EP_WAIT_TIME);
    ep_timer_tick(&g);
    CHECK(ep_station_key(&g, 0xff) == EP_WAIT_NONE && g.f.station_step == 0 && g.f.bar_quiet == 0);
}

/* 05e5: a key still held from before is not taken: the keys must all be up first */
static void define_waits_for_release(void)
{
    static ep_game g;
    memset(g.in.key, 0x80, sizeof g.in.key);
    g.in.up = (uint16_t)(0xffff - 0x20d); /* nothing bound: no question first */
    ep_key_event(&g, 0x1c);               /* Enter, held */
    CHECK(ep_define_keys(&g) == EP_WAIT_SCAN);
    CHECK(ep_station_key(&g, 0xff) == EP_WAIT_SCAN && !g.f.define_armed);
    ep_key_event(&g, 0x9c); /* released */
    CHECK(ep_station_key(&g, 0xff) == EP_WAIT_SCAN && g.f.define_armed && g.in.last_scan == 0xffff);
    ep_key_event(&g, 0x48); /* up arrow: the first key, "up" */
    ep_key_event(&g, 0xc8);
    CHECK(ep_station_key(&g, 0xff) == EP_WAIT_SCAN && g.in.up == 0x48 && g.f.define_k == 1);
}

int main(void)
{
    title_waits();
    define_waits_for_release();
    if (!failed) printf("flow: ok\n");
    return failed;
}
