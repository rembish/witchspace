/* Checks of the core's flow that the difftests cannot reach (the original's timer does not
 * run under the emulator): the title's waits end by the clock as well as by a key. */
#include "ep_sound.h"
#include "ep_station.h"
#include "ep_title.h"

#include <stdio.h>

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

int main(void)
{
    title_waits();
    if (!failed) printf("flow: ok\n");
    return failed;
}
