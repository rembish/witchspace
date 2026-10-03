/* The PC speaker (see audio.h). */
#include "audio.h"

#include <SDL.h>

#define RATE 44100

static SDL_AudioDeviceID dev;
static volatile uint16_t divisor;
static volatile int on;
static double phase;

static void fill(void *u, Uint8 *stream, int len)
{
    (void)u;
    int16_t *s = (int16_t *)stream;
    int n = len / 2;
    double hz = divisor ? 1193182.0 / divisor : 0;
    for (int i = 0; i < n; i++) {
        if (!on || hz < 20 || hz > 20000) {
            s[i] = 0;
            continue;
        }
        phase += hz / RATE;
        if (phase >= 1) phase -= (int)phase;
        s[i] = phase < 0.5 ? 2500 : -2500;
    }
}

void audio_init(void)
{
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

void audio_quit(void)
{
    if (dev) SDL_CloseAudioDevice(dev);
}
