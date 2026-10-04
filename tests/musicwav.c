/* The title music on an AdLib, or its effects, to a WAV file, to listen to.
 *   musicwav DATA SECONDS OUT.wav [fx]
 * The core started as the original starts with an AdLib (ADBLUE.MID read from DATA), the
 * music on (or, with fx, flight's sound and every effect the game plays, one every 1.5 s), the
 * timer ticked at 1193182 / its divisor Hz; the chip's writes played by Nuked OPL3 at the
 * sample each was made. */
#include "ep_adlib.h"
#include "ep_boot.h"
#include "ep_sound.h"
#include "opl3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 44100

static const char *data;

static int data_read(void *ctx, const char *name, uint8_t *buf, int max)
{
    (void)ctx;
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", data, name);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)max, f);
    fclose(f);
    return n;
}

static const ep_io io = { NULL, NULL, data_read, NULL, NULL };

static void le(FILE *f, uint32_t v, int n)
{
    for (int k = 0; k < n; k++) fputc((int)(v >> 8 * k & 0xff), f);
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) return 2;
    int fx = argc == 5 && !strcmp(argv[4], "fx");
    data = argv[1];
    uint32_t samples = (uint32_t)(atof(argv[2]) * RATE);
    static ep_game g;
    static opl3_chip chip;
    g.io = &io;
    ep_boot(&g, 2, 1, 0, 0, 0);
    ep_music_start(&g);
    if (fx) ep_effects_on(&g); /* 4ac0 */
    static const uint8_t played[] = { 1,    2,    3,    4,    5,    6,    7,    8,    9,
                                      0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x11, 0x12, 0x13,
                                      0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b };
    unsigned next = 0;
    OPL3_Reset(&chip, RATE);
    FILE *f = fopen(argv[3], "wb");
    if (!f) return 2;
    fputs("RIFF", f);
    le(f, 36 + samples * 2, 4);
    fputs("WAVEfmt ", f);
    le(f, 16, 4);
    le(f, 1, 2);
    le(f, 1, 2);
    le(f, RATE, 4);
    le(f, RATE * 2, 4);
    le(f, 2, 2);
    le(f, 16, 2);
    fputs("data", f);
    le(f, samples * 2, 4);
    double clocks = 0, peak = 0; /* the timer's input clock at the next tick */
    for (uint32_t s = 0; s < samples; s++) {
        if (fx && s % (RATE * 3 / 2) == 0 && next < sizeof played) {
            printf("%5.1f s: effect %02x\n", (double)s / RATE, played[next]);
            ep_sound(&g, played[next++]);
        }
        while (clocks <= s * (1193182.0 / RATE)) {
            for (int k = 0; k < g.nopl; k++) OPL3_WriteReg(&chip, g.opl[k][0], g.opl[k][1]);
            g.nopl = 0;
            ep_pit_tick(&g);
            clocks += g.pit ? g.pit : 0x10000;
        }
        int16_t lr[2];
        OPL3_GenerateResampled(&chip, lr);
        int v = (lr[0] + lr[1]) / 2;
        if (abs(v) > peak) peak = abs(v);
        le(f, (uint32_t)(uint16_t)v, 2);
    }
    fclose(f);
    printf("%u samples, peak %.0f, timer divisor %u\n", samples, peak, g.pit);
    return 0;
}
