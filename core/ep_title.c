/* Elite Plus title screen, reconstructed from ELITE.EXE (see ep_title.h). */
#include "ep_title.h"

#include "ep_sound.h"
#include "ep_commands.h"
#include "ep_dsmap.h"
#include "ep_station.h"
#include "ep_world.h"

#include <string.h>

static void set16(ep_object *o, int off, uint16_t v)
{
    o->b[off] = (uint8_t)v;
    o->b[off + 1] = (uint8_t)(v >> 8);
}

static uint16_t get16(const ep_object *o, int off) { return (uint16_t)(o->b[off] | o->b[off + 1] << 8); }

/* 2fca: centred on x, shadowed */
static void centred(ep_game *g, int16_t x, int16_t y, uint8_t colour, uint16_t addr)
{
    uint8_t t[96];
    int n = ep_ds_text(g, addr, t, sizeof t);
    ep_pen(&g->render, (int16_t)(x - (ep_text_width(t) >> 1)), y, colour);
    ep_text(&g->render, t, n, 1);
}

int ep_title_open(ep_game *g)
{
    ep_space *s = &g->space;
    memset(s->obj, 0, (size_t)s->count * sizeof s->obj[0]);      /* 816b */
    ep_music_start(g);                                           /* 4d21: the title music */
    if (g->f.video == 2) ep_event_add(g, EP_EV_PALETTE, 0x1444); /* 3aef */
    ep_render_sprite(&g->render, 0x89, 0, 0);                    /* 3ae5: the intro picture */
    g->f.intro_until = g->clock + 1000;
    g->f.title_step = 1;
    g->f.station_step = EP_STEP_TITLE;
    return EP_WAIT_TIME;
}

/* 9ea3..9f1c: the title behind the credits */
static void title_setup(ep_game *g)
{
    ep_flight *f = &g->f;
    ep_space *s = &g->space;
    g->cmdr.b[EP_CMDR_LASERS] = 0;
    memset(s->player_angle, 0, sizeof s->player_angle);
    s->extra_angle = 0;
    f->other_screen = 2;
    ep_cockpit(g); /* 763e */
    f->screen = 5;
    centred(g, 0xa0, 0, 0x0f, 0xaf36); /* 2f12: on both pages */
    ep_object *o = &s->obj[EP_TITLE_SLOT];
    s->count = 3;
    set16(o, 0x0c, 0);
    set16(o, 0x0e, 0);
    set16(o, EP_OBJ_POS, 0);
    set16(o, EP_OBJ_POS + 2, 0);
    set16(o, EP_OBJ_POS + 4, 5000);
    set16(o, EP_OBJ_POS_HI, 0);
    o->b[EP_OBJ_POS_HI + 2] = 0;
    f->title_ship = ep_ds_byte(g, 0xb263);
    f->title_list = 0xb263;
    f->title_hold = 0;
    f->space_pressed = 0;
    /* af73: the credits */
    f->bar_quiet++;
    ep_view_clear(g); /* 3130 */
    ep_key_bar(g);
    uint8_t t[512];
    int n = ep_ds_header_text(g, 0xb13a, t, sizeof t);
    ep_text_header(&g->render, t, n, 1);
    ep_view_flip(g);       /* 301a */
    f->note_ticks = 0x2ee; /* 750 ticks: the credits' wait counts down in the note timer */
    g->in.last_key = 0xff; /* 0287 */
}

int ep_title_key(ep_game *g, uint8_t key)
{
    ep_flight *f = &g->f;
    if (f->title_step == 1) { /* 3b18: until a key or the time is past */
        if (g->clock <= f->intro_until && key == 0xff) return EP_WAIT_TIME;
        if (key != 0xff) g->in.last_key = 0xff;
        if (f->video == 2) ep_event_add(g, EP_EV_PALETTE, 0x1144); /* 3b35 */
        title_setup(g);
        f->title_step = 2;
        return EP_WAIT_TIME;
    }
    if (key != 0xff) { /* af8f */
        g->in.last_key = 0xff;
        f->note_ticks = 0;
    }
    if (f->note_ticks) return EP_WAIT_TIME;
    g->in.last_key = 0xff; /* afa1 */
    f->bar_quiet--;
    f->screen_shown = 0xff;
    f->title_step = 0;
    f->station_step = 0;
    return EP_WAIT_NONE;
}

int ep_title_frame(ep_game *g)
{
    ep_flight *f = &g->f;
    ep_key_bar(g);
    if (f->leave == 2) return EP_CMD_QUIT;
    ep_music_again(g); /* 4d8e */
    ep_view_clear(g);  /* 3130 */
    /* 9f2a: the red disc, jittered (ds:108f = 1) */
    int n0 = g->circles.n;
    ep_draw_circle(&g->rng, 0xc8, 0x3c, 0x19, 1, 0, f->video == 2, &g->circles);
    ep_render_spans(&g->render, 0xb6, n0, g->circles.n - n0);

    /* 9f47: the ship's distance */
    ep_object *o = &g->space.obj[EP_TITLE_SLOT];
    uint16_t z = get16(o, EP_OBJ_POS + 4);
    if (f->title_hold == 0 && (uint16_t)(z - 0x50) >= ep_ds_word(g, (uint16_t)(0xb1bc + 2 * f->title_ship))) {
        set16(o, EP_OBJ_POS + 4, (uint16_t)(z - 0x50));
    } else if (++f->title_hold >= 0x78) {
        f->title_hold--; /* held at 78h, so every frame from here on backs off */
        z = (uint16_t)(z + 100);
        set16(o, EP_OBJ_POS + 4, z);
        if (z >= 5000) {
            set16(o, EP_OBJ_POS + 4, 5000);
            uint16_t at = (uint16_t)(f->title_list + 1);
            if (ep_ds_byte(g, at) == 0xff) at = 0xb263;
            f->title_list = at;
            f->title_ship = ep_ds_byte(g, at);
            f->title_hold = 0;
        }
    }
    /* 9fab: type, spin */
    o->b[EP_OBJ_FLAGS1E] = 2;
    o->b[EP_OBJ_FLAGS] = (uint8_t)(f->title_ship << 1 | 1);
    set16(o, 0x0e, (uint16_t)(get16(o, 0x0e) + 0x1e));
    set16(o, 0x0c, (uint16_t)(get16(o, 0x0c) + 0x14));
    set16(o, 0x0a, (uint16_t)(get16(o, 0x0a) + 0x19));
    /* 9fc3: the ship's name, the invitation */
    centred(g, 0xa0, 0x0c, 0x11, ep_ds_word(g, (uint16_t)(0xb27c + 2 * f->title_ship)));
    centred(g, 0xa0, 0x78, 0x0a, 0xaf19);
    if (++f->flash == 6) f->flash = 0; /* 3921: the flashing colour */
    int drawn[EP_OBJECTS];
    ep_world_update(g, drawn); /* 4154 */
    ep_view_flip(g);           /* 301a */
    int r = ep_commands(g);
    if (r != EP_CMD_STAY) return r;
    return f->space_pressed ? EP_CMD_START : EP_CMD_STAY;
}
