/* Elite Plus sound, reconstructed from ELITE.EXE (see ep_sound.h). */
#include "ep_sound.h"

#include "ep_adlib.h"
#include "ep_tables.h"

/* the sequences' data (ds:4803..): static but for the note 4e1a puts into sequence 9 */
static uint8_t data_byte(const ep_game *g, uint16_t addr)
{
    if (addr == 0x4f74) return g->f.surface_note;
    return addr < EP_DS_INITIAL ? ep_ds_initial[addr] : 0;
}

static uint16_t data_word(const ep_game *g, uint16_t addr)
{
    return (uint16_t)(data_byte(g, addr) | data_byte(g, (uint16_t)(addr + 1)) << 8);
}

static void speaker_pitch(ep_game *g, uint16_t divisor) { g->speaker = divisor; } /* 43h b6, 42h */

/* ds:4801, the sound device: 0 a Roland, 1 an AdLib, 2 the PC speaker */
static int speaker(const ep_game *g) { return g->f.sound_device == 2; }

/* 4c98's speaker part: sequence n (ds:4f7f) from the start */
static void speaker_start(ep_game *g, uint16_t seq)
{
    ep_flight *f = &g->f;
    f->sound_mode |= 1;
    f->snd_seq = seq;
    f->sound_mode = (uint8_t)((f->sound_mode | 2) & 0xfe);
}

void ep_sound(ep_game *g, uint8_t id)
{
    ep_event_add(g, EP_EV_SOUND, id);
    if (!speaker(g)) { /* 4ccd: an AdLib's effects take the number as it is */
        if (g->f.sound_device == 1 && !g->f.sound_off && id) ep_adfx_queue(g, id);
        return;
    }
    uint8_t n = id;
    if (!(n & 0x80)) {
        n = ep_ds_initial[0x45c0 + n]; /* xlat */
        if (n == 0xff) return;
    }
    speaker_start(g, data_word(g, (uint16_t)(0x4f7f + 2 * (n & 0x7f))));
}

void ep_sound_marked(ep_game *g, uint8_t id)
{
    g->f.snd_marked = 1;
    ep_sound(g, id);
}

void ep_sound_laser(ep_game *g, uint8_t fired)
{
    ep_flight *f = &g->f;
    if (speaker(g)) {
        if (f->snd_marked && !(f->sound_mode & 1)) return;
        f->snd_marked = 0;
    }
    ep_sound(g, (uint8_t)(0x14 + (fired & 3)));
}

void ep_sound_under_fire(ep_game *g)
{
    ep_flight *f = &g->f;
    if (speaker(g) && !(f->sound_mode & 1) && f->snd_seq < 0x4f4b) return;
    ep_sound(g, f->sound_device == 1 ? 0x17 : 0x10);
}

void ep_surface_sound(ep_game *g, uint16_t size)
{
    ep_event_add(g, EP_EV_SURFACE_SOUND, size);
    unsigned al = ((size & 0xffu) >> 1) + 0xa0u;
    uint8_t ah = al > 0xff ? 0xff : (uint8_t)al;
    if (ah >= 0xfa) ah = 0xfa;
    if (speaker(g)) { /* 4cfe: sequence 9 (ds:4f73) at that pitch */
        ep_flight *f = &g->f;
        f->sound_mode |= 1;
        f->surface_note = ah; /* its second byte, ds:4f74 */
        f->snd_seq = data_word(g, 0x4f7f + 2 * 9);
        f->sound_mode = (uint8_t)((f->sound_mode | 2) & 0xfe);
        return;
    }
    ep_sound(g, (uint8_t)(ah < 0xb4 ? 7 : ah < 0xc8 ? 8 : ah < 0xdc ? 9 : 0x0a));
}

void ep_launch_sound(ep_game *g)
{
    ep_flight *f = &g->f;
    if (speaker(g) || f->sound_off) return;
    ep_sound(g, 0x11);
    uint16_t ticks = f->sound_device ? 0x78 : 0x23a; /* a Roland's is the longer wait */
    ep_event_add(g, EP_EV_WAIT, ticks);
    if (g->wait) g->wait(g, g->clock + ticks, 0); /* 4e88 */
}

void ep_music_start(ep_game *g)
{
    ep_event_add(g, EP_EV_MUSIC, 2);
    g->f.music_on = 1;
    if (speaker(g)) {
        ep_sound(g, 0x81);
        return;
    }
    if (g->f.sound_device == 1) ep_adlib_game_timer(g); /* 1819 */
    ep_adlib_stop(g);                                   /* 0045 */
    g->adlib.drv[0xb5b8 - EP_ADLIB_DS] = 0;
    if (!g->f.sound_off) ep_adlib_music(g); /* 0000 */
}

void ep_music_stop(ep_game *g)
{
    ep_event_add(g, EP_EV_MUSIC, 1);
    g->f.music_on = 0;
    if (speaker(g))
        ep_sound(g, 0);
    else
        ep_adlib_stop(g); /* 0045 */
}

void ep_effects_on(ep_game *g)
{
    if (g->f.sound_device == 0) { /* a Roland: */
        ep_adlib_stop(g);         /* 0045 */
        g->adlib.drv[0xb5b8 - EP_ADLIB_DS] = 1;
        return; /* 008f, its effects: not ported */
    }
    if (g->f.sound_device != 1) return;
    ep_adlib_stop(g);   /* 0045 */
    ep_adfx_install(g); /* 17c6 */
}

void ep_music_switch(ep_game *g, uint8_t off)
{
    ep_event_add(g, EP_EV_MUSIC, off);
    if (speaker(g) || !g->f.music_on) return;
    if (off)
        ep_adlib_stop(g); /* 0045 */
    else
        ep_music_start(g); /* the music again where it was on */
}

void ep_music_again(ep_game *g)
{
    if (speaker(g) && (g->f.sound_mode & 1)) ep_music_start(g);
}

/* 4aea: the sequence's next note */
static void next_note(ep_game *g)
{
    ep_flight *f = &g->f;
    f->sound_mode &= 0xe4;
    uint16_t si = f->snd_seq;
    uint8_t al = data_byte(g, si++);
    if (al == 0xff) { /* 4b4b: the end */
        f->sound_mode |= 5;
        g->speaker_on = 0;
        return;
    }
    if (al == 0xfe) { /* 4b57: a rest */
        f->sound_mode |= 4;
        g->speaker_on = 0;
        f->snd_rest = data_byte(g, si++);
        f->snd_seq = si;
        return;
    }
    f->snd_pattern = data_word(g, (uint16_t)(0x4fce + 2 * al));
    uint8_t note = data_byte(g, si), length = data_byte(g, (uint16_t)(si + 1));
    si = (uint16_t)(si + 2);
    if (note != 0xff) {
        f->snd_note = note;
        speaker_pitch(g, data_word(g, (uint16_t)(0x4601 + 2 * note)));
    }
    f->snd_length = length;
    if (!length) f->sound_mode |= 0x10;
    f->snd_rest = 0;
    f->snd_wait = 0;
    f->snd_loop_sp = 0x45f4;
    f->snd_seq = si;
    if (f->sound_mode & 4) { /* 4c85 */
        f->sound_mode &= 0xfb;
        g->speaker_on = 1;
    }
}

/* the loop stack (ds:45f4, three bytes an entry: where, how many times more), by address: an
 * unbalanced pattern reaches below it, into the sequencer's other bytes */
static uint8_t loop_get(const ep_game *g, uint16_t addr)
{
    const ep_flight *f = &g->f;
    if (addr >= 0x45f4 && addr < 0x4600) return f->snd_loops[addr - 0x45f4];
    switch (addr) {
    case 0x45ed: return (uint8_t)f->snd_pattern;
    case 0x45ee: return (uint8_t)(f->snd_pattern >> 8);
    case 0x45ef: return f->snd_note;
    case 0x45f0: return f->snd_length;
    case 0x45f1: return f->snd_rest;
    case 0x45f2: return (uint8_t)f->snd_loop_sp;
    case 0x45f3: return (uint8_t)(f->snd_loop_sp >> 8);
    case 0x4600: return f->snd_wait;
    default: return 0;
    }
}

static void loop_set(ep_game *g, uint16_t addr, uint8_t v)
{
    ep_flight *f = &g->f;
    if (addr >= 0x45f4 && addr < 0x4600) {
        f->snd_loops[addr - 0x45f4] = v;
        return;
    }
    switch (addr) {
    case 0x45ed: f->snd_pattern = (uint16_t)((f->snd_pattern & 0xff00) | v); break;
    case 0x45ee: f->snd_pattern = (uint16_t)((f->snd_pattern & 0xff) | v << 8); break;
    case 0x45ef: f->snd_note = v; break;
    case 0x45f0: f->snd_length = v; break;
    case 0x45f1: f->snd_rest = v; break;
    case 0x45f2: f->snd_loop_sp = (uint16_t)((f->snd_loop_sp & 0xff00) | v); break;
    case 0x45f3: f->snd_loop_sp = (uint16_t)((f->snd_loop_sp & 0xff) | v << 8); break;
    case 0x4600: f->snd_wait = v; break;
    default: break;
    }
}

/* 4b6b: a tick of the note */
static void note_tick(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->snd_rest) {
        if (--f->snd_rest == 0) f->sound_mode |= 2;
        return;
    }
    if (f->snd_wait) {
        f->snd_wait--;
    } else {
        uint16_t si = f->snd_pattern;
        for (int guard = 0; guard < 256; guard++) { /* (ours: a pattern run wild ends) */
            uint8_t al = data_byte(g, si++);
            if (al == 0x80) { /* a wait */
                f->snd_wait = data_byte(g, si++);
                f->snd_pattern = si;
                break;
            }
            if (al == 0x81) { /* a loop: how many times */
                uint16_t bx = f->snd_loop_sp;
                uint8_t n = data_byte(g, si++);
                loop_set(g, bx, (uint8_t)si);
                loop_set(g, (uint16_t)(bx + 1), (uint8_t)(si >> 8));
                loop_set(g, (uint16_t)(bx + 2), n);
                f->snd_loop_sp = (uint16_t)(bx + 3);
                continue;
            }
            if (al == 0x82) { /* its end */
                uint16_t bx = f->snd_loop_sp;
                uint8_t n = (uint8_t)(loop_get(g, (uint16_t)(bx - 1)) - 1);
                loop_set(g, (uint16_t)(bx - 1), n);
                if (n)
                    si = (uint16_t)(loop_get(g, (uint16_t)(bx - 3)) | loop_get(g, (uint16_t)(bx - 2)) << 8);
                else
                    f->snd_loop_sp = (uint16_t)(f->snd_loop_sp - 3);
                continue;
            }
            if (al == 0x83) { /* the held note ends */
                f->snd_length = 0;
                f->sound_mode &= 0xef;
                break;
            }
            if (al == 0x7f) {
                f->sound_mode |= 8;
                continue;
            }
            if (al == 0x7e) {
                f->sound_mode &= 0xf7;
                continue;
            }
            f->snd_note = (uint8_t)(f->snd_note + al); /* a bend */
            f->snd_pattern = si;
            if (!(f->sound_mode & 8)) speaker_pitch(g, data_word(g, (uint16_t)(0x4601 + 2 * f->snd_note)));
            break;
        }
    }
    /* 4c44: noise: a subtract-with-borrow generator (ds:45dc), its low six bits a note
     * above the current one, its high byte added to that note's divisor */
    if (f->sound_mode & 8) {
        uint16_t bx = f->snd_noise;
        uint16_t ax = (uint16_t)((bx & 0xff) << 8 | 0xfd);
        uint8_t cl = (uint8_t)(bx >> 8);
        cl = (uint8_t)(cl - (ax < bx));
        ax = (uint16_t)(ax - bx);
        cl = (uint8_t)(cl - (ax < bx));
        ax = (uint16_t)(ax - bx);
        uint16_t was = ax;
        ax = (uint16_t)(ax - cl);
        ax = (uint16_t)(ax + (was < cl));
        f->snd_noise = ax;
        uint8_t index = (uint8_t)((ax & 0x3f) + f->snd_note);
        uint16_t d = data_word(g, (uint16_t)(0x4601 + 2 * index));
        unsigned lo = (d & 0xff) + (ax >> 8);
        speaker_pitch(g, (uint16_t)((d & 0xff00) + (lo & 0xff) + (lo > 0xff ? 0x100 : 0)));
    }
    if (f->sound_mode & 0x10) return; /* 4c2b */
    if (!f->snd_length) {
        f->sound_mode |= 2;
        return;
    }
    f->snd_length--;
}

void ep_key_event(ep_game *g, uint8_t scancode)
{
    ep_input *in = &g->in;
    if (scancode == 0xe0) {
        in->e0 = (uint8_t)~in->e0;
        return;
    }
    if (scancode == 0x45) in->num_lock = (uint8_t)~in->num_lock;
    uint8_t key = scancode & 0x7f, up = scancode & 0x80;
    if (key == 0x2a) return; /* the left shift (also sent around E0h keys) */
    in->key[key] = up;
    if (up) return;
    in->last_scan = (uint16_t)(0x020d + key);
    uint8_t code = ep_ds_initial[0x0cad + key];
    if (code != 0xff) in->last_key = code;
}

void ep_timer_tick(ep_game *g)
{
    ep_flight *f = &g->f;
    f->snd_ticks++;
    if (!f->paused) {
        g->clock++;
        if (f->note_ticks) f->note_ticks--;
    }
    if (f->sound_off || (f->sound_mode & 1)) return; /* 4a75 */
    if (f->sound_mode & 2) next_note(g);
    note_tick(g);
}
