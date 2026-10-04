/* Edge cases the original's own states never reach, from a code audit: each check is a defect
 * that was found and fixed, kept so it stays fixed. Most come from a commander file, which a
 * player can edit (its checksum is easy to remake), so the core must stay inside its own
 * memory whatever the file says; run under the sanitizers they are checked for that too. Needs
 * the game's tables (tests/testdata.c); prints each failed check and exits 1.
 *   ep_edgecheck [one check's name] */
#include "ep_adlib.h"
#include "ep_boot.h"
#include "ep_combat.h"
#include "ep_commander.h"
#include "ep_commands.h"
#include "ep_dsmap.h"
#include "ep_render.h"
#include "ep_ships.h"
#include "ep_sound.h"
#include "ep_station.h"
#include "ep_travel.h"

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

static ep_game g;

/* a new game, docked at the first station with every question answered */
static void docked(void)
{
    memset(&g, 0, sizeof g);
    ep_boot(&g, 2, 2, 0, 0, 0);
    int w = ep_start_game(&g, 1, 2, 3, 4);
    for (int k = 0; k < 20 && w != EP_WAIT_NONE; k++) w = ep_station_key(&g, 'N');
}

/* a key at the station's idle loop, as the frontend gives it (src/main.c, M_IDLE) */
static int idle_key(uint8_t k)
{
    ep_output_begin(&g);
    g.in.last_key = k;
    return ep_station_idle(&g);
}

static int bits(uint8_t v)
{
    int n = 0;
    for (; v; v &= (uint8_t)(v - 1)) n++;
    return n;
}

/* buying a second laser asks which mount: the frontend must get a dialogue to show, so the
 * laser is fitted where the player picks (it was paid for and never fitted) */
static void laser_mount(void)
{
    docked();
    ep_commander_set_cash(&g.cmdr, 1000000);
    ep_cash_text(&g.cmdr);
    g.cmdr.b[EP_CMDR_CURRENT + EP_SYSREC_TECH] = 12;
    ep_output_begin(&g);
    ep_equipment_screen(&g);
    for (int k = 0; k < 4; k++) idle_key(0x50); /* down to the pulse laser */
    idle_key(0xff);
    uint8_t mounts = g.cmdr.b[EP_CMDR_LASERS];
    CHECK(bits(mounts) == 1);
    int r = idle_key(0x9f); /* buy */
    CHECK(r == EP_CMD_SCREEN && g.f.station_step);
    for (int k = 0; k < 10 && g.f.station_step; k++) ep_station_key(&g, 0x0d); /* the first mount offered */
    CHECK(!g.f.station_step && bits(g.cmdr.b[EP_CMDR_LASERS]) == 2);
}

/* a commander's name with no end in its 9 bytes: the save screen's entry takes at most 8 */
static void save_name(void)
{
    docked();
    memset(&g.cmdr.b[EP_CMDR_NAME], 'A', 9);
    uint8_t cargo[0x10];
    memcpy(cargo, &g.cmdr.b[EP_CMDR_CARGO], sizeof cargo);
    uint8_t leave = g.f.leave;
    ep_output_begin(&g);
    int w = ep_save_screen(&g);
    for (int k = 0; k < 40 && w == EP_WAIT_TEXT; k++) w = ep_station_key(&g, 'B');
    CHECK(g.f.entry[1] <= 8 && g.f.leave == leave);
    CHECK(!memcmp(cargo, &g.cmdr.b[EP_CMDR_CARGO], sizeof cargo));
}

/* a tech level of ffh offers nothing, and the list's cursor runs on past its end (as the
 * original's does): buying there does nothing (it wrote past the commander) */
static void empty_equipment(void)
{
    docked();
    ep_commander_set_cash(&g.cmdr, 10000000);
    ep_cash_text(&g.cmdr);
    g.cmdr.b[EP_CMDR_CURRENT + EP_SYSREC_TECH] = 0xff;
    ep_output_begin(&g);
    ep_equipment_screen(&g);
    for (int k = 0; k < 40; k++) idle_key(0x50);
    idle_key(0xff);
    CHECK(g.f.list_count == 0 && g.f.list_row >= 17);
    ep_commander before = g.cmdr;
    idle_key(0x9f);
    idle_key(0xff);
    CHECK(!memcmp(&before, &g.cmdr, sizeof before));
}

/* the rating for 65535 kills: no word in the segment is above it, and the walk ends */
static void rating(void)
{
    docked();
    ep_rating_text(&g, 0xffff);
}

/* a text with no end (a commander's cash text, all letters) is cut short and ends */
static void endless_text(void)
{
    docked();
    for (int i = EP_CMDR_TITLE; i < EP_CMDR_SEED; i++) g.cmdr.b[i] = 'A';
    ep_sync_from_commander(&g);
    uint8_t t[16];
    int n = ep_ds_text(&g, 0x82fb, t, sizeof t);
    CHECK(n < (int)sizeof t && t[sizeof t - 1] == 0 && ep_text_width(t) > 0);
    ep_output_begin(&g);
    ep_equipment_screen(&g); /* the cash line: out of bounds before */
}

/* a government past 7 reads the spawn tables' entries from the data segment beyond them, as
 * the original does, not past the core's arrays (the sanitizers check) */
static void government(void)
{
    docked();
    g.cmdr.b[EP_CMDR_CURRENT + EP_SYSREC_GOVERNMENT] = 0xff;
    g.f.approach = 1;
    ep_flight_start(&g);
    for (int k = 0; k < 50; k++) ep_ai_frame(&g);
    CHECK(g.f.spawn_row == 0xff * 4); /* the rows past the tables were read */
}

/* the music with no song, or one with no time in it: the timer goes on (the title's clock runs
 * from it), the music stops */
static int no_file(void *ctx, const char *name, uint8_t *data, int max)
{
    (void)ctx, (void)name, (void)data, (void)max;
    return -1;
}

static void silent_song(const uint8_t *song, size_t len)
{
    static const ep_io io = { NULL, NULL, no_file, NULL, NULL };
    memset(&g, 0, sizeof g);
    g.io = &io;
    ep_boot(&g, 2, 1, 0, 0, 0);
    memcpy(g.adlib.song, song, len);
    ep_music_start(&g);
    uint32_t clock = g.clock;
    for (int k = 0; k < 5000; k++) ep_pit_tick(&g); /* each would loop for ever before */
    CHECK(g.clock != clock);
}

static void music(void)
{
    silent_song((const uint8_t *)"", 0); /* ADBLUE.MID missing */
    static const uint8_t eot[] = { 'M',  'T', 'h', 'd', 0,   0, 0, 6, 0, 0, 0,    1,    0,
                                   0x60, 'M', 'T', 'r', 'k', 0, 0, 0, 4, 0, 0xff, 0x2f, 0 };
    silent_song(eot, sizeof eot); /* a track with only its end */
}

static const struct {
    const char *name;
    void (*run)(void);
} checks[] = { { "laser_mount", laser_mount },
               { "save_name", save_name },
               { "empty_equipment", empty_equipment },
               { "rating", rating },
               { "endless_text", endless_text },
               { "government", government },
               { "music", music } };

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0); /* each line out before a hang (as these were) */
    for (size_t k = 0; k < sizeof checks / sizeof checks[0]; k++)
        if (argc < 2 || !strcmp(argv[1], checks[k].name)) checks[k].run();
    if (!failed) printf("edge cases: ok\n");
    return failed;
}
