/* The sound (see audio.h): the witchspace frontend's, on SDL2. The speaker's square wave and
 * the chip are mixed in the device's callback; the chip's writes wait in a queue, each marked
 * with the sample it plays from. The title's theme, if one was given, is an MP3 decoded in the
 * callback as it plays (dr_mp3). */
#include "audio.h"

#include "opl3.h"

#include "dr_mp3.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define RATE    44100
#define LATENCY (RATE / 20) /* the chip's writes play this long after they were made */
#define QUEUE   16384

static SDL_AudioDeviceID dev;
static uint16_t divisor; /* the speaker: set under the device's lock, read in the callback */
static int on;
static double phase;

static opl3_chip chip;
static struct {
    Uint64 at; /* the sample it plays from */
    uint8_t reg, val;
} queue[QUEUE];
static int head, tail; /* under the device's lock */
static Uint64 played, last_at;

static drmp3 theme;                 /* the theme's decoder, over theme_data */
static uint8_t *theme_data;         /* the MP3 file, while it is loaded */
static int theme_on;                /* playing (under the device's lock) */
static double theme_step;           /* the theme's samples per output sample (its rate over RATE) */
static double theme_at;             /* where in theme_pcm the next output sample is */
static int16_t theme_pcm[2 * 1152]; /* decoded frames (interleaved), theme_n of them */
static int theme_n;

/* the theme's next sample (its channels mixed), from the start again at its end */
static int theme_sample(void)
{
    while (theme_at >= theme_n) {
        theme_at -= theme_n;
        theme_n = (int)drmp3_read_pcm_frames_s16(&theme, 1152, theme_pcm);
        if (!theme_n) { /* the end: again from the start */
            if (!drmp3_seek_to_pcm_frame(&theme, 0)) return 0;
            theme_n = (int)drmp3_read_pcm_frames_s16(&theme, 1152, theme_pcm);
            if (!theme_n) return 0;
        }
    }
    int k = (int)theme_at, ch = (int)theme.channels;
    int v = theme_pcm[k * ch];
    if (ch > 1) v = (v + theme_pcm[k * ch + 1]) / 2;
    theme_at += theme_step;
    return v;
}

static void fill(void *u, Uint8 *stream, int len)
{
    (void)u;
    int16_t *s = (int16_t *)stream;
    int n = len / 2;
    double hz = divisor ? 1193182.0 / divisor : 0; /* SDL holds the lock around the callback */
    for (int i = 0; i < n; i++, played++) {
        while (head != tail && queue[head].at <= played) {
            OPL3_WriteReg(&chip, queue[head].reg, queue[head].val);
            head = (head + 1) % QUEUE;
        }
        int16_t lr[2];
        OPL3_GenerateResampled(&chip, lr);
        int v = (lr[0] + lr[1]) / 2;
        if (theme_on) v += theme_sample();
        if (on && hz >= 20 && hz <= 20000) {
            phase += hz / RATE;
            if (phase >= 1) phase -= (int)phase;
            v += phase < 0.5 ? 2500 : -2500;
        }
        s[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

void audio_init(void)
{
    OPL3_Reset(&chip, RATE);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "no sound: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = fill;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev)
        SDL_PauseAudioDevice(dev, 0);
    else
        fprintf(stderr, "no sound: %s\n", SDL_GetError());
}

void audio_speaker(uint16_t d, int o)
{
    if (d == divisor && o == on) return; /* as often as the timer ticks: lock only on a change */
    if (dev) SDL_LockAudioDevice(dev);
    divisor = d;
    on = o;
    if (dev) SDL_UnlockAudioDevice(dev);
}

void audio_opl(const uint8_t (*writes)[2], int n, double ago)
{
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    Uint64 at = played + LATENCY;
    Uint64 back = (Uint64)(ago * RATE);
    at = back < at ? at - back : 0;
    if (at < last_at) at = last_at; /* in the order made */
    last_at = at;
    for (int k = 0; k < n; k++) {
        /* full (the device stalled): the oldest goes to the chip now, its timing lost, so that
           no write is lost (a lost key-off would leave a note sounding) */
        if ((tail + 1) % QUEUE == head) {
            OPL3_WriteReg(&chip, queue[head].reg, queue[head].val);
            head = (head + 1) % QUEUE;
        }
        queue[tail].at = at;
        queue[tail].reg = writes[k][0];
        queue[tail].val = writes[k][1];
        tail = (tail + 1) % QUEUE;
    }
    SDL_UnlockAudioDevice(dev);
}

int audio_theme_load(uint8_t *data, size_t len)
{
    if (dev) SDL_LockAudioDevice(dev);
    if (theme_data) drmp3_uninit(&theme);
    free(theme_data);
    theme_data = NULL;
    theme_on = 0;
    /* the decoder set up in place: it keeps pointers into itself */
    int ok = drmp3_init_memory(&theme, data, len, NULL);
    if (ok && (theme.channels < 1 || theme.channels > 2 || !theme.sampleRate)) {
        drmp3_uninit(&theme);
        ok = 0;
    }
    if (ok) {
        theme_data = data;
        theme_step = (double)theme.sampleRate / RATE;
        theme_at = theme_n = 0;
    }
    if (dev) SDL_UnlockAudioDevice(dev);
    return ok;
}

void audio_theme(int play)
{
    if (!theme_data || !dev || play == theme_on) return;
    SDL_LockAudioDevice(dev);
    if (play) { /* from the start */
        drmp3_seek_to_pcm_frame(&theme, 0);
        theme_at = theme_n = 0;
    }
    theme_on = play;
    SDL_UnlockAudioDevice(dev);
}

void audio_quit(void)
{
    if (dev) SDL_CloseAudioDevice(dev);
    if (theme_data) drmp3_uninit(&theme);
    free(theme_data);
    theme_data = NULL;
}
