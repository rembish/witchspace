/* Elite Plus at the station, reconstructed from ELITE.EXE (see ep_station.h). */
#include "ep_station.h"

#include <string.h>

#include "ep_chart.h"
#include "ep_dsmap.h"
#include "ep_combat.h"
#include "ep_commands.h"
#include "ep_render.h"
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

/* 7748: the frame of a screen at the station */
static void frame(ep_game *g)
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
    g->f.other_screen = 2;
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
    ST_M456_DONE   /* 9c75, 9cb5, 9cf3 */
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
    case ST_ELITE: goto after_elite;
    case ST_TRIBBLES: goto after_tribbles;
    case ST_NONE: break;
    default: goto after_mission;
    }
    if (f->launching == 1) goto view;
    if ((r = promotion(g))) return r;
after_rating:
    if (f->station_rating) { /* 3b75: the Elite picture, palette cycling until a key */
        f->station_rating = 0;
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
    case ST_NONE: return EP_WAIT_NONE;
    default: break;
    }
    return arrival(g);
}
