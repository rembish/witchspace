/* Elite Plus on an AdLib: the effects in flight, reconstructed from ELITE.EXE's music driver
 * (segment 2270, 1842..262e; see ep_adlib.h).
 *
 * An effect is a program in the driver's bank (cs:0ea0, a word a number): its first word
 * gives the voice (0..9) and a priority, then come notes (a byte under 80h and its length)
 * and commands (80h + n: the table at cs:263e). A voice's record (43h bytes, cs:13ee + 43h a
 * voice) keeps the program's place, its timing, the note and the hooks run each step
 * (vibrato, a pitch sweep, a register stepped through a table). Every 12th tick of its timer
 * (555h) the queue is emptied into voices, each voice steps at its own speed, and a slow
 * counter moves on; every 16th tick is the game's own (4a99).
 *
 * The state lives in the driver's segment; g->adlib.fx holds cs:1390..168f, the rest
 * (programs, tables, instruments) is read from the segment as loaded.
 */
#include "ep_adlib.h"
#include "ep_sound.h"
#include "ep_tables.h"

/* a voice's record */
enum {
    R_LEVEL = 0x00,     /* added to the carrier's attenuation (9f/a0 on another voice) */
    R_PC = 0x03,        /* w: the program's place, 0 when done */
    R_LEFT = 0x05,      /* steps left of the note */
    R_LOOP = 0x06,      /* 80/81: a loop's count */
    R_OCTAVE = 0x07,    /* added to the note's octave */
    R_PRIORITY = 0x08,  /* an effect of lower priority does not take the voice */
    R_DEPTH = 0x09,     /* 85/86: calls; their returns at 0a.. */
    R_RETURNS = 0x0a,   /* w each */
    R_TRANSPOSE = 0x12, /* semitones */
    R_SWEEP_RATE = 0x13,
    R_SWEEP_ACC = 0x14,
    R_SWEEP = 0x15, /* w: added to the frequency */
    R_VIB = 0x17,   /* w: the vibrato's step */
    R_VIB_SHIFT = 0x19,
    R_VIB_COUNT = 0x1a,
    R_VIB_LEN = 0x1b,
    R_VIB_DELAY = 0x1c,
    R_VIB_RATE = 0x1d,
    R_VIB_ACC = 0x1e,
    R_VIB_WAIT = 0x1f,
    R_VOLUME = 0x20,
    R_OFF_AT = 0x21, /* the key let go with this many steps left */
    R_FINE = 0x24,
    R_SPEED = 0x25, /* added to R_SPEED_ACC each tick: a step on its carry */
    R_SPEED_ACC = 0x26,
    R_FREQ = 0x27,  /* a0h's value */
    R_BLOCK = 0x28, /* b0h's: key, octave, frequency's top */
    R_HOOK1 = 0x29, /* w: code run each tick (2527 nothing, 2528 vibrato, 25aa sweep) */
    R_HOOK2 = 0x2b, /* w: (2581: a register through a table) */
    R_OFF_SCALE = 0x2d,
    R_MOD = 0x2e, /* the instrument's 40h (modulator) and 43h (carrier) */
    R_CAR = 0x2f,
    R_VOLUME2 = 0x30,
    R_ADDITIVE = 0x31, /* c0h's connection: the modulator heard too */
    R_SCALE_MOD = 0x32,
    R_SCALE_CAR = 0x33,
    R_VELOCITY = 0x34,
    R_OFF_AT2 = 0x35,
    R_RANDOM = 0x36, /* a note's length plus a random part */
    R_TABLE_RATE = 0x39,
    R_TABLE_ACC = 0x3a,
    R_TABLE_LEN = 0x3b,
    R_TABLE_AT = 0x3c,
    R_TABLE_REG = 0x3d,
    R_TABLE = 0x3e,  /* w */
    R_FOLLOW = 0x40, /* the speed taken from the shared rate */
    R_NOTE = 0x41,
    R_DETUNE = 0x42,
};

/* the driver's variables */
enum {
    WAIT = 0x1396,  /* the last note's length */
    VOICE = 0x1399, /* the voice being stepped */
    ACTIVE = 0x139a,
    RHYTHM = 0x139b, /* the rhythm section's drums struck (bdh) */
    RATE = 0x139c,
    BUZZ = 0x139d, /* voice 0's key toggled every BUZZ_LEN ticks */
    BUZZ_LEN = 0x139e,
    BUZZ_LEFT = 0x139f,
    RANDOM = 0x13a2, /* w */
    BUZZ_KEY = 0x13a4,
    DEPTH = 0x13a5, /* bdh's top bits */
    OPERATOR = 0x13a6,
    SLOW_LEN = 0x13b6, /* a counter stepped at the shared rate */
    SLOW_LEFT = 0x13b7,
    SLOW_ACC = 0x13b8,
    SLOW = 0x13b9,
    SLOW_SEEN = 0x13ba,
    BANK = 0x13bf, /* w */
    STEP_LEFT = 0x13c9,
    CLOCK_LEFT = 0x13ca,
    QUEUE_IN = 0x13da, /* w */
    QUEUE_OUT = 0x13dc,
    QUEUE = 0x13de,
};

#define NOTHING 0x2527

static uint8_t cb(const ep_game *g, uint16_t a)
{
    if (a >= EP_FX_CS && a < EP_FX_CS + EP_FX_SIZE) return g->adlib.fx[a - EP_FX_CS];
    return a < EP_DRV_SIZE ? ep_drv_initial[a] : 0;
}

static void sb(ep_game *g, uint16_t a, uint8_t v)
{
    if (a >= EP_FX_CS && a < EP_FX_CS + EP_FX_SIZE) g->adlib.fx[a - EP_FX_CS] = v;
}

static uint16_t cw(const ep_game *g, uint16_t a)
{
    return (uint16_t)(cb(g, a) | cb(g, (uint16_t)(a + 1)) << 8);
}

static void sw(ep_game *g, uint16_t a, uint16_t v)
{
    sb(g, a, (uint8_t)v);
    sb(g, (uint16_t)(a + 1), (uint8_t)(v >> 8));
}

static uint8_t voice(const ep_game *g) { return cb(g, VOICE); }

/* a voice's record (168c), its operators' offset (16a0) */
static uint16_t record(const ep_game *g, uint8_t v) { return cw(g, (uint16_t)(0x168c + (uint8_t)(v << 1))); }
static uint8_t operators(const ep_game *g, uint8_t v)
{
    return cb(g, (uint16_t)(0x1600 | (uint8_t)(0xa0 + v)));
}

/* shifts as a 386 does them (the count taken mod 32) */
static uint16_t shl16(uint16_t v, uint8_t n)
{
    n &= 31;
    return n >= 16 ? 0 : (uint16_t)(v << n);
}

static uint16_t shr16(uint16_t v, uint8_t n)
{
    n &= 31;
    return n >= 16 ? 0 : (uint16_t)(v >> n);
}

/* 262e */
static uint16_t random16(ep_game *g)
{
    uint16_t a = (uint16_t)(cw(g, RANDOM) + 0x9248);
    a = (uint16_t)(a >> 3 | a << 13);
    sw(g, RANDOM, a);
    return a;
}

/* 2326 (and the same at 23ea/2428): an attenuation kept to 0..3fh */
static uint8_t clamp(uint8_t al)
{
    if (al <= 0x3f) return al;
    return (int8_t)al >= 0 ? 0x3f : 0;
}

/* 2326: a sum (0..ffh) kept to 3fh */
static uint8_t clamp2(uint8_t al) { return al > 0x3f ? 0x3f : al; }

/* 23c9: the carrier's 43h; CL left as the scaling had it */
static uint8_t carrier(const ep_game *g, uint16_t di, uint8_t *cl)
{
    uint8_t al = (uint8_t)((cb(g, di + R_CAR) & 0x3f) + cb(g, di + R_VOLUME) + cb(g, di + R_LEVEL) +
                           cb(g, di + R_VOLUME2));
    if (cb(g, di + R_SCALE_CAR)) {
        *cl = (uint8_t)(cb(g, di + R_SCALE_CAR) + 1);
        al = (uint8_t)(al - (shl16(cb(g, di + R_VELOCITY), *cl) >> 8));
    }
    return (uint8_t)(clamp(al) | (cb(g, di + R_CAR) & 0xc0));
}

/* 2401: the modulator's 40h */
static uint8_t modulator(const ep_game *g, uint16_t di, uint8_t *cl)
{
    uint8_t al = cb(g, di + R_MOD) & 0x3f;
    if (cb(g, di + R_ADDITIVE))
        al = (uint8_t)(al + cb(g, di + R_VOLUME) + cb(g, di + R_LEVEL) + cb(g, di + R_VOLUME2));
    if (cb(g, di + R_SCALE_MOD)) {
        *cl = (uint8_t)(cb(g, di + R_SCALE_MOD) + 1);
        al = (uint8_t)(al - (shl16(cb(g, di + R_VELOCITY), *cl) >> 8));
    }
    return (uint8_t)(clamp(al) | (cb(g, di + R_MOD) & 0xc0));
}

/* 239a: the voice's loudness written */
static void loudness(ep_game *g, uint16_t di)
{
    uint8_t cl = 0;
    uint8_t al = carrier(g, di, &cl);
    ep_opl(g, (uint8_t)(0x43 + operators(g, voice(g))), al);
    if (!cb(g, di + R_ADDITIVE)) return;
    al = modulator(g, di, &cl);
    ep_opl(g, (uint8_t)(0x40 + operators(g, voice(g))), al);
}

/* 243f: a voice's record cleared for a new effect */
static void clear(ep_game *g, uint16_t di)
{
    for (uint16_t k = 3; k < 0x43; k++) sb(g, (uint16_t)(di + k), 0);
    sb(g, di + R_SPEED, 0xff);
    sb(g, di + R_PRIORITY, 0);
    sw(g, di + R_HOOK1, NOTHING);
    sw(g, di + R_HOOK2, NOTHING);
    sb(g, di + R_OFF_AT2, 1);
}

/* 2472: an instrument (11 bytes at si) on the operators at cl, the voice being stepped */
static void instrument(ep_game *g, uint16_t di, uint8_t cl, uint16_t si)
{
    ep_opl(g, (uint8_t)(0x20 + cl), cb(g, si++));
    ep_opl(g, (uint8_t)(0x23 + cl), cb(g, si++));
    uint8_t al = cb(g, si++);
    ep_opl(g, (uint8_t)(0xc0 + voice(g)), al);
    sb(g, di + R_ADDITIVE, al & 1);
    ep_opl(g, (uint8_t)(0xe0 + cl), cb(g, si++));
    ep_opl(g, (uint8_t)(0xe3 + cl), cb(g, si++));
    uint8_t reg = (uint8_t)(0x40 + cl);
    sb(g, di + R_MOD, cb(g, si++));
    ep_opl(g, reg, modulator(g, di, &cl));
    reg = (uint8_t)(0x43 + cl);
    sb(g, di + R_CAR, cb(g, si++));
    ep_opl(g, reg, carrier(g, di, &cl));
    ep_opl(g, (uint8_t)(0x60 + cl), cb(g, si++));
    ep_opl(g, (uint8_t)(0x63 + cl), cb(g, si++));
    ep_opl(g, (uint8_t)(0x80 + cl), cb(g, si++));
    ep_opl(g, (uint8_t)(0x83 + cl), cb(g, si));
}

/* 24de: voice cl silenced before an effect takes it (but a drum while the rhythm plays) */
static void silence(ep_game *g, uint8_t cl)
{
    if (cb(g, RHYTHM) && (int8_t)cl >= 6) return;
    uint8_t op = operators(g, cl);
    ep_opl(g, (uint8_t)(0x60 + op), 0xff);
    ep_opl(g, (uint8_t)(0x63 + op), 0xff);
    ep_opl(g, (uint8_t)(0x80 + op), 0xff);
    ep_opl(g, (uint8_t)(0x83 + op), 0xff);
    ep_opl(g, (uint8_t)(0xb0 + cl), 0);
    ep_opl(g, (uint8_t)(0xb0 + cl), 0x20);
}

/* 1871's and 1a71's start of effect `id` (cs:[bank + 2 id]): the voice taken if the priority
 * allows; 1871 also silences it first, but voice 9 */
static void start(ep_game *g, uint8_t id, int queued)
{
    uint16_t si = cw(g, (uint16_t)(id * 2 + cw(g, BANK)));
    uint16_t head = cw(g, si);
    uint8_t v = (uint8_t)head, priority = (uint8_t)(head >> 8);
    if (queued && v == 0) sb(g, BUZZ, 0);
    uint16_t di = cw(g, (uint16_t)(0x168c + v * 2));
    if ((int8_t)priority < (int8_t)cb(g, di + R_PRIORITY)) return;
    clear(g, di);
    sb(g, di + R_PRIORITY, priority);
    sw(g, di + R_PC, (uint16_t)(si + 2));
    sb(g, di + R_SPEED, 0xff);
    sb(g, di + R_SPEED_ACC, 0xff);
    sb(g, di + R_LEFT, 1);
    sb(g, ACTIVE, 1);
    if (!queued || (uint8_t)(v * 2) != 0x12) silence(g, v);
}

/* 21d0 (a note), 1ddb (the detune changed): the frequency and octave written */
static void frequency(ep_game *g, uint16_t di, uint8_t note, int detune_zero_skipped)
{
    /* the note: octave in the high nibble, semitone (0..11) in the low; the octave goes to
     * b0h's block bits (2..4), the semitone through the table at 16a9 to the frequency */
    uint8_t ch = (uint8_t)((note & 0xf0) + cb(g, di + R_OCTAVE));
    uint8_t al = (uint8_t)((note & 0x0f) + cb(g, di + R_TRANSPOSE));
    if ((int8_t)al >= 12) {
        al = (uint8_t)(al - 12);
        ch = (uint8_t)(ch + 0x10);
    } else if ((int8_t)al < 0) {
        al = (uint8_t)(al + 12);
        ch = (uint8_t)(ch - 0x10);
    }
    uint16_t f = (uint16_t)(cw(g, (uint16_t)(0x16a9 + (uint8_t)(al << 1))) + cb(g, di + R_FINE));
    uint16_t cx = (uint16_t)((((ch >> 2) & 0x1c) | (f >> 8)) << 8 | (f & 0xff));
    int8_t d = (int8_t)cb(g, di + R_DETUNE);
    if (!(detune_zero_skipped && d == 0)) {
        uint8_t n = cb(g, di + R_NOTE) & 0x0f;
        if (d >= 0) {
            uint16_t t = cw(g, (uint16_t)(0x11b6 + (n + 2) * 2));
            cx = (uint16_t)(cx + cb(g, (uint16_t)(t + (uint8_t)d)));
        } else {
            uint16_t t = cw(g, (uint16_t)(0x11b6 + n * 2));
            cx = (uint16_t)(cx - cb(g, (uint16_t)(t + (uint8_t)-d)));
        }
    }
    uint8_t block = (uint8_t)((cx >> 8) | (cb(g, di + R_BLOCK) & 0x20)), low = (uint8_t)cx;
    sb(g, di + R_BLOCK, block);
    sb(g, di + R_FREQ, low);
    ep_opl(g, (uint8_t)(0xa0 + voice(g)), low);
    ep_opl(g, (uint8_t)(0xb0 + voice(g)), block);
}

/* 2285: the key down; the vibrato's step from the frequency */
static void key_on(ep_game *g, uint16_t di)
{
    uint8_t block = cb(g, di + R_BLOCK) | 0x20;
    sb(g, di + R_BLOCK, block);
    ep_opl(g, (uint8_t)(0xb0 + voice(g)), block);
    uint8_t cl = (uint8_t)(9 - cb(g, di + R_VIB_SHIFT));
    uint16_t ax = shr16(cw(g, di + R_FREQ) & 0x3ff, cl);
    sw(g, di + R_VIB, ax & 0xff);
    sb(g, di + R_VIB_WAIT, cb(g, di + R_VIB_DELAY));
}

/* 22b5: the key let go (not voice 9, nor a drum while the rhythm plays); AL as it leaves it */
static void key_off(ep_game *g, uint16_t di, uint8_t *al)
{
    uint8_t v = voice(g);
    if (cb(g, RHYTHM) && (int8_t)v >= 6) return;
    if (v == 9) return;
    *al = cb(g, di + R_BLOCK) & 0xdf;
    sb(g, di + R_BLOCK, *al);
    ep_opl(g, (uint8_t)(0xb0 + v), *al);
}

/* 22dd: a note's length (and where its key is let go) */
static void length(ep_game *g, uint16_t di, uint8_t ah)
{
    sb(g, WAIT, ah);
    uint8_t bl = ah;
    if (cb(g, di + R_RANDOM)) {
        sb(g, di + R_LEFT, (uint8_t)(bl + (random16(g) & cb(g, di + R_RANDOM))));
        return;
    }
    if (cb(g, di + R_OFF_SCALE)) {
        uint8_t part = (uint8_t)(ah >> 3), al = 0;
        for (int k = cb(g, di + R_OFF_SCALE); k > 0; k--) al = (uint8_t)(al + part);
        sb(g, di + R_OFF_AT, al);
    }
    sb(g, di + R_LEFT, bl);
}

/* 1b03: the effect over */
static void finish(ep_game *g, uint16_t di)
{
    uint8_t al = 0;
    sb(g, di + R_PRIORITY, 0);
    sw(g, di + R_PC, 0);
    if (voice(g) != 9) key_off(g, di, &al);
}

/* b1..b3 (1fe8, 208c, 2130): the rhythm section's five levels, three parts each */
static const uint16_t drum_level[5][4] = {
    /* the part set, the other two, the register */
    { 0x13b3, 0x13a9, 0x13ae, 0x51 }, { 0x13b5, 0x13ab, 0x13b0, 0x55 }, { 0x13b4, 0x13aa, 0x13af, 0x52 },
    { 0x13b2, 0x13a8, 0x13ad, 0x54 }, { 0x13b1, 0x13a7, 0x13ac, 0x53 },
};

static void drum_levels(ep_game *g, uint8_t which, uint8_t mask, uint8_t al)
{
    for (int k = 0; k < 5; k++) {
        if (!(mask & 1 << k)) continue;
        const uint16_t *d = drum_level[k];
        uint8_t sum;
        if (which == 0) { /* 1fe8: the third part set */
            sb(g, d[0], al);
            sum = (uint8_t)(al + cb(g, d[1]) + cb(g, d[2]) + cb(g, d[0]));
            ep_opl(g, (uint8_t)d[3], clamp2(sum));
        } else if (which == 1) { /* 208c: added to the three, the sum kept as the second */
            sum = (uint8_t)(al + cb(g, d[1]) + cb(g, d[2]) + cb(g, d[0]));
            sb(g, d[2], clamp2(sum));
            ep_opl(g, (uint8_t)d[3], clamp2(sum));
        } else { /* 2130: the second part set */
            sb(g, d[2], al);
            sum = (uint8_t)(al + cb(g, d[1]) + cb(g, d[0]));
            ep_opl(g, (uint8_t)d[3], clamp2(sum));
        }
    }
}

/* 2528: the vibrato, after its delay, at its rate */
static void vibrato(ep_game *g, uint16_t di)
{
    if (cb(g, di + R_VIB_WAIT)) {
        sb(g, di + R_VIB_WAIT, (uint8_t)(cb(g, di + R_VIB_WAIT) - 1));
        return;
    }
    unsigned acc = cb(g, di + R_VIB_ACC) + cb(g, di + R_VIB_RATE);
    sb(g, di + R_VIB_ACC, (uint8_t)acc);
    if (acc < 0x100) return;
    uint16_t bx = cw(g, di + R_VIB);
    uint8_t n = (uint8_t)(cb(g, di + R_VIB_COUNT) - 1);
    sb(g, di + R_VIB_COUNT, n);
    if (!n) {
        bx = (uint16_t)-bx;
        sw(g, di + R_VIB, bx);
        sb(g, di + R_VIB_COUNT, cb(g, di + R_VIB_LEN));
    }
    uint16_t ax = (uint16_t)((cw(g, di + R_FREQ) & 0x3ff) + bx);
    sb(g, di + R_FREQ, (uint8_t)ax);
    sb(g, di + R_BLOCK, (uint8_t)((cb(g, di + R_BLOCK) & 0xfc) | ax >> 8));
    ep_opl(g, (uint8_t)(0xa0 + voice(g)), cb(g, di + R_FREQ));
    ep_opl(g, (uint8_t)(0xb0 + voice(g)), cb(g, di + R_BLOCK));
}

/* 2581: a register of the voice's operators stepped through a table */
static void table(ep_game *g, uint16_t di)
{
    unsigned acc = cb(g, di + R_TABLE_ACC) + cb(g, di + R_TABLE_RATE);
    sb(g, di + R_TABLE_ACC, (uint8_t)acc);
    if (acc < 0x100) return;
    uint8_t at = (uint8_t)(cb(g, di + R_TABLE_AT) - 1);
    sb(g, di + R_TABLE_AT, at);
    if (at & 0x80) sb(g, di + R_TABLE_AT, cb(g, di + R_TABLE_LEN));
    uint8_t reg = (uint8_t)(cb(g, di + R_TABLE_REG) + cb(g, OPERATOR));
    ep_opl(g, reg, cb(g, (uint16_t)(cw(g, di + R_TABLE) + cb(g, di + R_TABLE_AT))));
}

/* 25aa: the pitch swept, an octave up or down at the frequency's ends */
static void sweep(ep_game *g, uint16_t di)
{
    unsigned acc = cb(g, di + R_SWEEP_ACC) + cb(g, di + R_SWEEP_RATE);
    sb(g, di + R_SWEEP_ACC, (uint8_t)acc);
    if (acc < 0x100) return;
    uint16_t bx = (uint16_t)((cb(g, di + R_BLOCK) & 3) << 8 | cb(g, di + R_FREQ));
    uint8_t dl = cb(g, di + R_BLOCK) & 0x1c, dh = cb(g, di + R_BLOCK) & 0x20;
    uint16_t cx = cw(g, di + R_SWEEP);
    bx = (uint16_t)(bx + cx);
    if ((int16_t)cx >= 0) {
        if ((int16_t)bx >= 0x2de) {
            bx >>= 1;
            dl = (uint8_t)((dl + 4) & 0x1c);
        }
    } else if ((int16_t)bx <= 0x184) {
        bx = (uint16_t)(bx << 1);
        dl = (uint8_t)((dl - 4) & 0x1c);
    }
    bx &= 0x3ff;
    ep_opl(g, (uint8_t)(0xa0 + voice(g)), (uint8_t)bx);
    sb(g, di + R_FREQ, (uint8_t)bx);
    uint8_t bh = (uint8_t)(bx >> 8 | dl | dh);
    ep_opl(g, (uint8_t)(0xb0 + voice(g)), bh);
    sb(g, di + R_BLOCK, bh);
}

static void hook(ep_game *g, uint16_t di, uint16_t at)
{
    if (at == 0x2528) vibrato(g, di);
    if (at == 0x2581) table(g, di);
    if (at == 0x25aa) sweep(g, di);
}

/* 1ec6: the rhythm section's three instruments (voices 6, 7, 8: ah and the next two bytes)
 * and their frequencies (six bytes), the drums on */
static uint16_t rhythm(ep_game *g, uint16_t di, uint8_t ah, uint16_t si)
{
    uint8_t was_voice = cb(g, VOICE), was_op = cb(g, OPERATOR), id = ah;
    for (uint8_t v = 6; v <= 8; v++) {
        if (v > 6) id = cb(g, si++);
        sb(g, VOICE, v);
        sb(g, OPERATOR, cb(g, (uint16_t)(0x16a0 + v)));
        uint16_t ins = cw(g, (uint16_t)(0x26eb + id * 2));
        if (v == 6) sb(g, 0x13a7, cb(g, ins + 6));
        if (v == 7) {
            sb(g, 0x13a9, cb(g, ins + 5));
            sb(g, 0x13a8, cb(g, ins + 6));
        }
        if (v == 8) {
            sb(g, 0x13aa, cb(g, ins + 5));
            sb(g, 0x13ab, cb(g, ins + 6));
        }
        instrument(g, di, cb(g, OPERATOR), ins);
    }
    static const uint16_t shadow[3] = { 0x15a8, 0x15eb, 0x162e };
    for (int k = 0; k < 3; k++) {
        uint8_t al = cb(g, si++) & 0x2f;
        sb(g, shadow[k], al);
        ep_opl(g, (uint8_t)(0xb6 + k), al);
        ep_opl(g, (uint8_t)(0xa6 + k), cb(g, si++));
    }
    sb(g, RHYTHM, 0x20);
    sb(g, OPERATOR, was_op);
    sb(g, VOICE, was_voice);
    return si;
}

/* the commands' code (cs:263e + 2 n), as the table gives it */
enum {
    C_LOOP_SET = 0x1a64,
    C_LOOP = 0x1a69,
    C_START = 0x1a71,
    C_OFF_AT2 = 0x1ac0,
    C_JUMP = 0x1ac6,
    C_CALL = 0x1ad2,
    C_RETURN = 0x1aea,
    C_OCTAVE = 0x1afd,
    C_END = 0x1b03,
    C_REST = 0x1b19,
    C_WRITE = 0x1b36,
    C_SLUR = 0x1b3d,
    C_TRANSPOSE = 0x1b58,
    C_TABLE = 0x1b5e,
    C_STOP = 0x1b7b,
    C_INSTRUMENT = 0x1b9f,
    C_SWEEP = 0x1bb5,
    C_SWEEP_OFF = 0x1bcb,
    C_FINE = 0x1bd9,
    C_VIBRATO = 0x1bdf,
    C_PRIORITY = 0x1bfe,
    C_SLOW = 0x1c04,
    C_SLOW_WAIT = 0x1c20,
    C_VOLUME = 0x1c50,
    C_HOLD = 0x1c59,
    C_AGAIN = 0x1c6c,
    C_OFF_SCALE = 0x1c82,
    C_RATE = 0x1c8b,
    C_TABLE_OFF = 0x1c92,
    C_SPEED = 0x1c9b,
    C_VOLUME2 = 0x1ca1,
    C_LEVEL_OF = 0x1ca7,
    C_LEVEL_ADD = 0x1cc8,
    C_DEPTH_AM = 0x1ceb,
    C_DEPTH_VIB = 0x1d09,
    C_VOLUME_ADD = 0x1d27,
    C_KILL = 0x1d33,
    C_RANDOM_FREQ = 0x1d9f,
    C_VIBRATO_OFF = 0x1dd2,
    C_DETUNE = 0x1ddb,
    C_SPEED_RATE = 0x1e95,
    C_NOP = 0x1e9f,
    C_RANDOM = 0x1ea3,
    C_SPEED_ADD = 0x1ea9,
    C_RHYTHM = 0x1ec6,
    C_DRUMS = 0x1fa5,
    C_DRUMS_OFF = 0x1fd5,
    C_DRUM_SET = 0x1fe8,
    C_DRUM_ADD = 0x208c,
    C_DRUM_PART = 0x2130,
    C_FOLLOW = 0x21c0,
    C_SCALE = 0x21c6,
};

static int run(ep_game *g, uint16_t di);

/* 1991..1a57: one voice's tick: a step when its speed carries; when its note is over the
 * program runs on to the next note; then its hooks */
static void tick_voice(ep_game *g, uint16_t di)
{
    uint8_t v = voice(g);
    sb(g, OPERATOR, operators(g, v));
    if (cb(g, di + R_FOLLOW)) sb(g, di + R_SPEED, cb(g, RATE));
    unsigned acc = cb(g, di + R_SPEED_ACC) + cb(g, di + R_SPEED);
    sb(g, di + R_SPEED_ACC, (uint8_t)acc);
    if (acc >= 0x100) {
        uint8_t left = (uint8_t)(cb(g, di + R_LEFT) - 1);
        sb(g, di + R_LEFT, left);
        if (left) {
            uint8_t al = left;
            if (al == cb(g, di + R_OFF_AT)) key_off(g, di, &al);
            if (al == cb(g, di + R_OFF_AT2) && voice(g) != 9) key_off(g, di, &al);
        } else if (!run(g, di)) {
            return;
        }
    }
    hook(g, di, cw(g, di + R_HOOK1));
    hook(g, di, cw(g, di + R_HOOK2));
}

/* 19ee..1a52 and the commands: the program from the voice's place to its next note (1 with
 * the note playing: the hooks run; 0 when the voice is left at once) */
static int run(ep_game *g, uint16_t di)
{
    uint16_t si = cw(g, di + R_PC);
    for (int guard = 0; guard < 0x10000; guard++) { /* (a program run wild ends: the original's would not) */
        uint8_t al = cb(g, si), ah = cb(g, (uint16_t)(si + 1));
        si = (uint16_t)(si + 2);
        if (!(al & 0x80)) {         /* a note and its length */
            sb(g, di + R_NOTE, al); /* 21d0 keeps it first: the detune looks at it */
            frequency(g, di, al, 1);
            key_on(g, di);
            length(g, di, ah);
            if (cw(g, di + R_SCALE_MOD) || cw(g, di + R_SCALE_CAR)) { /* and its velocity */
                sb(g, di + R_VELOCITY, cb(g, si++));
                uint8_t cl = 0;
                ep_opl(g, (uint8_t)(cb(g, OPERATOR) + 0x43), carrier(g, di, &cl));
                ep_opl(g, (uint8_t)(cb(g, OPERATOR) + 0x40), modulator(g, di, &cl));
            }
            if (!cb(g, WAIT)) continue;
            sw(g, di + R_PC, si);
            return 1;
        }
        /* a command: 80h + n and an operand byte (ah); those without one step back (si--) */
        uint8_t n = al & 0x7f;
        uint16_t code = n > 0x70 ? C_END : cw(g, (uint16_t)(0x263e + n * 2));
        switch (code) {
        case C_LOOP_SET: sb(g, di + R_LOOP, ah); break;
        case C_LOOP: {
            uint8_t left = (uint8_t)(cb(g, di + R_LOOP) - 1);
            sb(g, di + R_LOOP, left);
            if (!left) {
                si++;
                break;
            }
            si = (uint16_t)(cb(g, si) << 8 | ah);
            break;
        }
        case C_JUMP: si = (uint16_t)(cb(g, si) << 8 | ah); break;
        case C_START:
            if (ah != 0xff) start(g, ah, 0);
            break;
        case C_OFF_AT2: sb(g, di + R_OFF_AT2, ah); break;
        case C_CALL: {
            si--;
            uint8_t depth = cb(g, di + R_DEPTH);
            sw(g, (uint16_t)(di + R_RETURNS + (uint8_t)(depth << 1)), (uint16_t)(si + 2));
            sb(g, di + R_DEPTH, (uint8_t)(depth + 1));
            si = cw(g, si);
            break;
        }
        case C_RETURN: {
            si--;
            uint8_t depth = (uint8_t)(cb(g, di + R_DEPTH) - 1);
            sb(g, di + R_DEPTH, depth);
            si = cw(g, (uint16_t)(di + R_RETURNS + (uint8_t)(depth << 1)));
            break;
        }
        case C_OCTAVE: sb(g, di + R_OCTAVE, ah); break;
        case C_REST:
            length(g, di, ah);
            sw(g, di + R_PC, si);
            if (voice(g) != 9) key_off(g, di, &al);
            if (cb(g, WAIT)) return 1;
            break;
        case C_WRITE: ep_opl(g, ah, cb(g, si++)); break;
        case C_SLUR: /* a note without the key let go */
            sb(g, di + R_NOTE, ah);
            frequency(g, di, ah, 1);
            length(g, di, cb(g, si++));
            sw(g, di + R_PC, si);
            if (cb(g, WAIT)) return 1;
            break;
        case C_TRANSPOSE: sb(g, di + R_TRANSPOSE, ah); break;
        case C_TABLE:
            sb(g, di + R_TABLE_ACC, ah);
            sb(g, di + R_TABLE_RATE, ah);
            al = cb(g, si++);
            sb(g, di + R_TABLE_LEN, al);
            sb(g, di + R_TABLE_AT, al);
            sb(g, di + R_TABLE_REG, cb(g, si++));
            sw(g, di + R_TABLE, cw(g, si));
            si = (uint16_t)(si + 2);
            sw(g, di + R_HOOK2, 0x2581);
            break;
        case C_STOP: {
            uint16_t other = record(g, ah);
            sb(g, other + R_LEFT, 0);
            sb(g, other + R_PRIORITY, 0);
            sw(g, other + R_PC, 0);
            break;
        }
        case C_INSTRUMENT: instrument(g, di, cb(g, OPERATOR), cw(g, (uint16_t)(0x26eb + ah * 2))); break;
        case C_SWEEP: {
            sb(g, di + R_SWEEP_RATE, ah);
            uint8_t hi = cb(g, si++), lo = cb(g, si++);
            sw(g, di + R_SWEEP, (uint16_t)(hi << 8 | lo));
            sw(g, di + R_HOOK1, 0x25aa);
            sb(g, di + R_SWEEP_ACC, 0xff);
            break;
        }
        case C_SWEEP_OFF:
            si--;
            sw(g, di + R_HOOK1, NOTHING);
            sw(g, di + R_SWEEP, 0);
            break;
        case C_FINE: sb(g, di + R_FINE, ah); break;
        case C_VIBRATO:
            sb(g, di + R_VIB_RATE, ah);
            sb(g, di + R_VIB_SHIFT, cb(g, si++));
            al = cb(g, si++);
            sb(g, di + R_VIB_COUNT, (uint8_t)(al + 1));
            sb(g, di + R_VIB_LEN, (uint8_t)(al << 1));
            sb(g, di + R_VIB_DELAY, cb(g, si++));
            sw(g, di + R_HOOK1, 0x2528);
            break;
        case C_PRIORITY: sb(g, di + R_PRIORITY, ah); break;
        case C_SLOW:
            sb(g, SLOW_LEN, ah >> 1);
            sb(g, SLOW_LEFT, ah >> 1);
            sb(g, SLOW_ACC, 0xff);
            sb(g, SLOW, 0);
            sb(g, SLOW_SEEN, 0);
            break;
        case C_SLOW_WAIT: /* wait for the slow counter's bit ah to change */
            if (!cb(g, SLOW_SEEN)) {
                if (!(ah & cb(g, SLOW))) sb(g, SLOW_SEEN, (uint8_t)(cb(g, SLOW_SEEN) + 1));
            } else if (ah & cb(g, SLOW)) {
                sb(g, SLOW_SEEN, 0);
                break;
            }
            sb(g, di + R_LEFT, 1);
            sw(g, di + R_PC, (uint16_t)(si - 2));
            return 0;
        case C_VOLUME:
            sb(g, di + R_VOLUME, ah);
            loudness(g, di);
            break;
        case C_HOLD:
            length(g, di, ah);
            sw(g, di + R_PC, si);
            if (cb(g, WAIT)) return 1;
            break;
        case C_AGAIN:
            length(g, di, ah);
            key_on(g, di);
            sw(g, di + R_PC, si);
            if (cb(g, WAIT)) return 1;
            break;
        case C_OFF_SCALE: sb(g, di + R_OFF_SCALE, ah & 7); break;
        case C_RATE: sb(g, RATE, ah); break;
        case C_TABLE_OFF:
            si--;
            sw(g, di + R_HOOK2, NOTHING);
            break;
        case C_SPEED: sb(g, di + R_SPEED, ah); break;
        case C_VOLUME2: sb(g, di + R_VOLUME2, ah); break;
        case C_LEVEL_OF:
        case C_LEVEL_ADD: { /* another voice's level set, or added to */
            uint8_t was = cb(g, VOICE);
            al = cb(g, si++);
            sb(g, VOICE, ah);
            uint16_t other = record(g, ah);
            sb(g, other + R_LEVEL, code == C_LEVEL_OF ? al : (uint8_t)(al + cb(g, other + R_LEVEL)));
            loudness(g, other);
            sb(g, VOICE, was);
            break;
        }
        case C_DEPTH_AM:
        case C_DEPTH_VIB: {
            uint8_t bit = code == C_DEPTH_AM ? 0x80 : 0x40;
            uint8_t bd = (uint8_t)((cb(g, DEPTH) & ~bit) | ((ah & 1) ? bit : 0));
            sb(g, DEPTH, bd);
            ep_opl(g, 0xbd, bd);
            break;
        }
        case C_VOLUME_ADD:
            sb(g, di + R_VOLUME, (uint8_t)(ah + cb(g, di + R_VOLUME)));
            loudness(g, di);
            break;
        case C_KILL: { /* voice ah stopped and silenced */
            uint8_t was = cb(g, VOICE);
            if (!ah) sb(g, BUZZ, 0);
            sb(g, VOICE, ah);
            uint16_t other = record(g, ah);
            sb(g, other + R_LEFT, 0);
            sb(g, other + R_PRIORITY, 0);
            sw(g, other + R_PC, 0);
            sb(g, other + R_LEVEL, 0);
            if (ah != 9) {
                uint8_t op = cb(g, (uint16_t)(0x16a0 + ah));
                ep_opl(g, (uint8_t)(0xc0 + ah), 0);
                ep_opl(g, (uint8_t)(0x43 + op), 0x3f);
                ep_opl(g, (uint8_t)(0x83 + op), 0xff);
                ep_opl(g, (uint8_t)(0xb0 + ah), 0);
            }
            sb(g, VOICE, was);
            break;
        }
        case C_RANDOM_FREQ: { /* the frequency plus a random part (mask ah, next byte) */
            uint16_t mask = (uint16_t)(ah << 8 | cb(g, si++));
            uint16_t r = random16(g) & mask;
            uint8_t block = cb(g, di + R_BLOCK);
            uint16_t bx = (uint16_t)(((block & 0x1f) << 8 | cb(g, di + R_FREQ)) + r);
            bx = (uint16_t)(bx | (block & 0x20) << 8);
            ep_opl(g, (uint8_t)(0xa0 + voice(g)), (uint8_t)bx);
            ep_opl(g, (uint8_t)(0xb0 + voice(g)), (uint8_t)(bx >> 8));
            break;
        }
        case C_VIBRATO_OFF:
            si--;
            sw(g, di + R_HOOK1, NOTHING);
            break;
        case C_DETUNE:
            sb(g, di + R_DETUNE, ah);
            frequency(g, di, cb(g, di + R_NOTE), 0);
            break;
        case C_SPEED_RATE:
            si--;
            sb(g, di + R_SPEED, cb(g, RATE));
            break;
        case C_NOP: si--; break;
        case C_RANDOM: sb(g, di + R_RANDOM, ah); break;
        case C_SPEED_ADD: {
            uint8_t speed = cb(g, di + R_SPEED), sum = (uint8_t)(ah + speed);
            if (ah & 0x80)
                speed = sum < speed ? sum : 1;
            else
                speed = sum >= speed ? sum : 0xff;
            sb(g, di + R_SPEED, speed);
            break;
        }
        case C_RHYTHM: si = rhythm(g, di, ah, si); break;
        case C_DRUMS: /* the drums ah struck: let go, then on */
            ep_opl(g, 0xbd, (uint8_t)((~(ah & 0x1f) & cb(g, RHYTHM)) | 0x20));
            sb(g, RHYTHM, cb(g, RHYTHM) | ah);
            ep_opl(g, 0xbd, cb(g, RHYTHM) | cb(g, DEPTH) | 0x20);
            break;
        case C_DRUMS_OFF:
            si--;
            sb(g, RHYTHM, 0);
            ep_opl(g, 0xbd, cb(g, DEPTH) & 0xc0);
            break;
        case C_DRUM_SET: drum_levels(g, 0, ah, cb(g, si++)); break;
        case C_DRUM_ADD: drum_levels(g, 1, ah, cb(g, si++)); break;
        case C_DRUM_PART: drum_levels(g, 2, ah, cb(g, si++)); break;
        case C_FOLLOW: sb(g, di + R_FOLLOW, ah); break;
        case C_SCALE:
            sb(g, di + R_SCALE_MOD, ah);
            sb(g, di + R_SCALE_CAR, cb(g, si++));
            break;
        default: /* C_END, and what the table does not name */ finish(g, di); return 0;
        }
    }
    finish(g, di);
    return 0;
}

/* 1984: every voice playing, 9 down to 0 */
static void step(ep_game *g)
{
    if (!cb(g, ACTIVE)) return;
    sb(g, VOICE, 9);
    for (;;) {
        uint16_t di = record(g, voice(g));
        if (cb(g, di + R_LEFT)) tick_voice(g, di);
        uint8_t v = (uint8_t)(voice(g) - 1);
        sb(g, VOICE, v);
        if (v & 0x80) return;
    }
}

/* 2337: the slow counter, at the shared rate */
static void slow(ep_game *g)
{
    unsigned acc = cb(g, SLOW_ACC) + cb(g, RATE);
    sb(g, SLOW_ACC, (uint8_t)acc);
    if (acc < 0x100) return;
    uint8_t left = (uint8_t)(cb(g, SLOW_LEFT) - 1);
    sb(g, SLOW_LEFT, left);
    if (left) return;
    sb(g, SLOW_LEFT, cb(g, SLOW_LEN));
    sb(g, SLOW, (uint8_t)(cb(g, SLOW) + 1));
}

/* 1871: the queued effects started */
static void dequeue(ep_game *g)
{
    while (cw(g, QUEUE_OUT) != cw(g, QUEUE_IN)) {
        start(g, cb(g, (uint16_t)(QUEUE + cw(g, QUEUE_OUT))), 1);
        sw(g, QUEUE_OUT, (uint16_t)((cw(g, QUEUE_OUT) + 1) & 0xf));
    }
}

void ep_adfx_init(ep_game *g)
{
    sb(g, ACTIVE, 0);
    sw(g, RANDOM, 0x1234);
    ep_opl(g, 0x01, 0x20);
    ep_opl(g, 0x08, 0x00);
    ep_opl(g, 0xbd, 0x00);
    sb(g, BUZZ, 0);
    for (uint8_t v = 10; v > 0; v--) {
        uint8_t n = (uint8_t)(v - 1);
        if (n != 9) {
            ep_opl(g, (uint8_t)(cb(g, (uint16_t)(0x16a0 + n)) + 0x40), 0x3f);
            ep_opl(g, (uint8_t)(cb(g, (uint16_t)(0x16a0 + n)) + 0x43), 0x3f);
        }
        clear(g, record(g, n));
    }
}

void ep_adfx_install(ep_game *g)
{
    sw(g, BANK, cw(g, 0x26e0)); /* the first bank */
    g->int8 = EP_INT8_FX;       /* (the old vector kept at cs:13c1, not modelled) */
    ep_adfx_init(g);
    g->pit = 0x555;
    sw(g, QUEUE_OUT, 0);
    sw(g, QUEUE_IN, 0);
}

void ep_adfx_queue(ep_game *g, uint8_t id)
{
    uint16_t at = cw(g, QUEUE_IN);
    sb(g, (uint16_t)(QUEUE + at), id);
    sw(g, QUEUE_IN, (uint16_t)((at + 1) & 0xf));
}

void ep_adfx_tick(ep_game *g)
{
    if (cb(g, BUZZ)) {
        uint8_t left = (uint8_t)(cb(g, BUZZ_LEFT) - 1);
        sb(g, BUZZ_LEFT, left);
        if (!left) { /* voice 0's key toggled */
            uint16_t di = record(g, 0);
            sb(g, BUZZ_KEY, cb(g, BUZZ_KEY) ^ 0xff);
            uint8_t al = (uint8_t)((cb(g, di + R_BLOCK) & 0x1f) | (cb(g, BUZZ_KEY) & 0x20));
            sb(g, di + R_BLOCK, al);
            ep_opl(g, 0xb0, al);
            sb(g, BUZZ_LEFT, cb(g, BUZZ_LEN));
        }
    }
    uint8_t left = (uint8_t)(cb(g, STEP_LEFT) - 1);
    sb(g, STEP_LEFT, left);
    if (!left) { /* 1842 */
        sb(g, STEP_LEFT, 0x0c);
        dequeue(g);
        step(g);
        slow(g);
    }
    left = (uint8_t)(cb(g, CLOCK_LEFT) - 1);
    sb(g, CLOCK_LEFT, left);
    /* 4a99: the game's own interrupt; 16 x 555h is 5550h, not 5555h, so with the effects in
     * the game's clock runs a little fast */
    if (!left) {
        sb(g, CLOCK_LEFT, 0x10);
        ep_timer_tick(g);
    }
}
