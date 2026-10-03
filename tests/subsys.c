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
        int n = p->kind == EP_PRIM_TRI                                  ? 3
                : p->kind == EP_PRIM_QUAD                               ? 4
                : p->kind == EP_PRIM_PIXEL || p->kind == EP_PRIM_SPRITE ? 1
                                                                        : 2;
        printf("%d:%d", p->kind, p->colour);
        for (int j = 0; j < 2 * n; j++) printf(",%d", p->pt[j]);
        printf("\n");
    }
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
        } else if (!strcmp(argv[1], "fuel_leak")) {
            ep_fuel_leak(&g);
        } else if (!strcmp(argv[1], "energy_drain")) {
            ep_energy_drain(&g);
        } else if (!strcmp(argv[1], "laser")) {
            ep_laser_fire(&g);
        } else if (!strcmp(argv[1], "laser_hits")) {
            ep_laser_hits(&g);
            print_prims(&g.render);
        } else if (!strncmp(argv[1], "buy", 3) || !strncmp(argv[1], "sell", 4) ||
                   !strncmp(argv[1], "equip", 5)) {
            int row = ds[0xad2b]; /* the selected row, as the screen leaves it */
            uint16_t r = argv[1][0] == 'b'   ? ep_trade_buy(&g, row)
                         : argv[1][0] == 's' ? ep_trade_sell(&g, row)
                                             : ep_equip_buy(&g, row);
            if (r == EP_TRADE_CHOOSE_MOUNT)
                printf("choose mount\n");
            else if (r > EP_TRADE_CHOOSE_MOUNT)
                printf("result %u\n", r);
        } else if (!strcmp(argv[1], "dust")) {
            ep_dust_frame(&g);
            print_prims(&g.render);
        } else if (!strcmp(argv[1], "frame")) {
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
