/* Short clips of the game, made from your copy (nothing of it is distributed): scripted
 * scenes played through the core, drawn by the frontend's software renderer (src/screen.c),
 * the AdLib's sound played by Nuked OPL3.
 *   ep_clips DATA OUTDIR [SCENE...]
 * writes OUTDIR/SCENE.rgb (320 x 200 RGB frames, 30 a second) and OUTDIR/SCENE.wav (44100 Hz
 * mono) for each scene: title, launch, screens, docking, hyperspace (all by default).
 * `make clips` turns them into video (ffmpeg); see the Makefile. */
#include "ep_adlib.h"
#include "ep_boot.h"
#include "ep_commands.h"
#include "ep_frame.h"
#include "ep_sound.h"
#include "ep_station.h"
#include "ep_tables.h"
#include "ep_title.h"
#include "grf.h"
#include "opl3.h"
#include "screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIT_HZ 1193182.0
#define FPS    30
#define RATE   44100

static ep_game g;
static opl3_chip chip;
static FILE *video, *audio;
static uint32_t rgba[SCREEN_W * SCREEN_H];
static double now, next_frame, next_sample; /* the timer's input clock, in clocks */
static double record_from;                  /* nothing is written before this time */
static uint32_t samples;
static double phase; /* the speaker's square wave */

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

/* the picture now, as frames up to the present time */
static void frames(void)
{
    while (next_frame <= now) {
        if (next_frame < record_from) {
            next_frame += PIT_HZ / FPS;
            continue;
        }
        for (int k = 0; k < SCREEN_W * SCREEN_H; k++) {
            uint8_t px[3] = { (uint8_t)(rgba[k] >> 16), (uint8_t)(rgba[k] >> 8), (uint8_t)rgba[k] };
            fwrite(px, 1, 3, video);
        }
        next_frame += PIT_HZ / FPS;
    }
}

/* one tick of the timer: its sound up to now, the chip's writes, the core's interrupt */
static void tick(void)
{
    double hz = g.speaker ? PIT_HZ / g.speaker : 0;
    while (next_sample <= now) {
        int16_t lr[2];
        OPL3_GenerateResampled(&chip, lr);
        int v = (lr[0] + lr[1]) / 2;
        if (g.speaker_on && hz >= 20 && hz <= 20000) {
            phase += hz / RATE;
            if (phase >= 1) phase -= (int)phase;
            v += phase < 0.5 ? 2500 : -2500;
        }
        int16_t s = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        if (next_sample >= record_from) {
            fwrite(&s, 2, 1, audio);
            samples++;
        }
        next_sample += PIT_HZ / RATE;
    }
    for (int k = 0; k < g.nopl; k++) OPL3_WriteReg(&chip, g.opl[k][0], g.opl[k][1]);
    g.nopl = 0;
    ep_pit_tick(&g);
    now += g.pit ? g.pit : 0x10000;
}

/* the screen as the core has drawn it */
static void show(void)
{
    screen_draw(&g);
    ep_output_begin(&g);
    screen_rgba(rgba);
    frames();
}

/* time passing with the screen as it is */
static void hold(double seconds)
{
    double until = now + seconds * PIT_HZ;
    while (now < until) {
        tick();
        frames();
    }
}

/* the core waits on the timer (a frame flip, a sound playing out) */
static void wait_for(ep_game *gg, uint32_t until, int shown)
{
    if (shown) show();
    while (gg->clock < until && until - gg->clock < 100000 && !gg->f.paused) {
        tick();
        frames();
    }
}

static void press(uint8_t scancode)
{
    ep_key_event(&g, scancode);
    ep_key_event(&g, (uint8_t)(scancode | 0x80));
}

static void hold_key(uint8_t scancode, int down)
{
    ep_key_event(&g, (uint8_t)(scancode | (down ? 0 : 0x80)));
}

/* flight frames: a frame is shown at its flip (wait_for) */
static int fly(int n)
{
    for (int k = 0; k < n; k++) {
        int r = ep_flight_frame(&g);
        if (r != EP_FRAME_NEXT) return r;
    }
    return EP_FRAME_NEXT;
}

/* a dialogue answered with Enter until it is done */
static void enter_through(int w)
{
    for (int k = 0; w != EP_WAIT_NONE && k < 50; k++) {
        show();
        hold(0.6);
        w = ep_station_key(&g, 0x0d);
    }
    show();
}

/* a new game at the station, launched into space */
static void launch(int recorded)
{
    enter_through(ep_start_game(&g, 12, 3, 4, 5));
    if (recorded) hold(1.5);
    press(0x3b); /* F1: launch */
    ep_station_idle(&g);
    fly(130);
    if (!recorded) return;
    hold_key(0x4b, 1); /* rolling left, then climbing away from the planet */
    fly(40);
    hold_key(0x4b, 0);
    hold_key(0x50, 1);
    fly(110);
    hold_key(0x50, 0);
    fly(40);
}

/* the command on the bar with this id, pressed by its key (1..9, 0, -, =) */
static void command(uint8_t id)
{
    static const uint8_t keys[12] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=' };
    for (int k = 0; k < 12; k++)
        if (g.f.bar_active[k] == id) {
            g.in.last_key = keys[k];
            return;
        }
}

static void scene_title(void)
{
    int w = ep_title_open(&g); /* the opening picture and the music */
    for (int k = 0; w != EP_WAIT_NONE && k < 400; k++) {
        show();
        hold(0.033);
        w = ep_station_key(&g, k == 160 ? 0x0d : 0xff);
    }
    for (int k = 0; k < 330; k++) ep_title_frame(&g); /* the ships turning */
}

static void scene_launch(void) { launch(1); }

/* the station's idle loop for a while, as often as the frontend runs it (a pass a frame) */
static void idle(double seconds)
{
    double until = now + seconds * PIT_HZ;
    while (now < until) {
        int r = ep_station_idle(&g);
        if (r == EP_CMD_SCREEN && g.f.station_step) enter_through(EP_WAIT_KEY);
        show();
        hold(1.0 / 27);
    }
}

static void scene_screens(void)
{
    enter_through(ep_start_game(&g, 12, 3, 4, 5));
    idle(1.5);
    /* F2 the market, F6 the equipment, F4 the short-range chart, F4 again the galaxy, F11
     * the system's data */
    static const uint8_t keys[] = { 0x3c, 0x40, 0x3e, 0x3e, 0x57 };
    for (size_t k = 0; k < sizeof keys; k++) {
        press(keys[k]);
        idle(2.8);
    }
}

static void scene_docking(void)
{
    launch(0);
    hold_key(0x50, 1); /* turned away from the station, out a little */
    fly(70);
    hold_key(0x50, 0);
    fly(60);
    g.f.autopilot_in = 0; /* the docking computer, as its icon turns it on */
    g.f.autopilot_step = 0;
    g.f.autopilot = 1;
    for (int k = 0; k < 6000 && fly(1) == EP_FRAME_NEXT; k++) {}
    show();
    hold(2.5);
}

/* a scene that starts late: recording from now on */
static void start_recording(void) { record_from = now; }

/* a screen opened in flight: the station's idle loop until a command goes back to flight */
static int screen_until_flight(double seconds)
{
    double until = now + seconds * PIT_HZ;
    while (now < until) {
        int r = ep_station_idle(&g);
        show();
        if (r == EP_CMD_RESTART) return 1;
        hold(1.0 / 27);
    }
    return 0;
}

static void scene_hyperspace(void)
{
    launch(0);
    hold_key(0x50, 1); /* climbing away from the planet */
    fly(90);
    hold_key(0x50, 0);
    fly(10);
    start_recording();
    press(0x3e); /* F4: the short-range chart, the station's loop while it is up */
    while (fly(1) == EP_FRAME_NEXT) {}
    screen_until_flight(1.2);
    hold_key(0x4d, 1); /* the cursor right, to Zaonce */
    screen_until_flight(0.45);
    hold_key(0x4d, 0);
    screen_until_flight(0.8);
    command(0x10); /* find: the system nearest the cursor selected */
    screen_until_flight(1.5);
    command(0x0e); /* hyperspace, from the chart's bar: the countdown starts */
    screen_until_flight(1.5);
    press(0x3b); /* F1: the front view */
    screen_until_flight(2);
    fly(300);
}

static const struct {
    const char *name;
    void (*play)(void);
    double last; /* seconds: only the scene's end is kept (0: all of it; < 0: from start_recording) */
} scenes[] = {
    { "title", scene_title, 0 },      { "launch", scene_launch, 0 },          { "screens", scene_screens, 0 },
    { "docking", scene_docking, 16 }, { "hyperspace", scene_hyperspace, -1 },
};

static void le(FILE *f, uint32_t v, int n)
{
    for (int k = 0; k < n; k++) fputc((int)(v >> 8 * k & 0xff), f);
}

static void play(int k)
{
    memset(&g, 0, sizeof g);
    g.io = &io;
    g.wait = wait_for;
    ep_boot(&g, 2, 1, 3, 4, 5); /* MCGA, an AdLib */
    OPL3_Reset(&chip, RATE);
    now = next_frame = next_sample = 0;
    samples = 0;
    phase = 0;
    memset(g.in.key, 0x80, sizeof g.in.key);
    scenes[k].play();
}

static int run(const char *dir, int k)
{
    char p[1100];
    snprintf(p, sizeof p, "%s/%s.rgb", dir, scenes[k].name);
    video = fopen(p, "wb");
    snprintf(p, sizeof p, "%s/%s.wav", dir, scenes[k].name);
    audio = fopen(p, "wb");
    if (!video || !audio) return 1;
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, audio);
    le(audio, 16, 4);
    le(audio, 1, 2);
    le(audio, 1, 2);
    le(audio, RATE, 4);
    le(audio, RATE * 2, 4);
    le(audio, 2, 2);
    le(audio, 16, 2);
    fwrite("data\0\0\0\0", 1, 8, audio);

    record_from = 1e300; /* the scene once, to know its length (it plays the same each time) */
    if (scenes[k].last > 0) {
        play(k);
        record_from = now - scenes[k].last * PIT_HZ;
    } else if (scenes[k].last < 0) {
        record_from = 1e300; /* until the scene says */
    } else {
        record_from = 0;
    }
    play(k);

    fseek(audio, 4, SEEK_SET);
    le(audio, 36 + samples * 2, 4);
    fseek(audio, 40, SEEK_SET);
    le(audio, samples * 2, 4);
    fclose(video);
    fclose(audio);
    printf("%s: %.1f s\n", scenes[k].name, (now - record_from) / PIT_HZ);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: ep_clips DATA OUTDIR [title|launch|screens|docking|hyperspace...]\n");
        return 2;
    }
    data = argv[1];
    char p[1100];
    snprintf(p, sizeof p, "%s/ELITE.GRF", data);
    grf_load(p);
    screen_init();
    int n = (int)(sizeof scenes / sizeof scenes[0]);
    for (int k = 0; k < n; k++) {
        int wanted = argc == 3;
        for (int a = 3; a < argc; a++) wanted |= !strcmp(argv[a], scenes[k].name);
        if (wanted && run(argv[2], k)) return 1;
    }
    return 0;
}
