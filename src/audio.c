/* The sound (see audio.h). */
#include "audio.h"

#include "opl3.h"

#include <SDL.h>

#define RATE    44100
#define LATENCY (RATE / 20) /* the chip's writes play this long after they were made */
#define QUEUE   16384

static SDL_AudioDeviceID dev;
static volatile uint16_t divisor;
static volatile int on;
static double phase;

static opl3_chip chip;
static struct {
    Uint64 at; /* the sample it plays from */
    uint8_t reg, val;
} queue[QUEUE];
static int head, tail; /* under the device's lock */
static Uint64 played, last_at;

static void fill(void *u, Uint8 *stream, int len)
{
    (void)u;
    int16_t *s = (int16_t *)stream;
    int n = len / 2;
    double hz = divisor ? 1193182.0 / divisor : 0;
    for (int i = 0; i < n; i++, played++) {
        while (head != tail && queue[head].at <= played) {
            OPL3_WriteReg(&chip, queue[head].reg, queue[head].val);
            head = (head + 1) % QUEUE;
        }
        int16_t lr[2];
        OPL3_GenerateResampled(&chip, lr);
        int v = (lr[0] + lr[1]) / 2;
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
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = fill;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev) SDL_PauseAudioDevice(dev, 0);
}

void audio_speaker(uint16_t d, int o)
{
    divisor = d;
    on = o;
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
    for (int k = 0; k < n && (tail + 1) % QUEUE != head; k++) {
        queue[tail].at = at;
        queue[tail].reg = writes[k][0];
        queue[tail].val = writes[k][1];
        tail = (tail + 1) % QUEUE;
    }
    SDL_UnlockAudioDevice(dev);
}

void audio_quit(void)
{
    if (dev) SDL_CloseAudioDevice(dev);
}
