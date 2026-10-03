/* Run one reconstructed subsystem on a state from the original, for re/emu/subtest.py.
 *   subsys mask OUT            write the mask of data-segment bytes the core models
 *   subsys NAME IN OUT         load IN (64 KB data segment), run NAME, store into OUT;
 *                              primitives drawn go to stdout, one per line */
#include "statemap.h"

#include "ep_world.h"

#include <stdio.h>
#include <string.h>

static uint8_t ds[DS_SIZE];

static void print_prims(const ep_render *r)
{
    for (int k = 0; k < r->nprim; k++) {
        const ep_prim *p = &r->prim[k];
        int n = p->kind == EP_PRIM_TRI ? 3 : p->kind == EP_PRIM_QUAD ? 4 : 2;
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
            for (int k = 0; k < g.nevents; k++) printf("event %d:%d\n", g.event[k].kind, g.event[k].arg);
        } else {
            fprintf(stderr, "unknown subsystem %s\n", argv[1]);
            return 2;
        }
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
