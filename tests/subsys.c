/* Run one reconstructed subsystem on a state from the original, for re/emu/subtest.py.
 *   subsys mask OUT            write the mask of data-segment bytes the core models
 *   subsys NAME IN OUT         load IN (64 KB data segment), run NAME, store into OUT;
 *                              primitives drawn go to stdout, one per line */
#include "statemap.h"

#include "ep_combat.h"
#include "ep_dust.h"
#include "ep_travel.h"
#include "ep_chart.h"
#include "ep_frame.h"
#include "ep_commands.h"
#include "ep_station.h"
#include "ep_sound.h"
#include "ep_boot.h"
#include "ep_title.h"
#include "ep_flight.h"
#include "ep_ships.h"
#include "ep_trade.h"
#include "ep_world.h"

#include <stdio.h>
#include <string.h>

static uint8_t ds[DS_SIZE];

static void print_prims(const ep_render *r)
{
    for (int k = 0; k < r->nprim; k++) {
        const ep_prim *p = &r->prim[k];
        if (p->kind == EP_PRIM_SPANS) continue; /* printed from g->circles */
        if (p->kind == EP_PRIM_RECT) {
            printf("rect %d:%d,%d,%d,%d\n", p->colour, p->pt[0], p->pt[1], p->pt[2], p->pt[3]);
            continue;
        }
        if (p->kind == EP_PRIM_TEXT) {
            printf("text %d,%d,%d,%d:", p->pt[0], p->pt[1], p->colour, p->pt[4]);
            for (int j = 0; j < p->pt[3]; j++) printf("%02x", r->text[p->pt[2] + j]);
            printf("\n");
            continue;
        }
        int n = p->kind == EP_PRIM_TRI                                  ? 3
                : p->kind == EP_PRIM_QUAD                               ? 4
                : p->kind == EP_PRIM_PIXEL || p->kind == EP_PRIM_SPRITE ? 1
                                                                        : 2;
        printf("%d:%d", p->kind, p->colour);
        for (int j = 0; j < 2 * n; j++) printf(",%d", p->pt[j]);
        printf("\n");
    }
}

/* The fake disk of re/emu/subtest.py (ds:fc00): a count, then 16 bytes a file: its name, its
 * kind, and a byte that varies the commander in the state into the file's */
enum { DISK = 0xfc00, FILE_GOOD = 0, FILE_BAD_SUM, FILE_SHORT, FILE_NO_OPEN, FILE_READ_ONLY };
static ep_commander disk_base;
static char written[64][8 + 2 * EP_COMMANDER_SIZE + 16];
static int nwritten;

static int disk_find(const char *name)
{
    for (int j = 0; j < ds[DISK]; j++)
        if (!strcmp((const char *)&ds[DISK + 0x10 + 0x10 * j], name)) return j;
    return -1;
}

static int disk_exists(void *ctx, const char *name)
{
    (void)ctx;
    int j = disk_find(name); /* 3d00: it opens */
    return j >= 0 && ds[DISK + 0x10 + 0x10 * j + 13] != FILE_NO_OPEN;
}

static int disk_read(void *ctx, const char *name, uint8_t *data, int max)
{
    (void)ctx;
    int j = disk_find(name);
    if (j < 0) return -1;
    const uint8_t *e = &ds[DISK + 0x10 + 0x10 * j];
    if (e[13] == FILE_NO_OPEN) return -1;
    ep_commander c = disk_base;
    c.b[EP_CMDR_FUEL] = e[14];
    c.b[EP_CMDR_CASH] ^= e[14];
    ep_commander_seal(&c);
    if (e[13] == FILE_BAD_SUM) c.b[EP_CMDR_CHECKSUM] ^= 1;
    int n = e[13] == FILE_SHORT ? 100 : EP_COMMANDER_SIZE;
    if (n > max) n = max;
    memcpy(data, c.b, (size_t)n);
    return n;
}

static int disk_write(void *ctx, const char *name, const uint8_t *data, int len)
{
    (void)ctx;
    int j = disk_find(name);
    if (j >= 0 && ds[DISK + 0x10 + 0x10 * j + 13] == FILE_READ_ONLY) return -1;
    if (nwritten < 64) {
        char *w = written[nwritten++];
        int n = sprintf(w, "write %s:", name);
        for (int k = 0; k < len; k++) n += sprintf(w + n, "%02x", data[k]);
    }
    return len;
}

static int disk_list(void *ctx, char names[][13], int max)
{
    (void)ctx;
    int n = 0;
    for (int j = 0; j < ds[DISK] && n < max; j++) memcpy(names[n++], &ds[DISK + 0x10 + 0x10 * j], 13);
    return n;
}

static const ep_io disk = { NULL, disk_exists, disk_read, disk_write, disk_list };

/* a dialog given the scripted keys (ds:ff10, 32 of them) until it is done */
static void dialog_keys(ep_game *g, int w)
{
    for (int k = 0; w != EP_WAIT_NONE && k < 32; k++) w = ep_station_key(g, ds[0xff10 + k]);
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "mask")) {
        state_mask(ds);
    } else if (argc == 4) {
        FILE *f = fopen(argv[2], "rb");
        if (!f || fread(ds, 1, DS_SIZE, f) != DS_SIZE) return 2;
        fclose(f);
        static ep_game g;
        state_load(&g, ds);
        g.render.nprim = 0;
        g.render.ntext = 0;
        g.circles.n = 0;
        g.nevents = 0;
        if (!strcmp(argv[1], "update_objects")) {
            int drawn[EP_OBJECTS];
            ep_world_update(&g, drawn);
            print_prims(&g.render);
            for (int k = 0; k < g.circles.n; k++)
                printf("span %d,%d,%d\n", g.circles.span[k].x, g.circles.span[k].w, g.circles.span[k].row);

        } else if (!strcmp(argv[1], "message")) {
            ep_message_tick(&g);
            print_prims(&g.render);
        } else if (!strcmp(argv[1], "fuel_leak")) {
            ep_fuel_leak(&g);
        } else if (!strcmp(argv[1], "energy_drain")) {
            ep_energy_drain(&g);
        } else if (!strcmp(argv[1], "laser")) {
            ep_laser_fire(&g);
        } else if (!strcmp(argv[1], "laser_hits")) {
            ep_laser_hits(&g);
            print_prims(&g.render);
        } else if (!strcmp(argv[1], "dust")) {
            ep_dust_frame(&g);
            print_prims(&g.render);
        } else if (!strcmp(argv[1], "key_bar")) {
            ep_key_bar(&g);
        } else if (!strcmp(argv[1], "commands")) {
            printf("cmd %d\n", ep_commands(&g));
        } else if (!strcmp(argv[1], "countdowns")) {
            ep_countdowns(&g);
        } else if (!strcmp(argv[1], "market_session")) { /* the screen, then 12 passes, a key each */
            ep_market_screen(&g);
            for (int k = 0; k < 12; k++) {
                g.in.last_key = ds[0xff10 + k];
                ep_station_idle(&g);
            }
            print_prims(&g.render);
            printf("end\n");
        } else if (!strcmp(argv[1], "chart_session")) { /* F4, then 12 passes: a key and arrows each */
            int up = ep_chart_screen(&g) == EP_CMD_SCREEN;
            const uint8_t *keys[4] = { &g.in.up, &g.in.down, &g.in.left, &g.in.right };
            for (int k = 0; up && k < 12; k++) {
                if (g.f.station_step) { /* typing a name: the key goes to the text */
                    ep_station_key(&g, ds[0xff10 + k]);
                    continue;
                }
                uint8_t arrows = ds[0xff20 + k];
                for (int j = 0; j < 4; j++) g.in.key[*keys[j] & 0x7f] = (arrows >> j & 1) ? 0 : 0x80;
                g.in.last_key = ds[0xff10 + k];
                ep_station_idle(&g);
            }
            print_prims(&g.render);
            for (int k = 0; k < g.circles.n; k++)
                printf("span %d,%d,%d\n", g.circles.span[k].x, g.circles.span[k].w, g.circles.span[k].row);
            printf("end\n");
        } else if (!strcmp(argv[1], "start_game")) { /* the time at ds:ff30 (h, m, s, 1/100), keys at ff10 */
            int w = ep_start_game(&g, ds[0xff30], ds[0xff31], ds[0xff32], ds[0xff33]), k = 0;
            while (w != EP_WAIT_NONE) {
                uint8_t key = k < 8 ? ds[0xff10 + k] : 'Y';
                k++;
                w = ep_station_key(&g, key);
            }
            print_prims(&g.render);
            printf("end\n");
        } else if (!strcmp(argv[1], "pause_session")) { /* Esc, then 12 keys: a pass or a question each */
            int r = ep_pause_open(&g);
            for (int k = 0; r == EP_CMD_PAUSE && k < 12; k++) {
                uint8_t key = ds[0xff10 + k];
                if (g.f.station_step) {
                    ep_station_key(&g, key);
                    if (g.f.leave) break;
                    continue;
                }
                g.in.last_key = key;
                if (ep_pause_idle(&g) == EP_CMD_RESUME) break;
            }
            print_prims(&g.render);
            printf("end\n");
        } else if (!strcmp(argv[1], "save_session") || !strcmp(argv[1], "load_session")) {
            memcpy(disk_base.b, &ds[0x82db], EP_COMMANDER_SIZE);
            g.io = &disk;
            dialog_keys(&g, argv[1][0] == 's' ? ep_save_screen(&g) : ep_load_screen(&g));
            print_prims(&g.render);
            printf("end\n");
            if (g.f.leave) printf("leave %d\n", g.f.leave);
        } else if (!strcmp(argv[1], "key_event")) {
            for (int k = 0; k < 8; k++) ep_key_event(&g, ds[0xff10 + k]);
        } else if (!strcmp(argv[1], "timer")) { /* 24 ticks; what the speaker was last set to */
            g.speaker = 0xffff;
            g.speaker_on = 0xff;
            for (int k = 0; k < 24; k++) ep_timer_tick(&g);
            if (g.speaker != 0xffff) printf("speaker %d\n", g.speaker);
            if (g.speaker_on != 0xff) printf("gate %d\n", g.speaker_on);
        } else if (!strcmp(argv[1], "protection_pick")) {
            ep_protection_pick(&g);
        } else if (!strcmp(argv[1], "title_open")) { /* the waits given the keys at ds:ff10 */
            int w = ep_title_open(&g);
            for (int k = 0; w != EP_WAIT_NONE && k < 12; k++) w = ep_station_key(&g, ds[0xff10 + k]);
            print_prims(&g.render);
            printf("end\n");
        } else if (!strcmp(argv[1],
                           "title_session")) { /* 12 passes, a key each, until a screen or the game */
            int r = EP_CMD_STAY;
            for (int k = 0; r == EP_CMD_STAY && k < 12; k++) {
                g.in.last_key = ds[0xff10 + k];
                r = ep_title_frame(&g);
            }
            print_prims(&g.render);
            for (int k = 0; k < g.circles.n; k++)
                printf("span %d,%d,%d\n", g.circles.span[k].x, g.circles.span[k].w, g.circles.span[k].row);
            printf("%s\n", r == EP_CMD_START ? "start" : "end");
        } else if (!strcmp(argv[1], "data_screen")) {
            ep_data_screen(&g);
            print_prims(&g.render);
            printf("end\n");
        } else if (!strcmp(argv[1], "equip_screen")) {
            ep_equipment_screen(&g);
            print_prims(&g.render);
            printf("end\n");
        } else if (!strcmp(argv[1], "equip_session")) { /* 12 keys: a pass each, or a dialog's */
            ep_equipment_screen(&g);
            for (int k = 0; k < 12; k++) {
                uint8_t key = ds[0xff10 + k];
                if (g.f.station_step)
                    ep_station_key(&g, key);
                else {
                    g.in.last_key = key;
                    ep_station_idle(&g);
                }
            }
            print_prims(&g.render);
            printf("end\n");
        } else if (!strcmp(argv[1], "market")) {
            ep_market_screen(&g);
            print_prims(&g.render);
            printf("end\n");
        } else if (!strcmp(argv[1], "status")) {
            ep_enter_station(&g);
            int w = ep_status_screen(&g), k = 0;
            while (w != EP_WAIT_NONE) { /* the scripted keys (ds:ff10, 8 of them), then Y */
                uint8_t key = k < 8 ? ds[0xff10 + k] : 'Y';
                k++;
                w = ep_station_key(&g, key);
            }
            print_prims(&g.render);
            printf("end\n");
        } else if (!strcmp(argv[1], "launch")) {
            ep_launch(&g);
            print_prims(&g.render);
            for (int k = 0; k < g.circles.n; k++)
                printf("span %d,%d,%d\n", g.circles.span[k].x, g.circles.span[k].w, g.circles.span[k].row);
            printf("end\n");
        } else if (!strcmp(argv[1], "dock")) { /* 6864 alone: the tunnel, docking or not */
            ep_tunnel_start(&g);
            for (int k = 0; k < 20; k++) ep_tunnel_frame(&g, k);
            print_prims(&g.render);
            for (int k = 0; k < g.circles.n; k++)
                printf("span %d,%d,%d\n", g.circles.span[k].x, g.circles.span[k].w, g.circles.span[k].row);
        } else if (!strcmp(argv[1], "loop")) {
            g.test_dl_force = 1;
            g.test_dl = ds[0xff00]; /* the original's DL at 77e0, passed in a spare byte */
            printf("frame %d\n", ep_flight_frame(&g));
        } else if (!strcmp(argv[1], "frame")) {
            ep_key_bar(&g);
            ep_frame_before_ai(&g);
            g.f.reg_dl = ds[0xff00]; /* the original's DL at 77e0, passed in a spare byte */
            ep_frame_from_ai(&g);
            print_prims(&g.render);
            for (int k = 0; k < g.circles.n; k++)
                printf("span %d,%d,%d\n", g.circles.span[k].x, g.circles.span[k].w, g.circles.span[k].row);
        } else if (!strcmp(argv[1], "arrive")) {
            ep_arrive(&g);
        } else if (!strcmp(argv[1], "jump_missions")) {
            ep_jump_missions(&g);
        } else if (!strcmp(argv[1], "witchspace")) {
            ep_witchspace(&g);
        } else if (!strcmp(argv[1], "rings")) {
            ep_rings_frame(&g);
            for (int k = 0; k < g.circles.n; k++)
                printf("span %d,%d,%d\n", g.circles.span[k].x, g.circles.span[k].w, g.circles.span[k].row);
        } else if (!strcmp(argv[1], "select_system")) {
            ep_select_system(&g);
        } else if (!strcmp(argv[1], "flight_start")) {
            ep_flight_start(&g);
        } else if (!strcmp(argv[1], "new_system")) {
            ep_new_system(&g);
        } else if (!strcmp(argv[1], "jump_drive")) {
            ep_jump_drive(&g);
        } else if (!strcmp(argv[1], "missile_lock")) {
            ep_missile_lock(&g);
        } else if (!strcmp(argv[1], "tribbles")) {
            ep_tribbles_tick(&g);
            print_prims(&g.render);
        } else if (!strcmp(argv[1], "dust_reset")) {
            ep_dust_reset(&g);
        } else if (!strcmp(argv[1], "dashboard")) {
            ep_dashboard_tick(&g);
            print_prims(&g.render);
        } else if (!strcmp(argv[1], "ai")) {
            ep_ai_frame(&g);
        } else if (!strcmp(argv[1], "explode")) {
            ep_explode(&g, &g.space.obj[ds[0xff00]]); /* the slot is passed in a spare byte */
        } else if (!strcmp(argv[1], "collisions")) {
            ep_collisions(&g);
        } else if (!strcmp(argv[1], "enemy_fire")) {
            ep_enemy_fire(&g);
            print_prims(&g.render);
        } else if (!strcmp(argv[1], "controls")) {
            ep_controls(&g);
        } else if (!strcmp(argv[1], "tunnel")) {
            printf("end %d\n", ep_tunnel_tick(&g));
        } else {
            fprintf(stderr, "unknown subsystem %s\n", argv[1]);
            return 2;
        }
        for (int k = 0; k < g.nevents; k++) printf("event %d:%d\n", g.event[k].kind, g.event[k].arg);
        for (int k = 0; k < nwritten; k++) printf("%s\n", written[k]);
        state_store(&g, ds);
    } else {
        fprintf(stderr, "usage: subsys mask OUT | subsys NAME IN OUT\n");
        return 2;
    }
    FILE *f = fopen(argv[argc - 1], "wb");
    if (!f || fwrite(ds, 1, DS_SIZE, f) != DS_SIZE) return 2;
    fclose(f);
    return 0;
}
