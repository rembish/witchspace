/* Elite Plus on an AdLib card: the music driver (see ep_adlib.h). */
#include "ep_adlib.h"

#include "ep_sound.h"
#include "ep_tables.h"

#include <string.h>

/* the driver's data, ds:b5b7..bccf (g->adlib.drv); its tables beyond that are static */
static uint8_t rb(const ep_game *g, uint16_t a)
{
    if (a >= EP_ADLIB_DS && a < EP_ADLIB_DS + EP_ADLIB_DS_SIZE) return g->adlib.drv[a - EP_ADLIB_DS];
    return a < EP_DS_INITIAL ? ep_ds_initial[a] : 0;
}

static void wb(ep_game *g, uint16_t a, uint8_t v)
{
    if (a >= EP_ADLIB_DS && a < EP_ADLIB_DS + EP_ADLIB_DS_SIZE) g->adlib.drv[a - EP_ADLIB_DS] = v;
}

static uint16_t rw(const ep_game *g, uint16_t a)
{
    return (uint16_t)(rb(g, a) | rb(g, (uint16_t)(a + 1)) << 8);
}

static void ww(ep_game *g, uint16_t a, uint16_t v)
{
    wb(g, a, (uint8_t)v);
    wb(g, (uint16_t)(a + 1), (uint8_t)(v >> 8));
}

/* the song, ADBLUE.MID as read at start-up (segment 16e4) */
static uint8_t song(const ep_game *g, uint16_t at)
{
    return at < sizeof g->adlib.song ? g->adlib.song[at] : 0;
}

/* d74: a write to the chip */
void ep_opl(ep_game *g, uint8_t reg, uint8_t val)
{
    if (g->nopl < EP_MAX_OPL) {
        g->opl[g->nopl][0] = reg;
        g->opl[g->nopl][1] = val;
        g->nopl++;
    }
}

/* dd6: the timer's divisor, and the interrupt's two counters started from it */
static void set_timer(ep_game *g, uint16_t divisor)
{
    g->adlib.divisor = g->adlib.clock_acc = g->adlib.bios_acc = divisor;
    g->adlib.clock_acc_hi = 0;
    g->pit = divisor;
}

/* 5dc: a MIDI variable-length number */
static uint16_t varlen(const ep_game *g, uint16_t *si)
{
    uint16_t v = 0;
    uint8_t b;
    do {
        b = song(g, (*si)++);
        v = (uint16_t)((v << 7) + (b & 0x7f));
    } while (b & 0x80);
    return v;
}

/* c59: the rhythm register (bdh) */
static void rhythm_reg(ep_game *g)
{
    uint8_t v = (uint8_t)((uint8_t)(rb(g, 0xb73a) << 7) | (uint8_t)(rb(g, 0xb73b) << 6) |
                          (uint8_t)(rb(g, 0xb73d) << 5) | rb(g, 0xb743));
    ep_opl(g, 0xbd, v);
}

/* c7d: a voice's frequency and key: the note plus the pitch bend (2000h: none), through the
 * frequency tables; key on with keyon set */
static void voice_note(ep_game *g, uint8_t voice, uint8_t note, uint16_t pitch, uint8_t keyon)
{
    wb(g, 0xb73e, voice);
    wb(g, 0xb73f, note);
    ww(g, 0xb740, pitch);
    wb(g, 0xb742, (uint8_t)(keyon << 5));
    /* 0c92: the volume from the velocity is jumped over: never written */
    uint16_t ax = (uint16_t)(pitch - 0x2000);
    if (ax) ax = (uint16_t)((int32_t)(int16_t)((int16_t)ax >> 5) * (int16_t)rw(g, 0xb738));
    ax = (uint16_t)(ax + ((uint16_t)note << 8));
    int16_t v = (int16_t)(uint16_t)(ax + 8) >> 4;
    if (v < 0) v = 0;
    if (v >= 0x5ff) v = 0x5ff;
    uint16_t di = (uint16_t)v >> 4;
    uint16_t row = (uint16_t)(rb(g, (uint16_t)(0xb92e + di)) << 5);
    int16_t f = (int16_t)rw(g, (uint16_t)(0xb74e + row + ((v << 1) & 0x1f)));
    int8_t oct = (int8_t)(rb(g, (uint16_t)(0xb8ce + di)) - 1);
    if (f < 0) oct++;
    if (oct < 0) {
        oct++;
        f = (int16_t)(f >> 1);
    }
    uint8_t r = rb(g, 0xb73e);
    g->adlib.left_bl = (uint8_t)(oct << 2); /* what BL is left as */
    ep_opl(g, (uint8_t)(0xa0 + r), (uint8_t)f);
    ep_opl(g, (uint8_t)(0xb0 + r), (uint8_t)((((uint16_t)f >> 8) & 3) + (uint8_t)(oct << 2) + rb(g, 0xb742)));
}

/* a26 (on 0) and 9aa (on 1): a note off or on in a voice; the rhythm voices (6 and up, in
 * rhythm mode) by their bit in bdh */
static void voice_key(ep_game *g, uint16_t voice, uint8_t note, uint8_t vel, int on)
{
    uint8_t v = (uint8_t)voice;
    wb(g, (uint16_t)(0xb6d7 + v), vel);
    wb(g, (uint16_t)(0xb6b6 + v), note);
    int16_t n = (int16_t)(uint8_t)(note + rb(g, (uint16_t)(0xb70a + v))) - 0x0c;
    if (n < 0) n = 0;
    ww(g, (uint16_t)(0xb6c1 + 2 * v), on ? rw(g, 0xb686) : 0); /* when the note began (0: free) */
    if (!rb(g, 0xb73d) || (int16_t)voice <= 5) {
        voice_note(g, v, (uint8_t)n, 0x2000, (uint8_t)on);
        return;
    }
    uint8_t bit = rb(g, (uint16_t)(0xb744 + v - 6));
    if (!on) {
        wb(g, 0xb743, (uint8_t)(rb(g, 0xb743) & ~bit));
        rhythm_reg(g);
        return;
    }
    if (v == 8) voice_note(g, 7, (uint8_t)(n + 7), 0x2000, 0);
    if (v == 6 || v == 8) voice_note(g, v, (uint8_t)n, 0x2000, 0);
    wb(g, 0xb743, (uint8_t)(rb(g, 0xb743) | bit));
    rhythm_reg(g);
}

/* b84: an operator from a 14-byte record (masked in place), into the slot's registers */
static void operator_set(ep_game *g, uint8_t slot, uint8_t voice, uint16_t rec)
{
    static const uint8_t mask[14] = { 3, 0xf, 7, 0xf, 0xf, 1, 0xf, 0xf, 0x3f, 1, 1, 1, 1, 3 };
    wb(g, 0xb73e, voice);
    uint8_t off = rb(g, (uint16_t)(0xb98e + slot));
    uint8_t r[14];
    for (int k = 0; k < 14; k++) {
        r[k] = (uint8_t)(rb(g, (uint16_t)(rec + k)) & mask[k]);
        wb(g, (uint16_t)(rec + k), r[k]);
    }
    ep_opl(g, (uint8_t)(0x40 + off), (uint8_t)(r[0] << 6 | r[8]));
    ep_opl(g, (uint8_t)(0x60 + off), (uint8_t)(r[3] << 4 | r[6]));
    ep_opl(g, (uint8_t)(0x80 + off), (uint8_t)(r[4] << 4 | r[7]));
    ep_opl(g, (uint8_t)(0x20 + off), (uint8_t)(r[9] << 7 | r[10] << 6 | r[5] << 5 | r[11] << 4 | r[1]));
    ep_opl(g, (uint8_t)(0xe0 + off), r[13]);
}

/* b48: a voice's instrument: its operators (one in rhythm mode for some), the feedback (c32) */
static void instrument_set(ep_game *g, uint16_t voice, uint16_t rec, uint16_t rec2)
{
    uint16_t bx = (uint16_t)((rb(g, 0xb73d) ? 0xbcb7 : 0xbca5) + 2 * voice);
    operator_set(g, rb(g, bx), (uint8_t)voice, rec);
    if ((int8_t)rb(g, (uint16_t)(bx + 1)) < 0) return;
    ep_opl(g, (uint8_t)(0xc0 + rb(g, 0xb73e)),
           (uint8_t)((uint8_t)(rb(g, (uint16_t)(rec + 2)) << 1) | (rb(g, (uint16_t)(rec + 0x0c)) ^ 1)));
    operator_set(g, rb(g, (uint16_t)(bx + 1)), (uint8_t)voice, rec2);
}

/* ade: the rhythm voices' instruments */
static void rhythm_instruments(ep_game *g)
{
    instrument_set(g, 6, 0xb9db, 0xb9e9);
    instrument_set(g, 7, 0xb9f7, 0xb9e9);
    instrument_set(g, 8, 0xba05, 0xb9e9);
    instrument_set(g, 9, 0xba13, 0xb9e9);
    instrument_set(g, 10, 0xba21, 0xb9e9);
}

static int melodic_voices(const ep_game *g) { return rb(g, 0xb73d) ? 5 : 8; }

/* a91: the chip reset; its loop (a signed comparison) stops after register 1 */
static void chip_reset(ep_game *g)
{
    ep_opl(g, 1, 0);
    ep_opl(g, 4, 0x60);
    ep_opl(g, 4, 0x80);
    ep_opl(g, 1, 0x20);
}

/* a75: every voice off, with the note and velocity BL and CL hold: for the first what the
 * caller left (BX the word at ds:0002, CX its own), then what the last voice's frequency
 * left (BL its octave times 4, CL 5) */
static void all_off(ep_game *g)
{
    int last = rb(g, 0xb73d) ? 10 : 8;
    uint8_t note = rb(g, 2), vel = g->adlib.caller_cl;
    for (int v = 0; v <= last; v++) {
        voice_key(g, (uint16_t)v, note, vel, 0);
        note = g->adlib.left_bl;
        vel = 5;
    }
}

/* 7ff: the next event; returns the ticks to the one after */
static uint16_t step(ep_game *g)
{
    if (!rb(g, 0xb66d)) return 1;
    uint16_t si = rw(g, 0xb686);
    uint8_t c = song(g, si);
    if (c >= 0x80) {
        si++;
        if (c == 0xff) { /* 5f7: a meta event; 2fh, the end, starts the song again */
            uint8_t type = song(g, si++);
            uint16_t len = varlen(g, &si);
            if (type == 0x2f) goto again;
            si = (uint16_t)(si + len); /* 51h, the tempo, is read but not used */
            goto next;
        }
        if (c >= 0xf0) { /* 62f: system exclusive, sent to the MPU-401 port (not here) */
            uint16_t len = varlen(g, &si);
            si = (uint16_t)(si + len);
            goto next;
        }
        wb(g, 0xb5c0, c);
    }
    {
        uint8_t status = rb(g, 0xb5c0), ch = status & 0x0f, type = status >> 4;
        wb(g, 0xb5c1, ch);
        ww(g, 0xb5e2, rb(g, (uint16_t)(0xb5e4 + type))); /* the event's length */
        uint8_t note = song(g, si), vel = song(g, (uint16_t)(si + 1));
        uint8_t n = rb(g, (uint16_t)(0xb6e2 + ch));
        uint16_t list = (uint16_t)(0xb6ea + ch * 8);
        int drum = rb(g, 0xb73d) && ch == 9;
        if (type == 8 && !drum && n) { /* 873: off in the voice that plays the note */
            uint16_t voice = rb(g, list);
            for (int k = 0; n != 1 && k < n; k++) {
                voice = rb(g, (uint16_t)(list + k));
                if (rb(g, (uint16_t)(0xb6b6 + voice)) == note) break;
            }
            voice_key(g, voice, note, vel, 0);
        } else if (type == 9) { /* 8d2: on in a free voice, else the longest playing */
            ww(g, (uint16_t)(0xb5c2 + 2 * ch), (uint16_t)(rw(g, (uint16_t)(0xb5c2 + 2 * ch)) + 1));
            uint16_t voice = 0xffff;
            if (drum) { /* 094d: by the drum's note (the 18h it sets is reloaded at 0931) */
                voice = rb(g, (uint16_t)(0xb722 + (uint8_t)(note - 0x23)));
            } else if (n == 1) {
                voice = rb(g, list);
            } else if (n) {
                uint16_t best = 0xffff, pick = 0;
                for (int k = 0; k < n; k++) {
                    uint16_t v = rb(g, (uint16_t)(list + k)), age = rw(g, (uint16_t)(0xb6c1 + 2 * v));
                    if (!age) {
                        pick = v;
                        break;
                    }
                    if (age < best) {
                        best = age;
                        pick = v;
                    }
                }
                voice = pick;
            }
            if (voice != 0xffff) {
                voice_key(g, voice, note, vel, 0);
                voice_key(g, voice, note, vel, 1);
            }
        }
        si = (uint16_t)(si + rw(g, 0xb5e2));
    }
next:
    if ((uint16_t)(si - rw(g, 0xb68a)) < rw(g, 0xb688)) goto delta;
again:
    si = rw(g, 0xb68a);
delta: {
    uint16_t d = varlen(g, &si); /* 993 */
    ww(g, 0xb686, si);
    return d;
}
}

void ep_adlib_init(ep_game *g)
{
    /* 31a: a Roland's song goes to the same place (its effects, RFX.MID, after it at 1989h; a
     * Roland's music is not ported) */
    const char *name = rb(g, 0xb5b7) ? "ADBLUE.MID" : "BLUTEST.MID";
    if (g->io && g->io->read) g->io->read(g->io->ctx, name, g->adlib.song, sizeof g->adlib.song);
}

void ep_adlib_music(ep_game *g)
{
    if (!rb(g, 0xb5b7)) return; /* the driver's own test: a Roland's music (not ported) */
    ww(g, 0xb6b4, 0x60);
    uint16_t s = rb(g, 0xb5b9); /* 73f */
    ww(g, 0xbccd, s);
    static const struct {
        uint16_t table, to, n;
    } copy[4] = {
        { 0xbd88, 0xb6e2, 8 }, { 0xbd8c, 0xb6ea, 0x20 }, { 0xbd90, 0xb713, 8 }, { 0xbd94, 0xb70a, 9 }
    };
    for (int k = 0; k < 4; k++) {
        uint16_t from = rw(g, (uint16_t)(copy[k].table + 2 * s));
        for (uint16_t j = 0; j < copy[k].n; j++)
            wb(g, (uint16_t)(copy[k].to + j), rb(g, (uint16_t)(from + j)));
    }
    wb(g, 0xb73d, rb(g, (uint16_t)(0xbd98 + s)));
    chip_reset(g);
    rhythm_reg(g); /* c48 */
    ep_opl(g, 8, (uint8_t)(rb(g, 0xb73c) << 6));
    for (int v = 0; v <= melodic_voices(g); v++) instrument_set(g, (uint16_t)v, 0xb9bf, 0xb9cd); /* ab5 */
    if (rb(g, 0xb73d)) rhythm_instruments(g);
    for (int v = 0; v <= melodic_voices(g); v++) { /* b0f */
        uint16_t rec = rw(g, (uint16_t)(0xba2f + 2 * rb(g, (uint16_t)(0xb713 + v))));
        instrument_set(g, (uint16_t)v, rec, (uint16_t)(rec + 0x0e));
    }
    if (rb(g, 0xb73d)) rhythm_instruments(g);
    wb(g, 0xb66d, 0);
    set_timer(g, 0xffff); /* db6: the music's interrupt */
    g->adlib.busy = 0;
    g->int8 = EP_INT8_MUSIC;
    ww(g, 0xb684, 0x26e4); /* 7b0: the song's segment, 16e4 with the game loaded at 1000h */
    ww(g, 0xb686, 0);
    ww(g, 0xb69f, rb(g, (uint16_t)(0xbd00 + rw(g, 0xbccd))));
    uint32_t t = (uint32_t)rw(g, 0xb69f) * rw(g, 0xb6b4) / 0x3c; /* 7e8: ticks a second */
    set_timer(g, t > 0x12 ? (uint16_t)(0x123321u / t) : 0xffff);
    uint16_t si = 18; /* 3cf: past MThd and its six bytes, MTrk; the track's length (low word) */
    ww(g, 0xb688, (uint16_t)(song(g, 20) << 8 | song(g, 21)));
    si = 22;
    ww(g, 0xb68a, si);
    varlen(g, &si); /* 07c5: the first delta, not counted */
    ww(g, 0xb686, si);
    g->adlib.countdown = 1;
    wb(g, 0xb66d, 0xff);
}

void ep_adlib_stop(ep_game *g)
{
    if (!rb(g, 0xb5b7)) return;
    wb(g, 0xb66d, 0); /* 7dc */
    all_off(g);
    chip_reset(g);
}

void ep_adlib_game_timer(ep_game *g)
{
    g->int8 = EP_INT8_GAME;
    ep_adfx_init(g); /* 1930 */
    g->pit = 0x5555;
}

void ep_adlib_tick(ep_game *g)
{
    ep_adlib *a = &g->adlib;
    if (--a->countdown == 0 && !a->busy) {
        for (;;) {
            uint16_t d = step(g);
            if ((uint16_t)(0u - a->countdown) < d) {
                a->countdown = (uint16_t)(a->countdown + d);
                break;
            }
            a->countdown = 0;
        }
    }
    uint32_t acc = (uint32_t)a->clock_acc_hi << 16 | a->clock_acc;
    acc += a->divisor;
    while (acc >= 0x5555) { /* the game's clock (4a50) at its own rate */
        ep_timer_tick(g);
        acc -= 0x5555;
    }
    a->clock_acc = (uint16_t)acc;
    a->clock_acc_hi = (uint16_t)(acc >> 16);
    a->bios_acc = (uint16_t)(a->bios_acc + a->divisor); /* the BIOS's clock: not modelled */
}

void ep_pit_tick(ep_game *g)
{
    if (g->int8 == EP_INT8_MUSIC)
        ep_adlib_tick(g);
    else if (g->int8 == EP_INT8_FX)
        ep_adfx_tick(g);
    else
        ep_timer_tick(g);
}
