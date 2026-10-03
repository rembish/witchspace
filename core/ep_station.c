/* Elite Plus at the station, reconstructed from ELITE.EXE (see ep_station.h). */
#include "ep_station.h"

#include <string.h>

#include "ep_sound.h"
#include "ep_chart.h"
#include "ep_circle.h"
#include "ep_flight.h"
#include "ep_galaxy.h"
#include "ep_desc.h"
#include "ep_dsmap.h"
#include "ep_combat.h"
#include "ep_commands.h"
#include "ep_render.h"
#include "ep_market.h"
#include "ep_boot.h"
#include "ep_title.h"
#include "ep_trade.h"

/* 2e6d at (x, y): the text at ds:addr */
static void text_at(ep_game *g, int16_t x, int16_t y, uint8_t colour, uint16_t addr)
{
    uint8_t t[256];
    int n = ep_ds_text(g, addr, t, sizeof t);
    ep_pen(&g->render, x, y, colour);
    ep_text(&g->render, t, n, 0);
}

/* 2e6d at the pen */
static void text_on(ep_game *g, uint16_t addr)
{
    uint8_t t[256];
    int n = ep_ds_text(g, addr, t, sizeof t);
    ep_text(&g->render, t, n, 0);
}

/* 2e5f: a text with its own position and colour */
static void text_header(ep_game *g, uint16_t addr)
{
    uint8_t t[512];
    int n = ep_ds_header_text(g, addr, t, sizeof t);
    ep_text_header(&g->render, t, n, 0);
}

/* 2fca: centred on x, shadowed */
static void title(ep_game *g, int16_t x, int16_t y, uint8_t colour, const uint8_t *s, int n)
{
    ep_pen(&g->render, (int16_t)(x - (ep_text_width(s) >> 1)), y, colour);
    ep_text(&g->render, s, n, 1);
}

/* 6f91: five digits; 6fbc: up to n leading zeros blanked; returns the first shown */
static int digits(uint8_t *d, uint16_t v, int n)
{
    static const uint16_t pow10[4] = { 10000, 1000, 100, 10 };
    for (int k = 0; k < 4; k++) {
        d[k] = (uint8_t)('0' + v / pow10[k]);
        v %= pow10[k];
    }
    d[4] = (uint8_t)('0' + v);
    int k = 0;
    for (; k < n && d[k] < '1'; k++) d[k] = ' ';
    return k;
}

static void sprite(ep_game *g, uint8_t id, int16_t x, int16_t y) { ep_render_sprite(&g->render, id, x, y); }

static void frame_kind(ep_game *g, uint8_t kind);

/* 7748: the frame of a screen at the station */
static void frame(ep_game *g) { frame_kind(g, 2); }

/* 76cb (kind 1, a screen in flight) and 7748 (kind 2) */
static void frame_kind(ep_game *g, uint8_t kind)
{
    ep_render *r = &g->render;
    if (g->f.other_screen)
        ep_render_rect(r, 0, 8, 9, 0x130, 0xaf);
    else
        ep_render_rect(r, 0, 0, 9, 0x140, 0xaf);
    sprite(g, 0x87, 0, 0xb8);
    sprite(g, 0x88, 0x130, 0xb8);
    sprite(g, 0x82, 0, 9);
    sprite(g, 0x83, 0x138, 9);
    sprite(g, 0x85, 0, 0x85);
    sprite(g, 0x86, 0x138, 0x85);
    sprite(g, 0x6c, 0, 0); /* 30d2: the message line */
    g->f.other_screen = kind;
    g->f.screen_shown = 0xff;
}

uint16_t ep_rating_text(const ep_game *g, uint16_t kills)
{
    uint16_t at = 0x8ead;
    int k = -1;
    do {
        at = (uint16_t)(at + 2);
        k++;
    } while (kills >= ep_ds_word(g, at));
    return ep_ds_word(g, (uint16_t)(0x8ec1 + 2 * k));
}

void ep_status_picture(ep_game *g)
{
    const uint8_t *e = &g->cmdr.b[EP_CMDR_EQUIPMENT];
    ep_render_rect(&g->render, 0, 0x18, 0x87, 0x110, 0x2e);
    sprite(g, 0x37, 0x20, 0x90);
    sprite(g, 0x38, 0xb0, 0x90);
    if (e[6]) sprite(g, 0x4f, 0x48, 0x94); /* escape capsule */
    if (e[5]) sprite(g, 0x50, 0xd8, 0x92); /* fuel scoops */
    if (e[1]) {                            /* cargo bay extension */
        static const int16_t x[4] = { 0x33, 0x6d, 0xc3, 0xfd };
        for (int k = 0; k < 4; k++) sprite(g, 0x51, x[k], 0xb0);
    }
    if (e[7]) sprite(g, 0x4e, 0xe4, 0xa1); /* energy bomb */
    for (int k = 0; k < e[0]; k++)         /* missiles */
        sprite(g, 0x4d, (int16_t)ep_ds_word(g, (uint16_t)(0xa3e0 + 4 * k)),
               (int16_t)ep_ds_word(g, (uint16_t)(0xa3e2 + 4 * k)));
    uint8_t mounts = g->cmdr.b[EP_CMDR_LASERS], types = g->cmdr.b[EP_CMDR_LASER_TYPES];
    uint16_t at = 0xa3f8;
    for (int cx = 4; cx >= 1; cx--) { /* the lasers on their mounts */
        if (mounts & 1) {
            uint8_t base = (uint8_t)((types & 3) * 4 + 0x39);
            sprite(g, (uint8_t)(base + ep_ds_byte(g, (uint16_t)(0xa3ef + cx))), (int16_t)ep_ds_word(g, at),
                   (int16_t)ep_ds_word(g, (uint16_t)(at + 2)));
            sprite(g, (uint8_t)(base + ep_ds_byte(g, (uint16_t)(0xa3f3 + cx))),
                   (int16_t)ep_ds_word(g, (uint16_t)(at + 4)), (int16_t)ep_ds_word(g, (uint16_t)(at + 2)));
            if (cx == 4) sprite(g, (uint8_t)((types & 3) + 0x49), 0x54, 0x90);
        }
        mounts >>= 1;
        types >>= 2;
        at = (uint16_t)(at + 6);
    }
    if (e[2]) { /* ECM */
        sprite(g, 0x5a, 0x40, 0x8b);
        sprite(g, 0x5a, 0x64, 0x8b);
    }
    if (e[9]) { /* docking computer */
        sprite(g, 0x5b, 0xd9, 0xa6);
        sprite(g, 0x5b, 0xef, 0xa6);
    }
    if (e[8]) sprite(g, 0x5c, 0x50, 0xa5); /* extra energy unit */
}

void ep_status_view(ep_game *g)
{
    ep_flight *f = &g->f;
    frame(g);
    ep_select_system(g);
    /* "COMMANDER" and the name, as one line */
    uint8_t t[64];
    int n = 0;
    uint16_t a = 0x82f0;
    uint8_t c;
    do {
        c = ep_ds_byte(g, a++);
        t[n++] = c;
    } while (c && c != ' ' && n < 32);
    t[n - 1] = ' ';
    a = 0x8370;
    do {
        c = ep_ds_byte(g, a++);
        t[n++] = c;
    } while (c && c != ' ' && n < 63);
    t[n - 1] = 0;
    title(g, 0xa0, 0, 0x0f, t, n);
    text_header(g, 0x8df5);
    int witch = f->hyperspace != 0;
    text_at(g, 0x46, 0x19, witch ? 0x0c : 0x0f, witch ? 0x92fb : 0x831f); /* present system */
    text_header(g, 0x8e02);
    text_at(g, 0x64, 0x28, witch ? 0x0c : 0x0f, witch ? 0x92fb : 0x8338); /* hyperspace system */
    /* 8e48: fuel in light years */
    uint8_t q = (uint8_t)((uint16_t)(g->cmdr.b[EP_CMDR_FUEL] * 10) / 0x24);
    digits(&f->fuel_text[0x19], q, 0);
    f->fuel_text[0x09] = f->fuel_text[0x1c];
    f->fuel_text[0x0b] = f->fuel_text[0x1d];
    text_at(g, 0x14, 0x37, 0x10, 0x8e14);
    text_at(g, 0x14, 0x46, 0x10, 0x8e32);
    a = 0x82fb; /* the cash, without its leading spaces */
    while (ep_ds_byte(g, a) == ' ') a++;
    g->render.pen_x = (int16_t)(g->render.pen_x + 7);
    g->render.pen_colour = 0x0f;
    text_on(g, a);
    text_header(g, 0x8e38);
    uint16_t cond = f->screen == 1 ? 0x8e49 : ep_ds_word(g, (uint16_t)(0x8e52 + 2 * (f->status & 3)));
    g->render.pen_x = (int16_t)(g->render.pen_x + 6);
    text_on(g, cond);
    text_header(g, 0x8e71);
    uint8_t legal = g->cmdr.b[EP_CMDR_LEGAL];
    text_at(g, 0x68, 0x64, 0x0f, ep_ds_word(g, (uint16_t)(0x8e84 + 2 * (legal >= 1) + 2 * (legal >= 0x28))));
    text_header(g, 0x8ea2);
    uint16_t kills =
        (uint16_t)(g->cmdr.b[EP_CMDR_KILLS + 2] | g->cmdr.b[EP_CMDR_KILLS + 3] << 8); /* ds:836e */
    text_at(g, 0x45, 0x73, 0x0f, ep_rating_text(g, kills));
    if (f->tribbles) {
        text_header(g, 0x8dc0);
        f->tribble_text[0x0d] = 0;
        if (f->tribbles != 1) {
            f->tribble_text[0x0d] = 's';
            f->tribble_text[0x0e] = 0;
        }
        int k = digits(f->tribble_text, f->tribbles, 4);
        text_at(g, 0xae, 0x2c, 0x14, (uint16_t)(0x8de6 + k));
    } else if (g->cmdr.b[0xc0]) { /* ds:839b: mission cargo aboard */
        text_header(g, 0x8dc0);
        text_header(g, 0x8dd8);
    }
    ep_status_picture(g);
}

/* where the arrival waits */
enum {
    ST_NONE = 0,
    ST_RATING,     /* 8bd8: the promotion shown */
    ST_ELITE,      /* 3b96: the Elite picture */
    ST_TRIBBLES,   /* 8b40: Y/N */
    ST_M1_ASK,     /* 991d: Y/N */
    ST_M1_NO,      /* 993b */
    ST_M1_YES,     /* 9974 */
    ST_M2_BRIEF,   /* 9998 */
    ST_M3_BRIEF,   /* 99c9 */
    ST_BRIEF_DONE, /* m4..6: nothing after the key */
    ST_M1_DONE,    /* 9b5c */
    ST_M2_DONE,    /* 9ba5 */
    ST_M3_DONE,    /* 9c25 */
    ST_M456_DONE,  /* 9c75, 9cb5, 9cf3 */
    ST_MOUNT_BUY,  /* 9502: the mount for a laser bought */
    ST_MOUNT_SELL, /* 968f: the mount of a laser sold */
    ST_FIND_TEXT,  /* 61b1: the name to find */
    ST_ABANDON,    /* 0aac: Sure ? (Y/N) */
    ST_EXIT,       /* 0aef */
    ST_SAVE_NAME,  /* 07f7: the commander's name */
    ST_SAVE_ASK,   /* 084f: overwrite? (Y/N) */
    ST_SAVE_DONE,  /* 086a: space, then the box goes */
    ST_LOAD_LIST,  /* 0945: which file */
    ST_LOAD_NONE,  /* 08e4: none there (space) */
    ST_LOAD_GONE,  /* 0a29: it could not be opened (space) */
    ST_LOAD_BAD,   /* 0a0f: a bad file (space), then the title */
    ST_LOAD_GOOD,  /* 0a35: loaded (space), then the station */
    ST_PROTECTION, /* 1472: the word from the novella */
    ST_DEFINE_ASK, /* 0694: redefine? (Y/N) */
    ST_DEFINE_KEY, /* 05e5, 05f9: a key to define */
    ST_JOY_CENTRE, /* 0745: space, the joystick centred */
    ST_JOY_ERROR   /* 075e: space after the error */
};

static int wait(ep_game *g, uint8_t step, int kind)
{
    g->f.station_step = step;
    g->in.last_key = 0xff; /* 0287 */
    return kind;
}

static void earn(ep_game *g, uint32_t tenths)
{
    ep_commander_set_cash(&g->cmdr, ep_commander_cash(&g->cmdr) + tenths);
    ep_cash_text(&g->cmdr);
}

/* the frame and a title in a dialog's colour, then the bar (0299) */
static void dialog(ep_game *g, uint16_t text, uint8_t colour)
{
    frame(g);
    uint8_t t[128];
    int n = ep_ds_text(g, text, t, sizeof t);
    title(g, 0xa0, 0, colour, t, n);
    ep_key_bar(g);
}

/* a system for missions 4..6: a main-generator step whose byte is not here */
static uint8_t mission_target(ep_game *g)
{
    uint8_t s;
    do s = (uint8_t)(ep_rng_step(&g->rng) >> 16);
    while (s == g->cmdr.b[EP_CMDR_CURRENT + EP_SYSREC_INDEX]);
    g->f.mission_system = s;
    g->seed = ep_system_seed(g->cmdr.b[EP_CMDR_GALAXY], s); /* 610e, 6130: its name */
    ep_planet_name(&g->seed, &g->cmdr.b[EP_CMDR_SELECTED + EP_SYSREC_NAME]);
    return s;
}

/* 98eb: a mission's briefing */
static int briefing(ep_game *g)
{
    ep_flight *f = &g->f;
    f->mission5_phase = 1;
    dialog(g, 0x891f, 0x0c);
    switch (f->mission) {
    case 1: text_header(g, 0x9523); return wait(g, ST_M1_ASK, EP_WAIT_YN);
    case 2: text_header(g, 0x9d4f); return wait(g, ST_M2_BRIEF, EP_WAIT_KEY);
    case 4:
    case 5:
    case 6: {
        static const uint16_t text[3][2] = { { 0x99a0, 0x9a1e }, { 0x9a7d, 0x9b28 }, { 0x9bb1, 0x9bee } };
        int m = f->mission - 4;
        mission_target(g);
        if (f->mission == 4) f->mission_state = 1;
        if (f->mission == 6) f->mission_state = 0x32;
        text_header(g, text[m][0]);
        text_on(g, 0x8338);
        text_header(g, text[m][1]);
        return wait(g, ST_BRIEF_DONE, EP_WAIT_KEY);
    }
    default: text_header(g, 0xa090); return wait(g, ST_M3_BRIEF, EP_WAIT_KEY);
    }
}

/* 9ac4: back from a mission */
static int debriefing(ep_game *g)
{
    ep_flight *f = &g->f;
    uint8_t *c = g->cmdr.b;
    if (f->mission5_phase != 1 && f->mission < 4) return EP_WAIT_NONE;
    if (f->mission == 2 && f->convoy_leader_dead != 1) return EP_WAIT_NONE;
    if (f->mission == 1 && f->siege != 1) return EP_WAIT_NONE;
    switch (f->mission) {
    case 4:
    case 5:
    case 6: {
        int due = f->mission == 4   ? f->mission_state == 3
                  : f->mission == 5 ? c[EP_CMDR_CURRENT + EP_SYSREC_INDEX] == f->mission_system
                                    : f->mission_state == 1;
        if (!due) return EP_WAIT_NONE;
        dialog(g, 0x9766, 0x0a);
        static const uint16_t text[3] = { 0x9787, 0x981d, 0x98b8 };
        static const uint32_t pay[3] = { 0x9c40, 0xc350, 0xea60 };
        text_header(g, text[f->mission - 4]);
        earn(g, pay[f->mission - 4]);
        return wait(g, ST_M456_DONE, EP_WAIT_KEY);
    }
    case 1: {
        dialog(g, 0x9766, 0x0a);
        f->mission5_phase = 2;
        int full = c[0xc0] == 0x14; /* ds:839b: the cargo taken on */
        f->reward_digit = full ? '0' : '4';
        c[0xc0] = 0;
        c[EP_CMDR_CARGO_USED] = 0;
        earn(g, full ? 0x2710 : 0x36b0);
        text_header(g, 0x993f);
        return wait(g, ST_M1_DONE, EP_WAIT_KEY);
    }
    case 2: {
        dialog(g, 0x9766, 0x0a);
        uint16_t text = c[0xd7] == 1 ? 0x9f3c : 0x9e70; /* ds:83b2 */
        if (c[0xcd] == 1) {                             /* ds:83a8: the masking device earned */
            *(&c[EP_CMDR_EQUIPMENT] + 0x0d) = 1;
            sprite(g, 0x58, 0x58, 0x44);
            text = 0x9fa0;
        }
        text_header(g, text);
        return wait(g, ST_M2_DONE, EP_WAIT_KEY);
    }
    default:
        if (f->station_angry || f->mission_state) return EP_WAIT_NONE;
        dialog(g, 0x9766, 0x0a);
        sprite(g, 0x59, 0xe0, 0x74);
        text_header(g, 0xa1ef);
        return wait(g, ST_M3_DONE, EP_WAIT_KEY);
    }
}

/* 8b72: the rating, promotion shown when it changed */
static int promotion(ep_game *g)
{
    uint8_t *c = g->cmdr.b;
    uint16_t old = (uint16_t)(c[EP_CMDR_KILLS + 2] | c[EP_CMDR_KILLS + 3] << 8);
    uint16_t kills = (uint16_t)(c[EP_CMDR_KILLS] | c[EP_CMDR_KILLS + 1] << 8);
    c[EP_CMDR_KILLS + 2] = (uint8_t)kills;
    c[EP_CMDR_KILLS + 3] = (uint8_t)(kills >> 8);
    uint16_t was = ep_rating_text(g, old), now = ep_rating_text(g, kills);
    if (was == now) return EP_WAIT_NONE;
    dialog(g, 0x8938, 0x0e);
    uint8_t d[6];
    int k = digits(d, kills, 5);
    d[5] = 0;
    text_header(g, 0x9307);
    ep_text(&g->render, d + k, 6 - k, 0);
    text_on(g, 0x931f);
    g->render.pen_colour = 0x0f;
    text_on(g, now);
    g->render.pen_colour = 0x0e;
    text_on(g, 0x934f);
    g->f.station_rating = now == 0x8f24;
    return wait(g, ST_RATING, EP_WAIT_KEY);
}

/* 8b02: Tribbles for sale */
static int tribble_offer(ep_game *g)
{
    uint8_t *c = g->cmdr.b;
    uint16_t price = (uint16_t)(c[0xdc] | c[0xdd] << 8); /* ds:83b7 */
    if (!price) return EP_WAIT_NONE;
    uint32_t cash = ep_commander_cash(&g->cmdr);
    if (!(cash >> 16) && price >= (uint16_t)cash) return EP_WAIT_NONE;
    c[0xdc] = c[0xdd] = 0;
    dialog(g, 0x8929, 0x0c);
    text_header(g, 0x937b);
    return wait(g, ST_TRIBBLES, EP_WAIT_YN);
}

/* 8c03 onwards: the arrival's steps, from where the last key left off */
static int arrival(ep_game *g)
{
    ep_flight *f = &g->f;
    int r;
    switch (f->station_step) {
    case ST_RATING: goto after_rating;
    case ST_ELITE:
        if (f->video == 2) ep_event_add(g, EP_EV_PALETTE, 0x1144); /* 3ba8 */
        goto after_elite;
    case ST_TRIBBLES: goto after_tribbles;
    case ST_NONE: break;
    default: goto after_mission;
    }
    if (f->launching == 1) goto view;
    if ((r = promotion(g))) return r;
after_rating:
    if (f->station_rating) { /* 3b75: the Elite picture, palette cycling until a key */
        f->station_rating = 0;
        if (f->video == 2) ep_event_add(g, EP_EV_PALETTE, 0x1744); /* 3b7f */
        sprite(g, 0x8a, 0, 0);
        return wait(g, ST_ELITE, EP_WAIT_KEY);
    }
after_elite:
    if ((r = tribble_offer(g))) return r;
after_tribbles:
    if (f->mission) {
        if (!f->mission5_phase)
            r = briefing(g);
        else
            r = f->docked == 1 ? debriefing(g) : EP_WAIT_NONE;
        if (r) return r;
    }
after_mission:
view:
    f->station_step = ST_NONE;
    ep_status_view(g);
    f->screen_redraw = 0;
    f->idle = EP_IDLE_STATUS;
    return EP_WAIT_NONE;
}

int ep_status_screen(ep_game *g)
{
    ep_flight *f = &g->f;
    f->screen_bits = 0;
    if (f->screen == 0) f->screen = 2;
    f->screen_flag = 0;
    f->station_step = ST_NONE;
    return arrival(g);
}

static int find_key(ep_game *g, uint8_t key);
static int protection_key(ep_game *g, uint8_t key);
static int controls_key(ep_game *g, uint8_t key);
static int save_key(ep_game *g, uint8_t key);
static int load_key(ep_game *g, uint8_t key);
static int mount_key(ep_game *g, uint8_t key);

int ep_station_key(ep_game *g, uint8_t key)
{
    ep_flight *f = &g->f;
    uint8_t *c = g->cmdr.b;
    int yes = key == 'Y' || key == 'y', no = key == 'N' || key == 'n';
    switch (f->station_step) {
    case ST_TRIBBLES:
        if (!yes && !no) return EP_WAIT_YN;
        if (yes) {
            f->tribbles = 1;
            ep_pay(g, 0xc350);
        } else if (!(ep_commander_cash(&g->cmdr) >> 16)) {
            c[0xdc] = c[0xdd] = 0xff;
        }
        break;
    case ST_M1_ASK:
        if (!yes && !no) return EP_WAIT_YN;
        if (no) {
            text_header(g, 0x95a9);
            return wait(g, ST_M1_NO, EP_WAIT_KEY);
        }
        for (int k = 0; k < 17; k++) c[EP_CMDR_CARGO + 2 * k] = 0;
        c[EP_CMDR_CARGO_USED] = c[EP_CMDR_EQUIPMENT + 1] == 1 ? 0x23 : 0x14;
        c[0xc0] = c[EP_CMDR_CARGO_USED];
        text_header(g, 0x968d);
        return wait(g, ST_M1_YES, EP_WAIT_KEY);
    case ST_M1_NO:
        f->mission = 0;
        /* fall through */
    case ST_M1_YES:
        f->approach = 0xfa0;
        f->siege = 0;
        c[EP_CMDR_DOCKED_AT] = c[EP_CMDR_GALAXY];
        c[EP_CMDR_DOCKED_AT + 1] = c[EP_CMDR_CURRENT + EP_SYSREC_INDEX];
        break;
    case ST_M2_BRIEF:
        f->convoy_left = 5;
        f->convoy_countdown = 2;
        break;
    case ST_M3_BRIEF:
        f->siege = 0;
        f->mission_state = 1;
        break;
    case ST_M1_DONE:
    case ST_M456_DONE:
        f->mission = 0;
        f->mission5_phase = 0;
        break;
    case ST_M2_DONE:
        f->convoy_left = 0;
        f->convoy_leader_dead = 0;
        f->mission = 0;
        c[0xcd] = 0;
        f->convoy_countdown = 0;
        c[0xd7] = 0;
        f->mission5_phase = 0;
        break;
    case ST_M3_DONE:
        for (int k = 0; k < 9; k++)
            c[EP_CMDR_TITLE + k] = ep_ds_byte(g, (uint16_t)(0x8607 + k)); /* 7614: ARCHANGEL */
        c[0xd1] = 1;                                                      /* ds:83ac */
        f->mission = 0;
        f->mission5_phase = 0;
        f->station_hit = 0;
        break;
    case ST_MOUNT_BUY:
    case ST_MOUNT_SELL: return mount_key(g, key);
    case ST_FIND_TEXT: return find_key(g, key);
    case EP_STEP_TITLE: return ep_title_key(g, key);
    case ST_PROTECTION: return protection_key(g, key);
    case ST_DEFINE_ASK:
    case ST_DEFINE_KEY:
    case ST_JOY_CENTRE:
    case ST_JOY_ERROR: return controls_key(g, key);
    case ST_SAVE_NAME:
    case ST_SAVE_ASK:
    case ST_SAVE_DONE: return save_key(g, key);
    case ST_LOAD_LIST:
    case ST_LOAD_NONE:
    case ST_LOAD_GONE:
    case ST_LOAD_BAD:
    case ST_LOAD_GOOD: return load_key(g, key);
    case ST_ABANDON:
    case ST_EXIT:
        if (!yes && !no) return EP_WAIT_YN;
        if (no)
            ep_box_close(g);
        else if (f->station_step == ST_EXIT)
            f->leave = 2; /* 00ba */
        else {
            f->leave = 1; /* 0ac4: back to the title (9e80) */
            f->screen_shown = 0xff;
            g->space.in_flight = 0;
        }
        f->station_step = ST_NONE;
        return EP_WAIT_NONE;
    case ST_NONE: return EP_WAIT_NONE;
    default: break;
    }
    return arrival(g);
}

/* ---- the list (0bea..0d9a) ---- */

static uint16_t menu_word(const ep_game *g, int k)
{
    return (uint16_t)(g->f.menu[k] | g->f.menu[k + 1] << 8);
}

static void menu_set_word(ep_game *g, int k, uint16_t v)
{
    g->f.menu[k] = (uint8_t)v;
    g->f.menu[k + 1] = (uint8_t)(v >> 8);
}

/* 0d27: the k-th text of the list */
static uint16_t list_item(const ep_game *g, uint8_t k)
{
    uint16_t at = menu_word(g, 2);
    while (k) {
        uint8_t c = ep_ds_byte(g, at++);
        if (c == 0)
            k--;
        else if (c == 1)
            at++;
        else if (c == 2)
            at = (uint16_t)(at + 4);
    }
    return at;
}

/* 0d49: a row, highlighted when it is the cursor's */
static void list_row(ep_game *g, uint8_t row, uint16_t item)
{
    const uint8_t *m = g->f.menu;
    int cur = item == menu_word(g, 4);
    int16_t x = (int16_t)menu_word(g, 6), y = (int16_t)(menu_word(g, 8) + row * 8),
            w = (int16_t)menu_word(g, 10);
    ep_render_rect(&g->render, cur ? m[15] : m[13], x, y, w, 8);
    uint8_t t[256];
    int n = ep_ds_text(g, item, t, sizeof t);
    if (!m[16])
        ep_pen(&g->render, (int16_t)(x + 2), y, cur ? m[14] : m[12]);
    else /* 2fc0 */
        ep_pen(&g->render, (int16_t)(x + (w >> 1) - (ep_text_width(t) >> 1)), y, cur ? m[14] : m[12]);
    ep_text(&g->render, t, n, 0);
}

void ep_list_open(ep_game *g, uint16_t colours, uint16_t selected, uint8_t rows, uint8_t cursor,
                  uint16_t items, int16_t x, int16_t y, int16_t w)
{
    uint8_t *m = g->f.menu;
    menu_set_word(g, 12, colours);
    menu_set_word(g, 14, selected);
    m[0] = rows & 0x7f;
    m[16] = (uint8_t)(rows >> 7);
    m[1] = cursor;
    menu_set_word(g, 2, items);
    menu_set_word(g, 6, (uint16_t)x);
    menu_set_word(g, 8, (uint16_t)y);
    menu_set_word(g, 10, (uint16_t)w);
    menu_set_word(g, 4, list_item(g, cursor));
    g->f.last_cmd_key = 0xff;
    ep_list_poll(g, 1);
}

/* 0cac: the joystick or the mouse moves the list's cursor as the arrows do (ds:03f1) */
static void list_device(ep_game *g)
{
    ep_flight *f = &g->f;
    if (g->in.control == 1) {
        if (f->list_delay) { /* a move every nine ticks at most */
            if (f->list_tick != (uint16_t)g->clock) {
                f->list_tick = (uint16_t)g->clock;
                f->list_delay--;
            }
            return;
        }
        int8_t p = (int8_t)(ep_joystick_steering(g) >> 8);
        if (p <= -5) {
            f->last_cmd_key = 0x50;
            f->list_delay = 9;
        } else if (p >= 5) {
            f->last_cmd_key = 0x48;
            f->list_delay = 9;
        }
    } else if (g->in.control == 2) {
        f->list_mickeys = (int16_t)(f->list_mickeys + g->in.mouse_dy); /* int 33h, 0bh */
        g->in.mouse_dx = g->in.mouse_dy = 0;
        if (f->list_mickeys >= 0x19) {
            f->list_mickeys = (int16_t)(f->list_mickeys - 0x19);
            f->last_cmd_key = 0x50;
        } else if (f->list_mickeys <= -0x19) {
            f->list_mickeys = (int16_t)(f->list_mickeys + 0x19);
            f->last_cmd_key = 0x48;
        }
    }
}

uint8_t ep_list_poll(ep_game *g, int mode)
{
    uint8_t *m = g->f.menu;
    list_device(g); /* 0cac */
    if (mode == 2) {
        menu_set_word(g, 4, list_item(g, m[1]));
        list_row(g, m[1], menu_word(g, 4));
    } else if (mode == 1) {
        for (int r = m[0]; r > 0; r--) list_row(g, (uint8_t)(r - 1), list_item(g, (uint8_t)(r - 1)));
    }
    uint8_t key = g->f.last_cmd_key;
    g->f.last_cmd_key = 0xff;
    uint8_t row = m[1];
    if (key == 0x48 && row) {
        row--;
        key = 0xff;
    }
    if (key == 0x50 && row != (uint8_t)(m[0] - 1)) {
        row++;
        key = 0xff;
    }
    if (row != m[1]) {
        uint16_t old = menu_word(g, 4);
        uint8_t was = m[1];
        menu_set_word(g, 4, list_item(g, row));
        m[1] = row;
        list_row(g, was, old);
        list_row(g, row, menu_word(g, 4));
    }
    return key;
}

/* ---- the market ---- */

void ep_market_prices(ep_game *g)
{
    const uint8_t *cur = &g->cmdr.b[EP_CMDR_CURRENT];
    for (int k = 0; k < 17; k++) {
        uint16_t buy =
            ep_goods_price(k, cur[EP_SYSREC_GOVERNMENT], cur[EP_SYSREC_ECONOMY], cur[EP_SYSREC_TECH]);
        g->f.prices[2 * k] = buy;
        g->f.prices[2 * k + 1] = ep_sell_price(buy);
    }
}

/* 8e89: a price in tenths as "123.4", from its first digit shown (up to 3 zeros blanked;
 * the last digit moves behind the point) */
static int price_text(uint8_t *out, uint16_t v)
{
    uint8_t d[5];
    int k = digits(d, v, 3), n = 0;
    for (int i = k; i < 4; i++) out[n++] = d[i];
    out[n++] = '.';
    out[n++] = d[4];
    out[n] = 0;
    return n;
}

/* 6f91 + 6fbc(4): a quantity, from its first digit shown */
static int count_text(uint8_t *out, uint16_t v)
{
    uint8_t d[5];
    int k = digits(d, v, 4), n = 0;
    for (int i = k; i < 5; i++) out[n++] = d[i];
    out[n] = 0;
    return n;
}

static uint16_t market_random(ep_game *g) /* 9880 */
{
    uint16_t *w = g->market_rng;
    uint16_t old0 = w[0], old1 = w[1], old2 = w[2];
    w[0] = old1;
    w[1] = old2;
    uint16_t r = (uint16_t)(old0 + old1);
    w[2] = (uint16_t)(old2 + r);
    return r;
}

void ep_market_rows(ep_game *g)
{
    ep_flight *f = &g->f;
    ep_market_prices(g);
    uint8_t *o = f->rows;
    int at = 0;
#define PUT(b)     (at < (int)sizeof f->rows ? (void)(o[at++] = (uint8_t)(b)) : (void)0)
#define MOVE(x, y) (PUT(2), PUT(x), PUT((x) >> 8), PUT(y), PUT((y) >> 8))
    uint16_t names = 0xabe0, units = 0xac82, y = 0x1d;
    uint8_t *cargo = &g->cmdr.b[EP_CMDR_CARGO];
    for (int k = 0; k < 17; k++) {
        int last = k == 16;
        uint8_t c;
        while ((c = ep_ds_byte(g, names++)) != 0) PUT(c);
        MOVE(0x5c, y);
        uint16_t u = units;
        do PUT(ep_ds_byte(g, u++));
        while (ep_ds_byte(g, u) != ' ');
        PUT(1);
        PUT(0x0b);
        uint8_t t[12];
        int n = price_text(t, f->prices[2 * k]);
        MOVE((uint16_t)(0x98 - ep_text_width(t)), y);
        if (!last)
            for (int i = 0; i < n; i++) PUT(t[i]);
        n = price_text(t, f->prices[2 * k + 1]);
        MOVE((uint16_t)(0xc5 - ep_text_width(t)), y);
        for (int i = 0; i < n; i++) PUT(t[i]);
        uint8_t offer = cargo[2 * k + 1];
        if (!g->cmdr.b[EP_CMDR_MARKET_DRAWN]) { /* 8f5a: what is on offer, once per arrival */
            uint16_t r = market_random(g);
            int8_t q = (int8_t)((r & 0x1f) - 7);
            offer = q < 0 ? 0 : (uint8_t)(q ^ (r >> 8 & 3));
        }
        cargo[2 * k + 1] = offer;
        if (!last) {
            if (!offer) {
                MOVE(0xdf, y);
                PUT('-');
            } else {
                n = count_text(t, offer);
                MOVE((uint16_t)(0xe7 - ep_text_width(t)), y);
                for (int i = 0; i < n; i++) PUT(t[i]);
                u = units;
                do PUT(ep_ds_byte(g, u++));
                while (ep_ds_byte(g, u) != ' ');
            }
        }
        PUT(1);
        PUT(0x0e);
        if (!cargo[2 * k]) {
            MOVE(0x121, y);
            PUT('-');
        } else {
            n = count_text(t, cargo[2 * k]);
            MOVE((uint16_t)(0x129 - ep_text_width(t)), y);
            for (int i = 0; i < n; i++) PUT(t[i]);
            u = units;
            do PUT(ep_ds_byte(g, u++));
            while (ep_ds_byte(g, u) != ' ');
        }
        PUT(0);
        while (ep_ds_byte(g, units) != ' ') units++;
        units++;
        y = (uint16_t)(y + 8);
    }
#undef MOVE
#undef PUT
    g->cmdr.b[EP_CMDR_MARKET_DRAWN] = 1;
}

/* 90d7: the cash line under the list */
static void cash_line(ep_game *g)
{
    ep_render_rect(&g->render, 0, 0x10, 0xab, 0x120, 8);
    text_at(g, 0x10, 0xab, 0x0e, 0x8e32);
    g->f.list_busy = 0;
    uint16_t a = 0x82fb;
    while (ep_ds_byte(g, a) == ' ') a++;
    text_at(g, 0x37, 0xab, 0x0f, a);
    g->f.list_row = 0xff;
}

void ep_market_screen(ep_game *g)
{
    ep_flight *f = &g->f;
    f->note_ticks = 0;
    f->screen_bits = 0;
    f->screen_flag = 0;
    frame(g);
    /* the system's name and MARKET PRICES (built at ds:a410) */
    int n = 0;
    uint8_t c;
    uint16_t a = 0x831f;
    do f->rows[n++] = c = ep_ds_byte(g, a++);
    while (c);
    f->rows[n - 1] = ' ';
    a = 0xad1d;
    do f->rows[n++] = c = ep_ds_byte(g, a++);
    while (c);
    title(g, 0xa0, 0, 0x0f, f->rows, n);
    ep_market_rows(g);
    text_header(g, 0xacb4);
    if (f->screen != 1) { /* in flight: just the prices */
        f->screen = 2;
        ep_list_open(g, 0x000a, 0x000a, 0x11, 0, 0xa410, 8, 0x1d, 0x130);
        f->idle = EP_IDLE_PLAIN;
        return;
    }
    ep_list_open(g, 0x000a, 0x040f, 0x11, 0, 0xa410, 8, 0x1d, 0x130);
    cash_line(g); /* 90d7 (no note is up: 9048 cleared the time) */
    f->idle = EP_IDLE_MARKET;
}

/* a note on the cash line for 100 ticks (974e) */
static void note(ep_game *g, uint16_t text)
{
    ep_render_rect(&g->render, 0, 0x10, 0xab, 0x120, 8);
    uint8_t t[64];
    int n = ep_ds_text(g, text, t, sizeof t);
    ep_pen(&g->render, (int16_t)(0xa0 - (ep_text_width(t) >> 1)), 0xab, 0x0c);
    ep_text(&g->render, t, n, 0);
    g->f.note_ticks = 0x64;
    g->f.list_busy = 1;
}

static void market_done(ep_game *g)
{
    ep_market_rows(g);
    ep_list_poll(g, 2);
    g->f.note_ticks = 0;
    g->f.list_busy = 1;
}

void ep_market_buy(ep_game *g)
{
    uint16_t r = ep_trade_buy(g, g->f.list_row == 0xff ? -1 : g->f.list_row);
    if (r == EP_TRADE_NOTHING) return;
    if (r != EP_TRADE_OK) {
        note(g, r);
        return;
    }
    market_done(g);
}

void ep_market_sell(ep_game *g)
{
    if (ep_trade_sell(g, g->f.list_row == 0xff ? -1 : g->f.list_row) != EP_TRADE_OK) return;
    market_done(g);
}

/* ---- equipment ---- */

static uint8_t *owned(ep_game *g, int row) { return &g->cmdr.b[EP_CMDR_FUEL + row]; }

/* 8df7: the laser type of the row (pulse 4, beam 5, mining 12, military 13), else 4 */
static int laser_row(ep_game *g, int row)
{
    static const int rows[4] = { 4, 5, 12, 13 };
    int k = 0;
    while (k < 3 && rows[k] != row) k++;
    g->f.laser_kind = (uint8_t)k;
    return rows[k] == row;
}

void ep_equipment_rows(ep_game *g)
{
    ep_flight *f = &g->f;
    const uint8_t *cur = &g->cmdr.b[EP_CMDR_CURRENT];
    uint8_t *o = f->rows;
    int at = 0;
#define PUT(b)     (at < (int)sizeof f->rows ? (void)(o[at++] = (uint8_t)(b)) : (void)0)
#define MOVE(x, y) (PUT(2), PUT(x), PUT((x) >> 8), PUT(y), PUT((y) >> 8))
    uint16_t rec = 0x8bef, y = 0x14;
    uint8_t tech = (uint8_t)(cur[EP_SYSREC_TECH] + 1);
    int k = 0;
    while (k < 14 && tech >= ep_ds_byte(g, rec)) {
        rec++;
        uint8_t c;
        while ((c = ep_ds_byte(g, rec++)) != 0) PUT(c);
        int16_t a = (int8_t)ep_ds_byte(g, rec), b = (int8_t)ep_ds_byte(g, (uint16_t)(rec + 1));
        uint16_t price = (uint16_t)(a * (int8_t)cur[EP_SYSREC_GOVERNMENT] +
                                    b * (int8_t)cur[EP_SYSREC_ECONOMY] + ep_ds_word(g, (uint16_t)(rec + 2)));
        rec = (uint16_t)(rec + 4);
        f->prices[2 * k] = price;
        /* the original clears the selling price with [bx+2] instead of [bx+8d0c]: it zeroes
           ds:0002 + 4k (unused) and the old selling price stays */
        uint16_t back = k && *owned(g, k) ? price : 0; /* ds:8d08 */
        uint8_t t[12];
        int n = price_text(t, price);
        MOVE((uint16_t)(0xd2 - ep_text_width(t)), y);
        for (int i = 0; i < n; i++) PUT(t[i]);
        uint16_t sell = ep_sell_price(back);
        if (sell) {
            f->prices[2 * k + 1] = sell;
            n = price_text(t, sell);
            MOVE((uint16_t)(0x122 - ep_text_width(t)), y);
            for (int i = 0; i < n; i++) PUT(t[i]);
        }
        PUT(0);
        k++;
        y = (uint16_t)(y + 8);
    }
#undef MOVE
#undef PUT
    f->list_count = (uint8_t)k;
}

static void equipment_list(ep_game *g, uint8_t cursor)
{
    ep_list_open(g, 0x040e, 0x0c0f, g->f.list_count, cursor, 0xa410, 8, 0x14, 0x130);
}

/* 92a8: the picture and the cash, when no note is up */
static void equipment_cash(ep_game *g)
{
    ep_status_picture(g);
    uint16_t a = 0x82fb;
    while (ep_ds_byte(g, a) == ' ') a++;
    uint8_t t[96];
    int n = ep_ds_text(g, a, t, sizeof t);
    ep_pen(&g->render, (int16_t)(0xa0 - (ep_text_width(t) >> 1)), 0x87, 0x0f);
    ep_text(&g->render, t, n, 0);
    g->f.list_row = 0xff;
    g->f.list_busy = 0;
}

void ep_equipment_screen(ep_game *g)
{
    ep_flight *f = &g->f;
    f->note_ticks = 0;
    f->screen_flag = 0;
    f->screen_bits = 0;
    frame(g);
    uint8_t t[96];
    int n = ep_ds_text(g, 0x88fa, t, sizeof t);
    title(g, 0xa0, 0, 0x0f, t, n);
    ep_equipment_rows(g);
    ep_render_rect(&g->render, 4, 8, 0x0a, 0x130, 0x7a);
    text_header(g, 0xadef);
    equipment_list(g, 0);
    equipment_cash(g);
    f->idle = EP_IDLE_EQUIP;
}

/* 9403: a note in a box for 100 ticks */
static void box_note(ep_game *g, uint16_t text)
{
    ep_render_rect(&g->render, 1, 0x41, 0x96, 0xbe, 0x10);
    uint8_t t[64];
    int n = ep_ds_text(g, text, t, sizeof t);
    ep_pen(&g->render, (int16_t)(0xa0 - (ep_text_width(t) >> 1)), 0x9a, 0x0f);
    ep_text(&g->render, t, n, 0);
    g->f.note_ticks = 0x64;
    g->f.list_busy = 1;
}

/* the mounts' names into the rows, those that `take` says; returns how many */
static int mount_names(ep_game *g, int sell)
{
    uint8_t *o = g->f.rows;
    int at = 0, count = 0;
    uint16_t name = 0xaddb;
    uint8_t mounts = g->cmdr.b[EP_CMDR_LASERS] & 0x0f, types = g->cmdr.b[EP_CMDR_LASER_TYPES];
    for (int m = 0; m < 4; m++) {
        int bit = mounts >> m & 1;
        int take = sell ? bit && (types >> (2 * m) & 3) == g->f.laser_kind : !bit;
        uint8_t c;
        if (take) {
            count++;
            do o[at++] = c = ep_ds_byte(g, name++);
            while (c);
        } else {
            while (ep_ds_byte(g, name++)) {}
        }
    }
    return count;
}

/* the box asking which mount (94bf, 964c) */
static int ask_mount(ep_game *g, int count, uint16_t text, uint8_t step)
{
    ep_render_rect(&g->render, 1, 0x50, 0x1e, 0xa0, 0x38);
    uint8_t t[64];
    int n = ep_ds_text(g, text, t, sizeof t);
    ep_pen(&g->render, (int16_t)(0xa0 - (ep_text_width(t) >> 1)), 0x22, 0x0f);
    ep_text(&g->render, t, n, 0);
    ep_list_open(g, 0x010b, 0x090f, (uint8_t)(count | 0x80), 0, 0xa410, 0x6e, 0x32, 0x64);
    return wait(g, step, EP_WAIT_LIST);
}

/* the laser into the n-th free mount (0 = the first) */
static void fit_mount(ep_game *g, int n)
{
    uint8_t *c = g->cmdr.b;
    for (int m = 0; m < 8; m++) {
        if (c[EP_CMDR_LASERS] >> m & 1) continue;
        if (n-- > 0) continue;
        c[EP_CMDR_LASERS] |= (uint8_t)(1u << m);
        c[EP_CMDR_LASER_TYPES] = (uint8_t)((c[EP_CMDR_LASER_TYPES] & ~(3u << (2 * m))) |
                                           (unsigned)(g->f.laser_kind & 3) << (2 * m));
        return;
    }
}

/* the n-th mount carrying this laser loses it */
static void unfit_mount(ep_game *g, int n)
{
    uint8_t *c = g->cmdr.b;
    for (int m = 0; m < 8; m++) {
        if (!(c[EP_CMDR_LASERS] >> m & 1) || (c[EP_CMDR_LASER_TYPES] >> (2 * m) & 3) != g->f.laser_kind)
            continue;
        if (n-- > 0) continue;
        c[EP_CMDR_LASERS] &= (uint8_t)~(1u << m);
        return;
    }
}

/* 92d9..92f9: after the commands, the cursor's row, the bar's bits, rebuilding the list */
static void equipment_tail(ep_game *g)
{
    ep_flight *f = &g->f;
    ep_list_poll(g, 0);
    uint8_t row = f->menu[1];
    f->list_row = row;
    f->screen_bits = (uint8_t)(4 | (*owned(g, row) && row ? 8 : 0));
    for (;;) {
        if (f->list_busy == 2) {
            f->list_busy--;
            ep_equipment_rows(g);
            equipment_list(g, f->list_keep);
            continue;
        }
        if (f->list_busy && !f->note_ticks) equipment_cash(g); /* 92a8 */
        return;
    }
}

static int mount_key(ep_game *g, uint8_t key)
{
    g->f.last_cmd_key = key; /* 03b6 */
    if (ep_list_poll(g, 0) != 0x0d) return EP_WAIT_LIST;
    ep_render_rect(&g->render, 4, 0x50, 0x1e, 0xa0, 0x38);
    if (g->f.station_step == ST_MOUNT_BUY)
        fit_mount(g, g->f.menu[1]);
    else
        unfit_mount(g, g->f.menu[1]);
    g->f.station_step = ST_NONE;
    if (g->f.idle == EP_IDLE_EQUIP) equipment_tail(g); /* back in the pass the dialog interrupted */
    return EP_WAIT_NONE;
}

int ep_equipment_buy(ep_game *g)
{
    ep_flight *f = &g->f;
    uint8_t *c = g->cmdr.b;
    int row = f->list_row;
    if (row == 0xff) return EP_WAIT_NONE;
    if (row == 0) { /* fuel */
        if (f->mission == 1) {
            box_note(g, 0x8dad);
            return EP_WAIT_NONE;
        }
        if (c[EP_CMDR_FUEL] >= 0xfb) {
            box_note(g, 0xadaa);
            return EP_WAIT_NONE;
        }
        uint32_t p = (uint32_t)(uint16_t)((0xff - c[EP_CMDR_FUEL]) * 7) * f->prices[0];
        if (ep_pay(g, (uint16_t)(p >> 8))) {
            c[EP_CMDR_FUEL] = 0xff;
            box_note(g, 0xadc3);
            return EP_WAIT_NONE;
        }
        uint16_t lo = (uint16_t)ep_commander_cash(&g->cmdr);
        if (!lo) {
            box_note(g, 0xad50);
            return EP_WAIT_NONE;
        }
        uint32_t q = ((uint32_t)lo << 8) / f->prices[0];
        c[EP_CMDR_FUEL] = (uint8_t)(c[EP_CMDR_FUEL] + (uint8_t)((q & 0xffff) / 7));
        ep_pay(g, lo);
        f->list_busy = 1;
        f->note_ticks = 0;
        return EP_WAIT_NONE;
    }
    /* 93a9: what is fitted already, then lasers' own checks (8df7 only where the original asks) */
    int laser;
    if (*owned(g, row) && row == 1) {
        if (c[EP_CMDR_EQUIPMENT] == 4) {
            box_note(g, 0xad64);
            return EP_WAIT_NONE;
        }
        laser = laser_row(g, row);
    } else if (*owned(g, row)) {
        if (!(laser = laser_row(g, row))) {
            box_note(g, 0x8d5a);
            return EP_WAIT_NONE;
        }
    } else {
        laser = laser_row(g, row);
    }
    if (laser) {
        if (c[EP_CMDR_LASERS] == 0x0f) {
            box_note(g, 0x92e6);
            return EP_WAIT_NONE;
        }
        if (row == 12 && c[EP_CMDR_EQUIPMENT + 5] != 1) {
            box_note(g, 0x8d7a);
            return EP_WAIT_NONE;
        }
    }
    if (!ep_pay(g, f->prices[2 * row])) {
        box_note(g, 0xad50);
        return EP_WAIT_NONE;
    }
    f->list_busy = 1;
    f->note_ticks = 0;
    (*owned(g, row))++;
    if (!laser_row(g, row)) {
        ep_equipment_rows(g);
        ep_list_poll(g, 2);
        return EP_WAIT_NONE;
    }
    f->list_busy = 2;
    f->list_keep = (uint8_t)row;
    int n = mount_names(g, 0);
    if (n == 1) {
        fit_mount(g, 0);
        return EP_WAIT_NONE;
    }
    return ask_mount(g, n, 0xad7c, ST_MOUNT_BUY);
}

int ep_equipment_sell(ep_game *g)
{
    ep_flight *f = &g->f;
    int row = f->list_row;
    if (row == 0xff || !*owned(g, row)) return EP_WAIT_NONE;
    if (row == 2 && g->cmdr.b[EP_CMDR_CARGO_USED] > 0x14) {
        box_note(g, 0x8d6a);
        return EP_WAIT_NONE;
    }
    if (row == 6 && g->cmdr.b[EP_CMDR_EQUIPMENT + 11]) {
        box_note(g, 0x8d94);
        return EP_WAIT_NONE;
    }
    ep_commander_set_cash(&g->cmdr, ep_commander_cash(&g->cmdr) + f->prices[2 * row + 1]);
    ep_cash_text(&g->cmdr);
    f->list_busy = 1;
    f->note_ticks = 0;
    (*owned(g, row))--;
    if (!laser_row(g, row)) {
        ep_equipment_rows(g);
        ep_list_poll(g, 2);
        return EP_WAIT_NONE;
    }
    f->list_busy = 2;
    f->list_keep = (uint8_t)row;
    int n = mount_names(g, 1);
    if (n == 1) {
        unfit_mount(g, 0);
        return EP_WAIT_NONE;
    }
    return ask_mount(g, n, 0xad95, ST_MOUNT_SELL);
}

/* ---- the charts ---- */

static void line(ep_game *g, uint8_t colour, int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    ep_render_line(&g->render, colour, x0, y0, x1, y1); /* 261b */
}

/* 291b: a pixel (x < 130h, y < 7ch) */
static void pixel(ep_game *g, uint8_t colour, int16_t x, int16_t y)
{
    if ((uint16_t)x < 0x130 && (uint16_t)y < 0x7c) ep_render_pixel(&g->render, colour, x, y);
}

/* the chart's frame and edges (5b28.., 58ce..) */
static void chart_frame(ep_game *g, uint16_t title_addr)
{
    frame_kind(g, 1);
    uint8_t t[40];
    int n = ep_ds_text(g, title_addr, t, sizeof t);
    title(g, 0xa0, 0, 0x0f, t, n);
    ep_render_rect(&g->render, 0, 0x24, 0x8d, 0xc8, 0x14);
    ep_render_rect(&g->render, 8, 0x1e, 9, 2, 0x7c);
    ep_render_rect(&g->render, 8, 0x120, 9, 2, 0x7c);
    ep_render_rect(&g->render, 8, 0x1e, 0x85, 0x104, 2);
}

static uint8_t *chart_b(ep_game *g, uint16_t addr) { return &g->f.chart[addr - 0x5604]; }

static uint16_t chart_w(ep_game *g, uint16_t addr)
{
    return (uint16_t)(chart_b(g, addr)[0] | chart_b(g, addr)[1] << 8);
}

static void chart_set_w(ep_game *g, uint16_t addr, uint16_t v)
{
    chart_b(g, addr)[0] = (uint8_t)v;
    chart_b(g, addr)[1] = (uint8_t)(v >> 8);
}

/* 6318: the box (x range dx, y range bx) meets the record at `at` */
static int chart_overlap(ep_game *g, uint16_t at, uint16_t dx, uint16_t bx)
{
    const uint8_t *r = chart_b(g, at);
    return r[1] >= (uint8_t)dx && (uint8_t)(dx >> 8) >= r[0] && r[3] >= (uint8_t)bx &&
           (uint8_t)(bx >> 8) >= r[2];
}

/* 6275: each name placed where it hides nothing, moved up and down by growing steps */
static void chart_labels(ep_game *g)
{
    uint8_t n = *chart_b(g, 0x5604);
    if (!n) return;
    uint16_t label = 0x5809;
    *chart_b(g, 0x5a0a) = n;
    for (; *chart_b(g, 0x5a0a); (*chart_b(g, 0x5a0a))--) {
        uint16_t dx = ep_ds_word(g, label), bx = ep_ds_word(g, (uint16_t)(label + 2));
        label = (uint16_t)(label + 4);
        uint8_t *step_at = chart_b(g, 0x5a09), step = 0;
        *step_at = 0;
        for (;;) {
            int hit = 0;
            uint16_t at = 0x5607;
            for (int k = 0; k < *chart_b(g, 0x5604); k++, at = (uint16_t)(at + 8))
                if (chart_overlap(g, at, dx, bx)) {
                    hit = 1;
                    break;
                }
            if (!hit) break;
            int placed = 0; /* 62ec */
            for (;;) {
                step++;
                if (step >= 0x29) { /* give up: put it there anyway */
                    placed = 1;
                    break;
                }
                uint8_t lo = (uint8_t)bx, hi = (uint8_t)(bx >> 8);
                if (step & 1) {
                    hi = (uint8_t)(hi - step);
                    lo = (uint8_t)(lo - step);
                    bx = (uint16_t)(hi << 8 | lo);
                    if (lo & 0x80) continue;
                } else {
                    lo = (uint8_t)(lo + step);
                    hi = (uint8_t)(hi + step);
                    bx = (uint16_t)(hi << 8 | lo);
                    if (hi >= 0x7c) continue;
                }
                break;
            }
            *step_at = step;
            if (placed) break;
        }
        *step_at = step;
        uint16_t rec = chart_w(g, 0x5605); /* 62c1 */
        chart_set_w(g, rec, dx);
        chart_set_w(g, (uint16_t)(rec + 2), bx);
        chart_set_w(g, (uint16_t)(rec + 4), label);
        chart_b(g, rec)[7] = 1;
        chart_set_w(g, 0x5605, (uint16_t)(rec + 8));
        (*chart_b(g, 0x5604))++;
        while (ep_ds_byte(g, label)) label++;
        label++;
    }
}

/* 621f: the symbols and the names */
static void chart_symbols(ep_game *g)
{
    uint16_t at = 0x5607;
    for (int k = 0; k < *chart_b(g, 0x5604); k++, at = (uint16_t)(at + 8)) {
        const uint8_t *r = chart_b(g, at);
        if (!r[7]) {
            sprite(g, (uint8_t)((r[6] >> 1) + 0x50), (int16_t)(r[4] + 0x1d), (int16_t)(r[5] + 6));
        } else {
            uint8_t t[16];
            int n = ep_ds_text(g, chart_w(g, (uint16_t)(at + 4)), t, sizeof t);
            ep_pen(&g->render, (int16_t)(r[0] + 0x20), (int16_t)(r[2] + 9), 0x0e);
            ep_text(&g->render, t, n, 0);
        }
    }
}

/* 5bab..5c75: the systems in the window, a circle each and a label to place */
static void chart_local_systems(ep_game *g)
{
    *chart_b(g, 0x5604) = 0;
    chart_set_w(g, 0x5605, 0x5607);
    chart_set_w(g, 0x5807, 0x5809);
    g->seed = ep_galaxy_seed(g->cmdr.b[EP_CMDR_GALAXY]);
    for (int n = 0; n < 256; n++) {
        int16_t dx = (int16_t)((g->seed.w[1] >> 8) - g->cmdr.b[EP_CMDR_CHART_CENTRE]);
        int16_t dy = (int16_t)((g->seed.w[0] >> 9) - g->cmdr.b[EP_CMDR_CHART_CENTRE + 1]);
        if ((dx < 0 ? -dx : dx) >= 0x14 || (dy < 0 ? -dy : dy) >= 0x11) { /* 6124 */
            for (int k = 0; k < 4; k++) ep_twist(&g->seed);
            continue;
        }
        uint16_t y = (uint16_t)(3 * dy + (dy >> 1) + 0x40), x = (uint16_t)(3 * dx + (dx >> 1) + 0x50);
        if (y >= 0x7c) y = 0x7b;
        uint16_t rec = chart_w(g, 0x5605);
        uint8_t *r = chart_b(g, rec);
        uint8_t size = (uint8_t)((g->seed.w[0] >> 8 & 1) * 2 + 4), half = (uint8_t)(size >> 1);
        r[4] = (uint8_t)x;
        r[5] = (uint8_t)y;
        r[6] = size;
        r[7] = 0;
        r[0] = (uint8_t)((uint8_t)x - half);
        r[1] = (uint8_t)((uint8_t)x + half);
        r[2] = (uint8_t)((uint8_t)y - half);
        r[3] = (uint8_t)((uint8_t)y + half);
        chart_set_w(g, 0x5605, (uint16_t)(rec + 8));
        (*chart_b(g, 0x5604))++;
        uint8_t *name = &g->cmdr.b[EP_CMDR_SELECTED + EP_SYSREC_NAME];
        ep_planet_name(&g->seed, name); /* 6130 (the four twists) */
        uint8_t len = name[9];
        uint8_t lx = (uint8_t)((uint8_t)x + 7);
        uint16_t lab = chart_w(g, 0x5807);
        chart_set_w(g, lab, (uint16_t)(((uint8_t)(lx + ep_text_width(name)) << 8) | lx));
        uint8_t top = (uint8_t)y, bot;
        int8_t t = (int8_t)(top - 3);
        bot = top;
        while (t < 0) {
            bot++;
            t++;
        }
        top = (uint8_t)t;
        bot = (uint8_t)(bot + 4);
        while (bot >= 0x7c) {
            top--;
            bot--;
        }
        chart_set_w(g, (uint16_t)(lab + 2), (uint16_t)(bot << 8 | top));
        uint16_t d = (uint16_t)(lab + 4);
        for (int k = 0; k < len; k++) *chart_b(g, d++) = name[k];
        *chart_b(g, d++) = 0;
        chart_set_w(g, 0x5807, d);
    }
    chart_labels(g);
}

/* 5d71 + the cursor moved by the steering (at most 4 a pass) */
static void chart_cursor(ep_game *g, int galactic)
{
    uint16_t s = ep_steering(g);
    int8_t al = (int8_t)s, ah = (int8_t)(s >> 8);
    if (al > 4) al = 4;
    if (al < -4) al = -4;
    if (ah > 4) ah = 4;
    if (ah < -4) ah = -4;
    g->f.roll = al;
    g->f.pitch = ah;
    ah = (int8_t)-ah;
    uint8_t *c = &g->cmdr.b[EP_CMDR_CURSOR];
    unsigned x = c[0] + (unsigned)(uint8_t)al;
    if (al >= 0)
        c[0] = (uint8_t)(x > 0xff ? 0xff : x);
    else
        c[0] = (uint8_t)(x > 0xff ? x : 0);
    unsigned y = c[1] + (unsigned)(uint8_t)ah;
    if (!galactic) {
        if (ah >= 0)
            c[1] = (uint8_t)((uint8_t)y >= 0x7c ? 0x7b : y);
        else
            c[1] = (uint8_t)(y > 0xff ? y : 0);
    } else {
        if (ah >= 0)
            c[1] = (uint8_t)((uint8_t)y >= 0x7e ? 0x7d : y);
        else
            c[1] = (uint8_t)((int8_t)(uint8_t)y < 2 ? 2 : y);
    }
}

/* a pass of the short-range chart (5c80..5d6e) */
static void chart_local_pass(ep_game *g)
{
    ep_render_rect(&g->render, 0, 0x20, 9, 0x100, 0x7c);
    uint16_t r = (uint16_t)(((g->cmdr.b[EP_CMDR_FUEL] >> 1) + 1) >> 1);
    int n0 = g->circles.n;
    ep_draw_circle(&g->rng, 0x68, 0x40, (int16_t)r, g->f.circle_mask, 0, g->f.video == 2, &g->circles);
    ep_render_spans(&g->render, 4, n0, g->circles.n - n0);
    ep_render_rect(&g->render, 0, 0x70, 0x38, 1, 0x23);
    ep_render_rect(&g->render, 0, 0x5f, 0x49, 0x23, 1);
    chart_symbols(g);
    int16_t x = (int16_t)(g->cmdr.b[EP_CMDR_CURSOR] + 0x18), y = g->cmdr.b[EP_CMDR_CURSOR + 1];
    line(g, 0x0a, (int16_t)(x - 5), y, (int16_t)(x + 6), y);
    line(g, 0x0a, x, (int16_t)(y - 5), x, (int16_t)(y + 6));
    pixel(g, 0, x, y);
    chart_cursor(g, 0);
}

/* a pass of the galactic chart (595a..5abd) */
static void chart_galaxy_pass(ep_game *g)
{
    ep_render_rect(&g->render, 0, 0x20, 9, 0x100, 0x7c);
    g->seed = ep_galaxy_seed(g->cmdr.b[EP_CMDR_GALAXY]);
    uint16_t r = (uint16_t)(((g->cmdr.b[EP_CMDR_FUEL] >> 3) + 3) >> 1);
    int16_t cx = (int16_t)(g->cmdr.b[EP_CMDR_CHART_CENTRE] + 0x18),
            cy = (int16_t)(g->cmdr.b[EP_CMDR_CHART_CENTRE + 1] - 2);
    int n0 = g->circles.n;
    ep_draw_circle(&g->rng, cx, cy, (int16_t)r, g->f.circle_mask, 0, g->f.video == 2, &g->circles);
    ep_render_spans(&g->render, 4, n0, g->circles.n - n0);
    /* the cross over the present system: its y takes x + 18h's high byte (mov bh,ah) */
    int16_t ly = (int16_t)(cy + (cx & 0xff00));
    line(g, 0, cx, (int16_t)(ly - 0x11), cx, (int16_t)(ly + 0x12));
    line(g, 0, (int16_t)(cx - 0x11), ly, (int16_t)(cx + 0x12), ly);
    int16_t x = (int16_t)(g->cmdr.b[EP_CMDR_CURSOR] + 0x18), y = (int16_t)(g->cmdr.b[EP_CMDR_CURSOR + 1] - 2);
    line(g, 0x0a, (int16_t)(x - 5), y, (int16_t)(x + 6), y);
    line(g, 0x0a, x, (int16_t)(y - 5), x, (int16_t)(y + 6));
    pixel(g, 0, x, y);
    for (int n = 0; n < 256; n++) {
        int16_t sx = (int16_t)((g->seed.w[1] >> 8) + 0x18), sy = (int16_t)(g->seed.w[0] >> 9);
        if (sy > 1) {
            sy = (int16_t)(sy - 2);
            if (sy >= 0x7c) sy = 0x7b;
        } else {
            sy = 0;
        }
        pixel(g, 7, sx, sy);
        for (int k = 0; k < 4; k++) ep_twist(&g->seed);
    }
    chart_cursor(g, 1);
}

int ep_chart_screen(ep_game *g)
{
    ep_flight *f = &g->f;
    uint8_t *c = g->cmdr.b;
    if (f->screen != 1 && f->hyperspace && f->hyperspace != 1) { /* 5b09: no chart in witchspace */
        f->message = 0x5a0b;
        f->message_time = 0x23;
        return f->other_screen ? ep_view_command(g) : EP_CMD_STAY; /* a1cf: back to the view */
    }
    if (f->screen == 0) f->screen = 2;
    if (f->other_screen == 1)
        f->chart_kind = (uint8_t)~f->chart_kind;
    else
        f->chart_kind = 0;
    f->screen_flag = 1;
    f->screen_bits = 0;
    if (!f->chart_kind) { /* 5b20: short-range */
        chart_frame(g, 0x55c5);
        c[EP_CMDR_CURSOR] = c[EP_CMDR_CURSOR + 2];
        c[EP_CMDR_CURSOR + 1] = c[EP_CMDR_CURSOR + 3];
        c[EP_CMDR_ZOOM] = 1;
        chart_local_systems(g);
        f->idle = EP_IDLE_LOCAL;
    } else { /* 58c6: galactic */
        g->seed = ep_system_seed(c[EP_CMDR_GALAXY], c[EP_CMDR_CURRENT + EP_SYSREC_INDEX]);
        c[EP_CMDR_CHART_CENTRE] = (uint8_t)(g->seed.w[1] >> 8);
        c[EP_CMDR_CHART_CENTRE + 1] = (uint8_t)(g->seed.w[0] >> 9);
        f->chart_digit = (uint8_t)(c[EP_CMDR_GALAXY] + '1');
        chart_frame(g, 0x55d7);
        c[EP_CMDR_CURSOR] = c[EP_CMDR_CURSOR + 4];
        c[EP_CMDR_CURSOR + 1] = c[EP_CMDR_CURSOR + 5];
        c[EP_CMDR_ZOOM] = 0;
        f->idle = EP_IDLE_GALAXY;
    }
    return EP_CMD_SCREEN;
}

void ep_chart_find(ep_game *g)
{
    uint8_t *c = g->cmdr.b;
    ep_render_rect(&g->render, 0, 0x24, 0x8d, 0xc8, 0x14);
    ep_find_nearest(g);
    int k = g->f.chart_kind ? 4 : 2;
    c[EP_CMDR_CURSOR + k] = c[EP_CMDR_CURSOR];
    c[EP_CMDR_CURSOR + k + 1] = c[EP_CMDR_CURSOR + 1];
    ep_system_distance(g);
    uint16_t v =
        (uint16_t)(c[EP_CMDR_SELECTED + EP_SYSREC_DIST] | c[EP_CMDR_SELECTED + EP_SYSREC_DIST + 1] << 8);
    digits(g->dist_text, v, 3);
    uint8_t *d = g->f.dist_shown; /* ds:5550.. */
    d[0] = g->dist_text[1];
    d[1] = g->dist_text[2];
    d[2] = g->dist_text[3];
    d[4] = g->dist_text[4];
    text_header(g, 0x553f);
    ep_planet_name(&g->seed, &c[EP_CMDR_SELECTED + EP_SYSREC_NAME]);
    text_at(g, 0x24, 0x8d, 0x0e, 0x8338);
}

void ep_chart_home(ep_game *g)
{
    uint8_t *c = g->cmdr.b;
    if (!g->f.chart_kind) {
        c[EP_CMDR_CURSOR] = 0x50;
        c[EP_CMDR_CURSOR + 1] = 0x40;
    } else {
        c[EP_CMDR_CURSOR] = c[EP_CMDR_CHART_CENTRE];
        c[EP_CMDR_CURSOR + 1] = c[EP_CMDR_CHART_CENTRE + 1];
    }
    ep_chart_find(g);
}

/* ---- DATA ON ---- */

static uint8_t *data_b(ep_game *g, uint16_t addr) { return &g->f.data_text[addr - 0x8900]; }

/* 3a40: the planet's picture from the table at ds:2680 (n entries on, wrapping at ffh) */
static void data_picture(ep_game *g, uint8_t n)
{
    uint16_t at = 0x2680;
    for (; n; n--) {
        if (ep_ds_byte(g, at) == 0xff) at = 0x2680;
        at++;
        while (ep_ds_byte(g, at)) at = (uint16_t)(at + 3);
        at++;
    }
    ep_render_rect(&g->render, 8, 0xe0, 0x67, 0x52, 0x2a);
    ep_render_rect(&g->render, 7, 0xdf, 0x66, 0x52, 0x2a);
    uint8_t colour = ep_ds_byte(g, at++);
    if (colour == 0xff) {
        at = 0x2680;
        colour = ep_ds_byte(g, at++);
    }
    if (g->f.video == 2) { /* 28ab: MCGA's own colour for it */
        uint16_t m = 0x28ab;
        while (ep_ds_byte(g, m) != colour) m = (uint16_t)(m + 2);
        colour = ep_ds_byte(g, (uint16_t)(m + 1));
    }
    ep_render_rect(&g->render, colour, 0xe0, 0x67, 0x50, 0x28);
    uint8_t p;
    while ((p = ep_ds_byte(g, at++)) != 0) {
        uint16_t off = ep_ds_word(g, at);
        at = (uint16_t)(at + 2);
        sprite(g, (uint8_t)(0x5e + p), (int16_t)(0xe0 + (uint8_t)off), (int16_t)(0x67 + (off >> 8)));
    }
}

/* 632b: the description, word by word, wrapped at 132h */
static void data_description(ep_game *g)
{
    uint8_t *d = g->f.description, *c = g->cmdr.b;
    memset(d, 0, 0x100);
    uint16_t r0 = (uint16_t)(c[EP_CMDR_SELECTED + 0x19] | c[EP_CMDR_SELECTED + 0x1a] << 8);
    uint16_t r1 = (uint16_t)(c[EP_CMDR_SELECTED + 0x1b] | c[EP_CMDR_SELECTED + 0x1c] << 8);
    ep_desc_io io = {
        d,        ep_ds_byte(g, 0x5a3d), &r0,           &r1, &c[EP_CMDR_SELECTED + EP_SYSREC_NAME],
        &g->seed, &g->f.desc_caps,       g->f.desc_save
    };
    ep_describe(&io);
    c[EP_CMDR_SELECTED + 0x19] = (uint8_t)r0;
    c[EP_CMDR_SELECTED + 0x1a] = (uint8_t)(r0 >> 8);
    c[EP_CMDR_SELECTED + 0x1b] = (uint8_t)r1;
    c[EP_CMDR_SELECTED + 0x1c] = (uint8_t)(r1 >> 8);
    int16_t x = 0x0e, y = 0x95;
    int at = 0;
    for (;;) {
        int end = at;
        while (end < 0x100 && d[end] && d[end] != ' ') end++;
        int last = end >= 0x100 || !d[end];
        if (!last) d[end] = 0;
        uint16_t w = ep_text_width(&d[at]);
        if ((uint16_t)(w + 5 + x) >= 0x132) {
            x = 0x0e;
            y = (int16_t)(y + 9);
        }
        ep_pen(&g->render, x, y, 0x0e);
        ep_text(&g->render, &d[at], end - at + 1, 0);
        x = g->render.pen_x;
        if (last) return;
        x = (int16_t)(x + 5);
        at = end + 1;
    }
}

void ep_data_screen(ep_game *g)
{
    ep_flight *f = &g->f;
    uint8_t *c = g->cmdr.b;
    f->screen_bits = 0;
    ep_select_system(g);
    if (f->screen_flag && !f->hyper_countdown) { /* from the chart: keep its cursor */
        int k = f->chart_kind ? 4 : 2;
        c[EP_CMDR_CURSOR + k] = c[EP_CMDR_CURSOR];
        c[EP_CMDR_CURSOR + k + 1] = c[EP_CMDR_CURSOR + 1];
    }
    f->screen_flag = 0;
    frame(g);
    uint8_t *name = &c[EP_CMDR_SELECTED + EP_SYSREC_NAME];
    name[name[9]] = 0; /* 8dd1 */
    int k = 0;
    do *data_b(g, (uint16_t)(0x890d + k)) = name[k];
    while (name[k++]);
    uint8_t t[64];
    int n = ep_ds_text(g, 0x8905, t, sizeof t);
    title(g, 0xa0, 0, 0x0f, t, n);
    /* 8ddd: the distance "xx.x" written backwards up to 8a94 */
    uint16_t src = 0x5566, dst = 0x8a94;
    *data_b(g, dst--) = ep_ds_byte(g, src--);
    *data_b(g, dst--) = '.';
    for (;;) {
        uint8_t ch = ep_ds_byte(g, src--);
        *data_b(g, dst) = ch;
        if (ch == ' ') break;
        dst--;
    }
    dst++;
    n = 0;
    uint8_t ch;
    while ((ch = *data_b(g, dst++)) != 0) t[n++] = ch;
    uint16_t a = 0x8a96;
    do t[n++] = ch = ep_ds_byte(g, a++);
    while (ch && n < 63);
    ep_pen(&g->render, 0x47, 0x1f, 0x0e);
    ep_text(&g->render, t, n, 0);
    text_header(g, 0x895d);
    text_at(g, 0xb1, 0x4d, 0x0f,
            ep_ds_word(g, (uint16_t)(0x894d + 2 * c[EP_CMDR_SELECTED + EP_SYSREC_ECONOMY])));
    text_header(g, 0x8a0d);
    text_at(g, 0xc4, 0x57, 0x0f,
            ep_ds_word(g, (uint16_t)(0x89fd + 2 * c[EP_CMDR_SELECTED + EP_SYSREC_GOVERNMENT])));
    text_header(g, 0x8a7b);
    uint8_t tech = (uint8_t)(c[EP_CMDR_SELECTED + EP_SYSREC_TECH] + 1);
    uint16_t tat = 0x8a8e;
    if (tech >= 10) {
        tech = (uint8_t)(tech - 10);
        tat = 0x8a8d;
        *data_b(g, 0x8a8d) = '1';
    }
    *data_b(g, 0x8a8e) = (uint8_t)(tech + '0');
    text_at(g, 0xc2, 0x6b, 0x0f, tat);
    sprite(g, 0x84, 0x18, 0x0f);
    uint8_t *pop = data_b(g, 0x8ab4);
    digits(pop, c[EP_CMDR_SELECTED + 0x10], 0);
    pop[0] = 0x0f;
    pop[1] = ' ';
    pop[2] = pop[3];
    pop[3] = '.';
    text_header(g, 0x8aa3);
    text_header(g, 0x8ac2);
    const uint8_t *sp = &c[EP_CMDR_SELECTED + 0x11];
    if (sp[0] == 0xff) {
        f->species_icon = 8;
        text_on(g, 0x8ac9);
    } else {
        text_on(g, ep_ds_word(g, (uint16_t)(0x8ada + 2 * sp[0])));
        text_on(g, ep_ds_word(g, (uint16_t)(0x8b00 + 2 * sp[1])));
        text_on(g, ep_ds_word(g, (uint16_t)(0x8b3b + 2 * sp[2])));
        f->species_icon = sp[3];
        text_on(g, ep_ds_word(g, (uint16_t)(0x8b76 + 2 * sp[3])));
    }
    uint16_t prod = (uint16_t)(c[EP_CMDR_SELECTED + 0x15] | c[EP_CMDR_SELECTED + 0x16] << 8);
    digits(data_b(g, 0x8bd8), prod, 0);
    text_header(g, 0x8bcd);
    g->render.pen_colour = 0x0f;
    text_on(g, *data_b(g, 0x8bd8) == '0' ? 0x8bd9 : 0x8bd8);
    uint16_t radius = (uint16_t)(c[EP_CMDR_SELECTED + 0x17] | c[EP_CMDR_SELECTED + 0x18] << 8);
    digits(data_b(g, 0x8be7), radius, 0);
    *data_b(g, 0x8be7) = ' ';
    text_header(g, 0x8be2);
    sprite(g, (uint8_t)(0x79 + f->species_icon), 0x30, 0x3d);
    data_description(g);
    ep_select_system(g);
    data_picture(g, (uint8_t)((uint8_t)g->seed.w[0] ^ (uint8_t)(g->seed.w[1] >> 8)));
    uint8_t v = (uint8_t)((uint8_t)(g->seed.w[1] >> 8) - (uint8_t)g->seed.w[0]); /* 3a28 */
    sprite(g, (uint8_t)(0x6d + (uint8_t)((v * 0x0c) >> 8)), 0xb0, 0x0b);
    static const int16_t arrows[8][4] = { { 0x33, 0x21, 0xa7, 0x21 },   { 0xa3, 0x1e, 0xa7, 0x22 },
                                          { 0xa3, 0x24, 0xa7, 0x20 },   { 0x101, 2, 0x101, 0x1b },
                                          { 0x101, 2, 0x105, 6 },       { 0x102, 2, 0xfe, 6 },
                                          { 0x101, 0x28, 0x101, 0x42 }, { 0x102, 0x41, 0xfe, 0x3d } };
    for (int j = 0; j < 8; j++) line(g, 0x0c, arrows[j][0], arrows[j][1], arrows[j][2], arrows[j][3]);
    line(g, 0x0c, 0x101, 0x41, 0x105, 0x3d);
    f->idle = EP_IDLE_PLAIN;
}

/* ---- typing a text (0d9d..0ed4) ---- */

static uint16_t entry_w(ep_game *g, uint16_t addr)
{
    return (uint16_t)(g->f.entry[addr - 0x9a2] | g->f.entry[addr - 0x9a1] << 8);
}

static uint8_t *entry_b(ep_game *g, uint16_t addr) { return &g->f.entry[addr - 0x9a2]; }

/* 0ebd: the cursor after the text */
static void entry_cursor(ep_game *g, uint8_t colour)
{
    ep_render_rect(&g->render, colour, (int16_t)(entry_w(g, 0x9c8) + 1), (int16_t)(entry_w(g, 0x9c6) + 6), 7,
                   2);
}

/* 0dda: the text and where it ends */
static void entry_text(ep_game *g)
{
    uint8_t t[16];
    int n = 0;
    while (n < 15 && (t[n] = *entry_b(g, (uint16_t)(0x9a4 + n))) != 0) n++;
    t[n++] = 0;
    uint16_t x = entry_w(g, 0x9c4);
    uint16_t end = (uint16_t)(x + ep_text_width(t));
    *entry_b(g, 0x9c8) = (uint8_t)end;
    *entry_b(g, 0x9c9) = (uint8_t)(end >> 8);
    ep_pen(&g->render, (int16_t)x, (int16_t)entry_w(g, 0x9c6), *entry_b(g, 0x9ca));
    ep_text(&g->render, t, n, 0);
}

/* 0db6: the box cleared and the text drawn */
static void entry_draw(ep_game *g)
{
    g->in.last_key = 0xff; /* 0287 */
    ep_render_rect(&g->render, *entry_b(g, 0x9cb), (int16_t)entry_w(g, 0x9c4), (int16_t)entry_w(g, 0x9c6),
                   (int16_t)((*entry_b(g, 0x9a2) + 1) * 8), 8);
    entry_text(g);
}

/* 0d9d (fresh) or 0daa (keep: the text is there already) */
static void entry_open(ep_game *g, uint8_t max, int16_t x, int16_t y, uint16_t colours, int keep)
{
    if (!keep) {
        *entry_b(g, 0x9a2) = max;
        *entry_b(g, 0x9a3) = 0;
        *entry_b(g, 0x9a4) = 0;
    }
    entry_b(g, 0x9c4)[0] = (uint8_t)x;
    entry_b(g, 0x9c4)[1] = (uint8_t)((uint16_t)x >> 8);
    entry_b(g, 0x9c6)[0] = (uint8_t)y;
    entry_b(g, 0x9c6)[1] = (uint8_t)((uint16_t)y >> 8);
    entry_b(g, 0x9ca)[0] = (uint8_t)colours;
    entry_b(g, 0x9ca)[1] = (uint8_t)(colours >> 8);
    entry_draw(g);
}

/* 0df6..0e38 for one key (ffh none): 1 accepted (the text at ds:09a4), -1 Esc, 0 typing on */
static int entry_key(ep_game *g, uint8_t key)
{
    uint8_t bit = (uint8_t)((uint8_t)g->clock >> 4 & 1); /* 0e9e: the cursor blinks */
    if (bit != *entry_b(g, 0x9cc)) {
        *entry_b(g, 0x9cc) = bit;
        entry_cursor(g, bit ? *entry_b(g, 0x9cb) : *entry_b(g, 0x9ca));
    }
    if (key == 0xff) return 0;
    g->in.last_key = 0xff;
    uint8_t *count = entry_b(g, 0x9a3);
    if (key == 0x1b) {
        ep_render_rect(&g->render, *entry_b(g, 0x9cb), (int16_t)entry_w(g, 0x9c4), (int16_t)entry_w(g, 0x9c6),
                       (int16_t)((*entry_b(g, 0x9a2) + 1) * 8), 8);
        return -1;
    }
    if (key == 0x0d) {
        entry_cursor(g, *entry_b(g, 0x9cb));
        return *count ? 1 : 0;
    }
    if (key == 8) {
        if (!*count) return 0;
        (*count)--;
        *entry_b(g, (uint16_t)(0x9a4 + *count)) = 0;
        entry_draw(g);
        return 0;
    }
    if (key < 0x20 || key > 0x7a) return 0;
    if (!(key == '-' || (key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z'))) return 0; /* 0e87 */
    if (*count == *entry_b(g, 0x9a2)) return 0;
    (*count)++;
    *entry_b(g, (uint16_t)(0x9a3 + *count)) = key;
    *entry_b(g, (uint16_t)(0x9a4 + *count)) = 0;
    entry_cursor(g, *entry_b(g, 0x9cb));
    entry_text(g);
    return 0;
}

int ep_chart_find_name(ep_game *g)
{
    ep_render_rect(&g->render, 0, 0x24, 0x8d, 0xc8, 0x14);
    g->seed = ep_galaxy_seed(g->cmdr.b[EP_CMDR_GALAXY]);
    text_header(g, 0x556b);
    entry_open(g, 8, 0xa5, 0x8d, 0x0f, 0);
    g->f.station_step = ST_FIND_TEXT;
    return EP_WAIT_TEXT;
}

static int find_key(ep_game *g, uint8_t key)
{
    int r = entry_key(g, key);
    if (!r) return EP_WAIT_TEXT;
    g->f.station_step = ST_NONE;
    if (r < 0) {
        ep_chart_find(g);
        return EP_WAIT_NONE;
    }
    uint8_t *c = g->cmdr.b;
    const uint8_t *typed = entry_b(g, 0x9a4);
    int len = 0;
    while (typed[len]) len++;
    g->f.find_text[0] = 0x09a4;
    g->f.find_text[1] = (uint16_t)(0x09a4 + len);
    int cmp = len == 8 ? 8 : len + 1; /* the NUL too, unless all eight letters */
    for (int n = 0; n < 256; n++) {
        uint8_t *name = &c[EP_CMDR_SELECTED + EP_SYSREC_NAME];
        ep_planet_name(&g->seed, name); /* 6130 */
        if (memcmp(name, typed, (size_t)cmp)) continue;
        g->seed = ep_system_seed(c[EP_CMDR_GALAXY], n);
        int16_t dx = (int16_t)((g->seed.w[1] >> 8) - c[EP_CMDR_CHART_CENTRE]);
        int16_t dy = (int16_t)((g->seed.w[0] >> 9) - c[EP_CMDR_CHART_CENTRE + 1]);
        if (c[EP_CMDR_ZOOM] >= 1 && ((dx < 0 ? -dx : dx) >= 0x14 || (dy < 0 ? -dy : dy) >= 0x11))
            break;                  /* off the map */
        if (c[EP_CMDR_ZOOM] == 1) { /* 5e95 */
            for (int k = 0; k < 2; k++) {
                int16_t d = k ? dy : dx;
                c[EP_CMDR_CURSOR + k] = (uint8_t)(3 * d + (d >> 1) + (k ? 0x40 : 0x50));
            }
        } else {
            c[EP_CMDR_CURSOR] = (uint8_t)(g->seed.w[1] >> 8);
            c[EP_CMDR_CURSOR + 1] = (uint8_t)(g->seed.w[0] >> 9);
        }
        ep_chart_find(g);
        return EP_WAIT_NONE;
    }
    *entry_b(g, (uint16_t)(0x9a4 + len)) = 0; /* 61f0: not on the map */
    text_header(g, 0x55e8);
    text_on(g, 0x09a4);
    text_on(g, 0x55f7);
    return EP_WAIT_NONE;
}

/* ---- boxes and questions ---- */

void ep_box_open(ep_game *g, uint16_t title_text)
{
    ep_render *r = &g->render;
    ep_event_add(g, EP_EV_KEEP, 1); /* 397c */
    ep_render_rect(r, 4, 0x19, 0x0d, 0x110, 0x73);
    ep_render_rect(r, 4, 0x1a, 0x80, 0x110, 1);
    ep_render_rect(r, 4, 0x129, 0x0e, 1, 0x73);
    ep_render_rect(r, 0x0c, 0x18, 0x0c, 0x110, 1);
    ep_render_rect(r, 0x0c, 0x18, 0x17, 0x110, 1);
    ep_render_rect(r, 0x0c, 0x18, 0x7e, 0x110, 1);
    ep_render_rect(r, 0x0c, 0x18, 0x0c, 1, 0x73);
    ep_render_rect(r, 0x0c, 0x127, 0x0c, 1, 0x73);
    uint8_t t[40];
    int n = ep_ds_text(g, title_text, t, sizeof t);
    title(g, 0xa0, 0x0e, 0x0a, t, n);
}

void ep_box_close(ep_game *g) { ep_event_add(g, EP_EV_PUT_BACK, 1); } /* 3981 */

int ep_station_ask(ep_game *g, uint16_t title_text, int what)
{
    ep_box_open(g, title_text);
    uint8_t t[96];
    int n = ep_ds_text(g, 0x0685, t, sizeof t);
    title(g, 0xa0, 0x46, 0x0f, t, n);
    wait(g, what == EP_ASK_ABANDON ? ST_ABANDON : ST_EXIT, EP_WAIT_YN);
    return EP_CMD_SCREEN;
}

/* ---- saving and loading ---- */

/* 2fc0: centred on x */
static void centred(ep_game *g, int16_t x, int16_t y, uint8_t colour, uint16_t addr)
{
    uint8_t t[96];
    int n = ep_ds_text(g, addr, t, sizeof t);
    ep_pen(&g->render, (int16_t)(x - (ep_text_width(t) >> 1)), y, colour);
    ep_text(&g->render, t, n, 0);
}

int ep_save_screen(ep_game *g)
{
    ep_box_open(g, 0x03f3);
    centred(g, 0xa0, 0x24, 0x0e, 0x045e);
    uint8_t *e = entry_b(g, 0x9a4), *name = &g->cmdr.b[EP_CMDR_NAME];
    int n = 0;
    uint8_t c;
    do e[n] = c = name[n]; /* up to a space or the end */
    while (++n < 16 && c && c != ' ');
    e[n - 1] = 0;
    *entry_b(g, 0x9a3) = (uint8_t)(n - 1);
    *entry_b(g, 0x9a2) = 8;
    entry_open(g, 8, 0x80, 0x3c, 0x010f, 1);
    g->f.station_step = ST_SAVE_NAME;
    return EP_WAIT_TEXT;
}

/* 0864..086d: a text, then space */
static int save_said(ep_game *g, uint16_t text)
{
    text_header(g, text);
    g->f.station_step = ST_SAVE_DONE;
    return EP_WAIT_KEY;
}

static int save_key(ep_game *g, uint8_t key)
{
    ep_flight *f = &g->f;
    const ep_io *io = g->io;
    char *file = (char *)entry_b(g, 0x9a4); /* the name typed, then the file's */
    if (f->station_step == ST_SAVE_DONE) {  /* 0ed5 */
        if (key != ' ') return EP_WAIT_KEY;
        f->station_step = ST_NONE;
        ep_box_close(g);
        return EP_WAIT_NONE;
    }
    if (f->station_step == ST_SAVE_NAME) {
        int r = entry_key(g, key);
        if (!r) return EP_WAIT_TEXT;
        if (r < 0) {
            f->station_step = ST_NONE;
            ep_box_close(g);
            return EP_WAIT_NONE;
        }
        uint8_t *name = &g->cmdr.b[EP_CMDR_NAME];
        int n = 0;
        do name[n] = (uint8_t)file[n];
        while (file[n++]);
        memcpy(&file[n - 1], ".CDR", 5);
        f->screen_redraw = 1;
        ep_sync_to_commander(g); /* 77c5 */
        ep_commander_seal(&g->cmdr);
        if (io && io->exists && io->exists(io->ctx, file)) {
            centred(g, 0xa0, 0x4b, 0x0f, 0x047c);
            f->station_step = ST_SAVE_ASK;
            return EP_WAIT_YN;
        }
    } else { /* ST_SAVE_ASK */
        int yes = key == 'Y' || key == 'y', no = key == 'N' || key == 'n';
        if (!yes && !no) return EP_WAIT_YN;
        if (no) return save_said(g, 0x04a3);
    }
    if ((io && io->write ? io->write(io->ctx, file, g->cmdr.b, EP_COMMANDER_SIZE) : -1) !=
        EP_COMMANDER_SIZE) {
        text_header(g, 0x04dd);
        return save_said(g, 0x04a3);
    }
    return save_said(g, 0x04ff);
}

int ep_load_screen(ep_game *g)
{
    ep_flight *f = &g->f;
    const ep_io *io = g->io;
    memcpy(f->menu_kept, f->menu, sizeof f->menu_kept);
    ep_box_open(g, 0x0402);
    static char names[0x28][13];
    int n = io && io->list ? io->list(io->ctx, names, 0x28) : 0;
    if (n <= 0) {
        text_header(g, 0x053b);
        return wait(g, ST_LOAD_NONE, EP_WAIT_KEY);
    }
    text_header(g, 0x057e);
    f->file_count = 0;
    int at = 0;
    for (int k = 0; k < n && f->file_count < 0x28; k++) { /* 0a70: up to the dot */
        for (const char *p = names[k]; *p && *p != '.' && at < (int)sizeof f->files - 1; p++)
            f->files[at++] = (uint8_t)*p;
        f->files[at++] = 0;
        f->file_count++;
    }
    uint8_t rows = f->file_count;
    if (rows > 0x0c) {
        rows = 0x0c;
        f->file_top = 0;
    }
    ep_list_open(g, 0x0107, 0x090f, (uint8_t)(rows | 0x80), 0, 0x0088, 0x1e, 0x1a, 0x4c);
    f->station_step = ST_LOAD_LIST;
    return EP_WAIT_LIST;
}

/* 08e7: the list as it was, the box gone */
static int load_back(ep_game *g)
{
    memcpy(g->f.menu, g->f.menu_kept, sizeof g->f.menu_kept);
    g->f.station_step = ST_NONE;
    ep_box_close(g);
    return EP_WAIT_NONE;
}

/* 0979, 09a8: the list scrolled; the cursor's item is the one at row */
static void load_scroll(ep_game *g, uint8_t row)
{
    menu_set_word(g, 2, 0x0088);
    menu_set_word(g, 2, list_item(g, g->f.file_top));
    menu_set_word(g, 4, list_item(g, row));
    ep_list_poll(g, 1);
}

static int load_key(ep_game *g, uint8_t key)
{
    ep_flight *f = &g->f;
    const ep_io *io = g->io;
    uint8_t *c = g->cmdr.b;
    switch (f->station_step) {
    case ST_LOAD_NONE:
    case ST_LOAD_GONE:
        if (key != ' ') return EP_WAIT_KEY;
        return load_back(g);
    case ST_LOAD_BAD:
        if (key != ' ') return EP_WAIT_KEY;
        f->station_step = ST_NONE;
        f->leave = 1; /* 0a12 */
        f->screen_shown = 0xff;
        g->space.in_flight = 0;
        return EP_WAIT_NONE;
    case ST_LOAD_GOOD:
        if (key != ' ') return EP_WAIT_KEY;
        f->station_step = ST_NONE;
        /* 0a3c: a military laser in front counts as fitted */
        if ((c[EP_CMDR_LASERS] & 1) && (c[EP_CMDR_LASER_TYPES] & 3) == 3 && !c[EP_CMDR_EQUIPMENT + 12]) {
            c[EP_CMDR_EQUIPMENT + 12] = 1;
            c[EP_CMDR_EQUIPMENT + 3] = 0;
        }
        f->screen_shown = 0xff;
        g->space.in_flight = 1;
        ep_music_stop(g); /* 4d55, 4ac0 */
        f->leave = 3;
        return EP_WAIT_NONE;
    default: break;
    }
    f->last_cmd_key = key; /* 03b6 */
    uint8_t got = ep_list_poll(g, 0);
    if (got == 0x1b) return load_back(g);
    if (got == 0x48 && f->file_count > 0x0c && f->file_top) {
        f->file_top--;
        load_scroll(g, 0);
        return EP_WAIT_LIST;
    }
    if (got == 0x50 && f->file_count > 0x0c && f->file_top + 1 + 0x0c <= f->file_count) {
        f->file_top++;
        load_scroll(g, 0x0b);
        return EP_WAIT_LIST;
    }
    if (got != 0x0d) return EP_WAIT_LIST;
    /* 09c2: ".CDR" written after the name under the cursor (over the next one's) */
    uint16_t item = list_item(g, f->menu[1]), end = item;
    while (ep_ds_byte(g, end)) end++;
    char file[13];
    int k = 0;
    for (uint16_t a = item; a < end && k < 8; a++) file[k++] = (char)ep_ds_byte(g, a);
    memcpy(file + k, ".CDR", 5);
    for (int j = 0; j < 5 && end - 0x88 + j < (int)sizeof f->files; j++)
        f->files[end - 0x88 + j] = (uint8_t)".CDR"[j];
    uint8_t data[EP_COMMANDER_SIZE];
    int r = io && io->read ? io->read(io->ctx, file, data, EP_COMMANDER_SIZE) : -1;
    if (r < 0) {
        text_header(g, 0x05fe);
        f->station_step = ST_LOAD_GONE;
        return EP_WAIT_KEY;
    }
    memcpy(c, data, (size_t)r);
    ep_sync_from_commander(g);
    int good = r == EP_COMMANDER_SIZE && ep_commander_valid(&g->cmdr);
    if (r == EP_COMMANDER_SIZE) ep_commander_seal(&g->cmdr); /* 77c5 stores the sum it made */
    text_header(g, good ? 0x05c8 : 0x063d);
    f->station_step = good ? ST_LOAD_GOOD : ST_LOAD_BAD;
    return EP_WAIT_KEY;
}

/* ---- the copy protection's question ---- */

int ep_protection_ask(ep_game *g)
{
    text_header(g, 0x09db);
    entry_open(g, 0x18, 0x5a, 0x93, 0x000a, 0); /* 0d9d */
    g->f.station_step = ST_PROTECTION;
    return EP_WAIT_TEXT;
}

static int protection_key(ep_game *g, uint8_t key)
{
    int r = entry_key(g, key);
    if (!r) return EP_WAIT_TEXT;
    /* 1474: Esc leaves DI 0, the hash then of ds:0000 (empty here) */
    uint16_t h = r < 0 ? 0 : ep_protection_hash(entry_b(g, 0x9a4));
    g->f.protection_failed = h != g->f.prot_hash;
    g->f.station_step = ST_NONE;
    return EP_WAIT_NONE;
}

/* ---- the controls: keys, joystick, mouse (0674, 0736, 0779) ---- */

#define KEY_NONE ((uint16_t)(0xffff - 0x20d)) /* a binding to nothing (ds:ffff) */

static uint16_t *binding(ep_game *g, uint16_t addr)
{
    uint16_t *b[7] = { &g->in.faster, &g->in.slower, &g->in.up,  &g->in.down,
                       &g->in.left,   &g->in.right,  &g->in.fire };
    return b[(addr - 0xb251) / 2];
}

/* the prompts and what they bind: all seven (0674), the speed's two (0709) */
static const struct {
    uint16_t text, bind;
} define_all[7] = { { 0x06b5, 0xb255 }, { 0x06e6, 0xb257 }, { 0x06fd, 0xb259 }, { 0x0721, 0xb25b },
                    { 0x0741, 0xb25d }, { 0x075d, 0xb251 }, { 0x077d, 0xb253 } },
  define_two[2] = { { 0x079d, 0xb251 }, { 0x077d, 0xb253 } };

/* 05db: the prompt; the keys must all be up before one is taken */
static int define_prompt(ep_game *g)
{
    ep_flight *f = &g->f;
    text_header(g, f->define_set == 1 ? define_all[f->define_k].text : define_two[f->define_k].text);
    f->define_armed = 0;
    return wait(g, ST_DEFINE_KEY, EP_WAIT_SCAN);
}

/* 06ac, 0709: no key bound, then each in turn */
static int define_start(ep_game *g, uint8_t set)
{
    for (uint16_t a = 0xb251; a <= 0xb25d; a = (uint16_t)(a + 2)) *binding(g, a) = KEY_NONE;
    g->f.define_set = set;
    g->f.define_k = 0;
    return define_prompt(g);
}

int ep_define_keys(ep_game *g)
{
    ep_box_open(g, 0x0411);
    g->in.control = 0;
    g->f.screen_shown = 0xff;
    if (g->in.up != KEY_NONE) { /* 068b: they are defined already */
        text_header(g, 0x0692);
        return wait(g, ST_DEFINE_ASK, EP_WAIT_YN);
    }
    return define_start(g, 1);
}

int ep_joystick(ep_game *g)
{
    ep_box_open(g, 0x0422);
    text_header(g, 0x07b2);
    return wait(g, ST_JOY_CENTRE, EP_WAIT_KEY);
}

int ep_mouse(ep_game *g)
{
    ep_box_open(g, 0x0437);
    if (!g->in.mouse_present) { /* 079f: the error, and the box at once put back */
        text_header(g, 0x08cd);
        g->in.last_key = 0xff;
        g->f.station_step = ST_NONE;
        ep_box_close(g);
        return EP_WAIT_NONE;
    }
    g->in.control = 2;
    g->f.screen_shown = 0xff;
    text_header(g, 0x088b);
    return define_start(g, 2);
}

static int controls_key(ep_game *g, uint8_t key)
{
    ep_flight *f = &g->f;
    ep_input *in = &g->in;
    switch (f->station_step) {
    case ST_DEFINE_ASK: {
        int yes = key == 'Y' || key == 'y', no = key == 'N' || key == 'n';
        if (!yes && !no) return EP_WAIT_YN;
        if (yes) return define_start(g, 1);
        f->station_step = ST_NONE;
        ep_box_close(g);
        return EP_WAIT_NONE;
    }
    case ST_JOY_CENTRE:
        if (key != ' ') return EP_WAIT_KEY;
        in->joy_centre_x = in->joy_present ? in->joy_x : 0; /* 0ffb */
        in->joy_centre_y = in->joy_present ? in->joy_y : 0;
        if (!in->joy_present) {
            text_header(g, 0x07f2);
            return wait(g, ST_JOY_ERROR, EP_WAIT_KEY);
        }
        in->control = 1;
        f->screen_shown = 0xff;
        text_header(g, 0x0866);
        return define_start(g, 2);
    case ST_JOY_ERROR:
        if (key != ' ') return EP_WAIT_KEY;
        f->station_step = ST_NONE;
        ep_box_close(g);
        return EP_WAIT_NONE;
    default: break;
    }
    /* ST_DEFINE_KEY */
    if (!f->define_armed) { /* 05e5: until no key is down */
        for (int k = 0; k < 0x80; k++)
            if (!in->key[k]) return EP_WAIT_SCAN;
        in->last_scan = 0xffff;
        f->define_armed = 1;
        return EP_WAIT_SCAN;
    }
    uint16_t p = in->last_scan; /* 05f9: a key pressed, one not bound yet */
    if (p == 0xffff) return EP_WAIT_SCAN;
    for (uint16_t a = 0xb251; a <= 0xb25d; a = (uint16_t)(a + 2))
        if ((uint16_t)(0x20d + *binding(g, a)) == p) return EP_WAIT_SCAN;
    *binding(g, f->define_set == 1 ? define_all[f->define_k].bind : define_two[f->define_k].bind) =
        (uint16_t)(p - 0x20d);
    if (++f->define_k < (f->define_set == 1 ? 7 : 2)) return define_prompt(g);
    in->last_key = 0xff; /* 0287 */
    f->station_step = ST_NONE;
    f->define_set = 0;
    ep_box_close(g);
    return EP_WAIT_NONE;
}

/* ---- a new game ---- */

static uint8_t rol8(uint8_t v, int n) { return (uint8_t)(v << n | v >> (8 - n)); }

void ep_new_game(ep_game *g, uint8_t hour, uint8_t minute, uint8_t second, uint8_t hundredths)
{
    ep_flight *f = &g->f;
    uint8_t *c = g->cmdr.b;
    memcpy(c, g->cmdr_saved.b, EP_COMMANDER_SIZE);
    ep_sync_from_commander(g);
    f->hyperspace = 0;
    f->energy = 0x3ff;
    f->fore_shield = 0xff;
    f->aft_shield = 0xff;
    f->sun_size = 0x0c;
    f->altitude = 0xff;
    f->dead = 0;
    f->mission5_count = 0;
    c[EP_CMDR_JUMPS_COUNTED] = 0;
    f->mission = 0;
    f->warn_time = 0;
    f->missile_alert = 0;
    f->mission5_phase = 0;
    f->leak_countdown = 0;
    f->leak = 0;
    f->approach = 0;
    f->approach_size = 0;
    f->convoy_leader_dead = 0;
    f->station_angry = 0;
    f->convoy_left = 0;
    f->station_hit = 0;
    f->force_misjump = 0;
    f->message_time = 0;
    f->target_note = 0;
    c[EP_CMDR_CONVOY_DUE] = 0;
    f->convoy_countdown = 0;
    f->laser_temp = 0;
    f->energy_drain = 0;
    uint8_t x = (uint8_t)(rol8(minute, 2) ^ rol8(hour, 4) ^ hundredths ^ second); /* 7260 */
    uint16_t cx = (uint16_t)(((x << 2) + 0x1388) << 1);
    uint16_t price = (uint16_t)(cx * 5);
    c[0xdc] = (uint8_t)price; /* ds:83b7: the Tribble offer */
    c[0xdd] = (uint8_t)(price >> 8);
    f->tribbles = 0;
}

int ep_start_game(ep_game *g, uint8_t hour, uint8_t minute, uint8_t second, uint8_t hundredths)
{
    ep_flight *f = &g->f;
    ep_music_stop(g); /* 4d55, 4ac0: the title music stops */
    ep_new_game(g, hour, minute, second, hundredths);
    g->space.in_flight = 1;
    f->screen_shown = 0xff;
    f->screen = 1;
    f->launching = 0;
    return ep_status_screen(g);
}

/* what a pass does after the commands (also when the pause menu closes) */
static int idle_after(ep_game *g, int r)
{
    ep_flight *f = &g->f;
    if (r == EP_CMD_PAUSE) {
        f->resume = EP_RESUME_IDLE;
        return r;
    }
    switch (f->idle) {
    case EP_IDLE_STATUS:
        if (r == EP_CMD_STAY && f->screen_redraw) ep_status_screen(g);
        return r;
    case EP_IDLE_MARKET: { /* 912a..915f */
        if (r != EP_CMD_STAY) return r;
        ep_list_poll(g, 0);
        uint8_t row = f->menu[1];
        f->list_row = row;
        const uint8_t *c = &g->cmdr.b[EP_CMDR_CARGO + 2 * row];
        f->screen_bits = (uint8_t)((c[0] ? 2 : 0) | (c[1] && row != 0x10 ? 1 : 0));
        if (f->list_busy && !f->note_ticks) cash_line(g); /* 90d7 */
        return r;
    }
    case EP_IDLE_EQUIP:
        if (r != EP_CMD_STAY || f->station_step) return r;
        equipment_tail(g);
        return r;
    default: return r;
    }
}

int ep_station_idle(ep_game *g)
{
    ep_flight *f = &g->f;
    switch (f->idle) {
    case EP_IDLE_STATUS: f->screen_redraw = 0; break; /* 8dac */
    case EP_IDLE_LOCAL: chart_local_pass(g); break;
    case EP_IDLE_GALAXY: chart_galaxy_pass(g); break;
    default: break;
    }
    ep_key_bar(g);
    return idle_after(g, ep_commands(g));
}

int ep_station_resume(ep_game *g)
{
    g->f.resume = EP_RESUME_NONE;
    return idle_after(g, ep_commands(g)); /* 03c0 goes on reading keys */
}
