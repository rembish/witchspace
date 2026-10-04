/* Witchspace: the SDL2 frontend. The core plays the game; this keeps its clock (the timer the
 * original programs: 1193182 Hz over the divisor the core sets, 5555h, or the music's),
 * feeds it the keyboard as PC scancodes, shows what it draws on a 320 x 200 MCGA screen,
 * sounds the PC speaker or the AdLib, and goes from one of the core's loops to the next as
 * their results say (title, station screens, flight, pause, dialogues).
 *
 * usage: witchspace [--data DIR] [--saves DIR] [--speaker] [--protection]
 *   --data DIR     where your copy of the game is: ELITE.EXE, ELITE.GRF, ADBLUE.MID (default:
 *                  this program's folder, then the current one, then original/)
 *   --saves DIR    where commanders are saved (default: the game's folder)
 *   --speaker      the PC speaker for the sound (default an AdLib)
 *   --protection   ask the copy protection's question (off by default)
 *   --version      print the version and stop
 *   --shots DIR    every 25th picture shown saved as a PPM file in DIR, for checking */
#include "audio.h"
#include "files.h"
#include "grf.h"
#include "screen.h"

#include "ep_adlib.h"
#include "ep_boot.h"
#include "ep_commands.h"
#include "ep_frame.h"
#include "ep_sound.h"
#include "ep_station.h"
#include "ep_tables.h"
#include "ep_title.h"
#include "ep_travel.h"

#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PIT_HZ 1193182.0 /* the timer's input clock */

static ep_game g;
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static uint32_t rgba[SCREEN_W * SCREEN_H];
static int running = 1;
static Uint64 t0, clocks_done; /* the timer's input clock, since t0 */

/* which of the core's loops runs next (step):
 *   M_TITLE_OPENING  the protection's question or the title's intro and credits (ep_station_key)
 *   M_TITLE          the title's frames (ep_title_frame)
 *   M_DIALOG         a screen waiting for keys (ep_station_key), then after_dialog
 *   M_IDLE           a station screen idling (ep_station_idle)
 *   M_FLIGHT         flight frames (ep_flight_frame)
 *   M_PAUSE          the pause menu (ep_pause_idle), over paused_from */
enum { M_TITLE_OPENING, M_TITLE, M_DIALOG, M_IDLE, M_FLIGHT, M_PAUSE };
static int mode, after_dialog, waiting; /* waiting: the dialogue's EP_WAIT_* */
static int paused_from;                 /* the mode the pause menu came over */

/* ---- the keyboard: SDL keys as the PC's set-1 scancodes (E0h: the extended keys) ---- */

static const struct {
    SDL_Scancode sdl;
    uint8_t pc, e0;
} keys[] = {
    { SDL_SCANCODE_ESCAPE, 0x01, 0 },
    { SDL_SCANCODE_1, 0x02, 0 },
    { SDL_SCANCODE_2, 0x03, 0 },
    { SDL_SCANCODE_3, 0x04, 0 },
    { SDL_SCANCODE_4, 0x05, 0 },
    { SDL_SCANCODE_5, 0x06, 0 },
    { SDL_SCANCODE_6, 0x07, 0 },
    { SDL_SCANCODE_7, 0x08, 0 },
    { SDL_SCANCODE_8, 0x09, 0 },
    { SDL_SCANCODE_9, 0x0a, 0 },
    { SDL_SCANCODE_0, 0x0b, 0 },
    { SDL_SCANCODE_MINUS, 0x0c, 0 },
    { SDL_SCANCODE_EQUALS, 0x0d, 0 },
    { SDL_SCANCODE_BACKSPACE, 0x0e, 0 },
    { SDL_SCANCODE_TAB, 0x0f, 0 },
    { SDL_SCANCODE_Q, 0x10, 0 },
    { SDL_SCANCODE_W, 0x11, 0 },
    { SDL_SCANCODE_E, 0x12, 0 },
    { SDL_SCANCODE_R, 0x13, 0 },
    { SDL_SCANCODE_T, 0x14, 0 },
    { SDL_SCANCODE_Y, 0x15, 0 },
    { SDL_SCANCODE_U, 0x16, 0 },
    { SDL_SCANCODE_I, 0x17, 0 },
    { SDL_SCANCODE_O, 0x18, 0 },
    { SDL_SCANCODE_P, 0x19, 0 },
    { SDL_SCANCODE_LEFTBRACKET, 0x1a, 0 },
    { SDL_SCANCODE_RIGHTBRACKET, 0x1b, 0 },
    { SDL_SCANCODE_RETURN, 0x1c, 0 },
    { SDL_SCANCODE_LCTRL, 0x1d, 0 },
    { SDL_SCANCODE_A, 0x1e, 0 },
    { SDL_SCANCODE_S, 0x1f, 0 },
    { SDL_SCANCODE_D, 0x20, 0 },
    { SDL_SCANCODE_F, 0x21, 0 },
    { SDL_SCANCODE_G, 0x22, 0 },
    { SDL_SCANCODE_H, 0x23, 0 },
    { SDL_SCANCODE_J, 0x24, 0 },
    { SDL_SCANCODE_K, 0x25, 0 },
    { SDL_SCANCODE_L, 0x26, 0 },
    { SDL_SCANCODE_SEMICOLON, 0x27, 0 },
    { SDL_SCANCODE_APOSTROPHE, 0x28, 0 },
    { SDL_SCANCODE_GRAVE, 0x29, 0 },
    { SDL_SCANCODE_LSHIFT, 0x2a, 0 },
    { SDL_SCANCODE_BACKSLASH, 0x2b, 0 },
    { SDL_SCANCODE_Z, 0x2c, 0 },
    { SDL_SCANCODE_X, 0x2d, 0 },
    { SDL_SCANCODE_C, 0x2e, 0 },
    { SDL_SCANCODE_V, 0x2f, 0 },
    { SDL_SCANCODE_B, 0x30, 0 },
    { SDL_SCANCODE_N, 0x31, 0 },
    { SDL_SCANCODE_M, 0x32, 0 },
    { SDL_SCANCODE_COMMA, 0x33, 0 },
    { SDL_SCANCODE_PERIOD, 0x34, 0 },
    { SDL_SCANCODE_SLASH, 0x35, 0 },
    { SDL_SCANCODE_RSHIFT, 0x36, 0 },
    { SDL_SCANCODE_KP_MULTIPLY, 0x37, 0 },
    { SDL_SCANCODE_LALT, 0x38, 0 },
    { SDL_SCANCODE_SPACE, 0x39, 0 },
    { SDL_SCANCODE_CAPSLOCK, 0x3a, 0 },
    { SDL_SCANCODE_F1, 0x3b, 0 },
    { SDL_SCANCODE_F2, 0x3c, 0 },
    { SDL_SCANCODE_F3, 0x3d, 0 },
    { SDL_SCANCODE_F4, 0x3e, 0 },
    { SDL_SCANCODE_F5, 0x3f, 0 },
    { SDL_SCANCODE_F6, 0x40, 0 },
    { SDL_SCANCODE_F7, 0x41, 0 },
    { SDL_SCANCODE_F8, 0x42, 0 },
    { SDL_SCANCODE_F9, 0x43, 0 },
    { SDL_SCANCODE_F10, 0x44, 0 },
    { SDL_SCANCODE_NUMLOCKCLEAR, 0x45, 0 },
    { SDL_SCANCODE_SCROLLLOCK, 0x46, 0 },
    { SDL_SCANCODE_KP_7, 0x47, 0 },
    { SDL_SCANCODE_KP_8, 0x48, 0 },
    { SDL_SCANCODE_KP_9, 0x49, 0 },
    { SDL_SCANCODE_KP_MINUS, 0x4a, 0 },
    { SDL_SCANCODE_KP_4, 0x4b, 0 },
    { SDL_SCANCODE_KP_5, 0x4c, 0 },
    { SDL_SCANCODE_KP_6, 0x4d, 0 },
    { SDL_SCANCODE_KP_PLUS, 0x4e, 0 },
    { SDL_SCANCODE_KP_1, 0x4f, 0 },
    { SDL_SCANCODE_KP_2, 0x50, 0 },
    { SDL_SCANCODE_KP_3, 0x51, 0 },
    { SDL_SCANCODE_KP_0, 0x52, 0 },
    { SDL_SCANCODE_KP_PERIOD, 0x53, 0 },
    { SDL_SCANCODE_F11, 0x57, 0 },
    { SDL_SCANCODE_F12, 0x58, 0 },
    { SDL_SCANCODE_UP, 0x48, 1 },
    { SDL_SCANCODE_DOWN, 0x50, 1 },
    { SDL_SCANCODE_LEFT, 0x4b, 1 },
    { SDL_SCANCODE_RIGHT, 0x4d, 1 },
    { SDL_SCANCODE_INSERT, 0x52, 1 },
    { SDL_SCANCODE_DELETE, 0x53, 1 },
    { SDL_SCANCODE_HOME, 0x47, 1 },
    { SDL_SCANCODE_END, 0x4f, 1 },
    { SDL_SCANCODE_PAGEUP, 0x49, 1 },
    { SDL_SCANCODE_PAGEDOWN, 0x51, 1 },
    { SDL_SCANCODE_KP_ENTER, 0x1c, 1 },
    { SDL_SCANCODE_RCTRL, 0x1d, 1 },
    { SDL_SCANCODE_RALT, 0x38, 1 },
};

static void key(SDL_Scancode s, int up)
{
    for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++)
        if (keys[k].sdl == s) {
            if (keys[k].e0) ep_key_event(&g, 0xe0);
            ep_key_event(&g, (uint8_t)(keys[k].pc | (up ? 0x80 : 0)));
            return;
        }
}

/* ---- the mouse (int 33h: mickeys and buttons) and a game controller as the joystick (port
 * 201h: counts about 1000 at the centre, buttons A and B) ---- */

static SDL_GameController *pad;
static int fullscreen;

static void devices(void)
{
    if (!pad)
        for (int k = 0; k < SDL_NumJoysticks() && !pad; k++)
            if (SDL_IsGameController(k)) pad = SDL_GameControllerOpen(k);
    g.in.joy_present = pad != NULL;
    if (pad) {
        int x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX),
            y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        g.in.joy_x = (uint16_t)(1000 + x * 900 / 32768);
        g.in.joy_y = (uint16_t)(1000 + y * 900 / 32768);
        g.in.joy_buttons =
            (uint8_t)(0xff & ~(SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) ? 0x10 : 0) &
                      ~(SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B) ? 0x20 : 0));
    }
    g.in.mouse_present = 1;
    Uint32 b = SDL_GetMouseState(NULL, NULL);
    g.in.mouse_buttons = (uint8_t)((b & SDL_BUTTON_LMASK ? 1 : 0) | (b & SDL_BUTTON_RMASK ? 2 : 0));
    SDL_SetRelativeMouseMode(g.in.control == 2 && mode == M_FLIGHT ? SDL_TRUE : SDL_FALSE);
}

static void pump(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) running = 0;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_RETURN &&
            (ev.key.keysym.mod & KMOD_ALT)) {
            fullscreen = !fullscreen; /* Alt+Enter: the frontend's own */
            SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            continue;
        }
        if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) key(ev.key.keysym.scancode, ev.type == SDL_KEYUP);
        if (ev.type == SDL_MOUSEMOTION) {
            g.in.mouse_dx = (int16_t)(g.in.mouse_dx + ev.motion.xrel);
            g.in.mouse_dy = (int16_t)(g.in.mouse_dy + ev.motion.yrel);
        }
    }
    devices();
}

/* ---- time: the timer interrupt as often as the original's ---- */

static void advance(void)
{
    Uint64 due =
        (Uint64)((double)(SDL_GetPerformanceCounter() - t0) / (double)SDL_GetPerformanceFrequency() * PIT_HZ);
    if (due > clocks_done + 30 * 0x5555) clocks_done = due - 30 * 0x5555; /* far behind (the window held) */
    for (;;) {
        Uint64 divisor = g.pit ? g.pit : 0x10000;
        if (clocks_done + divisor > due) break;
        clocks_done += divisor;
        ep_pit_tick(&g);
        audio_speaker(g.speaker, g.speaker_on);
        if (g.nopl) {
            audio_opl((const uint8_t (*)[2])g.opl, g.nopl, (double)(due - clocks_done) / PIT_HZ);
            g.nopl = 0;
        }
    }
}

/* ---- the screen ---- */

static const char *shots; /* --shots DIR: the picture now and then, for checking */
static int presents;

static void shot(void)
{
    char p[1100];
    snprintf(p, sizeof p, "%s/%06d.ppm", shots, presents);
    FILE *f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int k = 0; k < SCREEN_W * SCREEN_H; k++) {
        uint8_t px[3] = { (uint8_t)(rgba[k] >> 16), (uint8_t)(rgba[k] >> 8), (uint8_t)rgba[k] };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static void present(void)
{
    screen_draw(&g);
    ep_output_begin(&g);
    screen_rgba(rgba);
    if (shots && presents++ % 25 == 0) shot();
    SDL_UpdateTexture(tex, NULL, rgba, SCREEN_W * 4);
    int w, h;
    SDL_GetRendererOutputSize(ren, &w, &h);
    SDL_Rect dst; /* 320 x 200 shown 4:3, as on the monitors of the time */
    if (w * 3 > h * 4) {
        dst.h = h;
        dst.w = h * 4 / 3;
    } else {
        dst.w = w;
        dst.h = w * 3 / 4;
    }
    dst.x = (w - dst.w) / 2;
    dst.y = (h - dst.h) / 2;
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, &dst);
    SDL_RenderPresent(ren);
}

/* the core waits on the timer here (the frame flip, a sound playing out) */
static void wait_for(ep_game *gg, uint32_t until, int show)
{
    if (show) present();
    while (running && gg->clock < until && until - gg->clock < 100000 && !gg->f.paused) {
        pump();
        advance();
        if (gg->clock < until) SDL_Delay(1);
    }
}

/* a screen's pass: as often as a frame (two ticks of the game's 54.6 Hz clock, about 36 ms),
 * where the original runs them as fast as it can */
static void pace(void)
{
    Uint32 until = SDL_GetTicks() + 36;
    while (running && !SDL_TICKS_PASSED(SDL_GetTicks(), until)) {
        pump();
        advance();
        SDL_Delay(1);
    }
}

/* ---- the game's loops ---- */

static void time_of_day(uint8_t t[4])
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    t[0] = (uint8_t)tm->tm_hour;
    t[1] = (uint8_t)tm->tm_min;
    t[2] = (uint8_t)tm->tm_sec;
    t[3] = (uint8_t)(SDL_GetTicks() / 10 % 100);
}

static void dialog(int w, int after)
{
    if (w == EP_WAIT_NONE) {
        mode = after;
        return;
    }
    waiting = w;
    after_dialog = after;
    mode = M_DIALOG;
}

static void title(void)
{
    g.f.leave = 0;
    waiting = ep_title_open(&g);
    mode = M_TITLE_OPENING;
}

static void station(void)
{
    ep_enter_station(&g);
    dialog(ep_status_screen(&g), M_IDLE);
}

/* the station's screens, in flight too (EP_CMD_*) */
static void after_idle(int r)
{
    switch (r) {
    case EP_CMD_SCREEN: dialog(g.f.station_step ? EP_WAIT_KEY : EP_WAIT_NONE, M_IDLE); break;
    case EP_CMD_RESTART: mode = M_FLIGHT; break;
    case EP_CMD_PAUSE:
        paused_from = M_IDLE;
        mode = M_PAUSE;
        break;
    case EP_CMD_TITLE: title(); break;
    case EP_CMD_QUIT: running = 0; break;
    default: break;
    }
}

static void after_frame(int r)
{
    switch (r) {
    case EP_FRAME_DOCKED: station(); break;
    case EP_FRAME_SCREEN: dialog(g.f.station_step ? EP_WAIT_KEY : EP_WAIT_NONE, M_IDLE); break;
    case EP_FRAME_OVER: title(); break;
    case EP_FRAME_PAUSED:
        paused_from = M_FLIGHT;
        mode = M_PAUSE;
        break;
    default: break;
    }
}

/* a dialogue is over: where its answers lead (f.leave) */
static void dialog_over(void)
{
    switch (g.f.leave) {
    case 1: title(); return;
    case 2: running = 0; return;
    case 3:
        g.f.leave = 0;
        station();
        return;
    default: mode = after_dialog;
    }
}

static void step(void)
{
    switch (mode) {
    case M_TITLE_OPENING:
    case M_DIALOG: {
        uint8_t k = g.in.last_key;
        /* these waits are passed ffh (no key) every pass as well: the core looks at the clock
         * or the key table itself (EP_WAIT_* in ep_station.h) */
        if (k == 0xff && waiting != EP_WAIT_TEXT && waiting != EP_WAIT_TIME && waiting != EP_WAIT_LIST &&
            waiting != EP_WAIT_SCAN) {
            pace();
            break;
        }
        g.in.last_key = 0xff; /* 0276 takes it */
        waiting = ep_station_key(&g, k);
        if (waiting == EP_WAIT_NONE) {
            if (mode == M_TITLE_OPENING)
                mode = M_TITLE;
            else
                dialog_over();
        }
        pace();
        break;
    }
    case M_TITLE: {
        int r = ep_title_frame(&g);
        if (g.f.leave == 2) {
            running = 0;
            break;
        }
        if (r == EP_CMD_START) {
            uint8_t t[4];
            time_of_day(t);
            dialog(ep_start_game(&g, t[0], t[1], t[2], t[3]), M_IDLE);
        } else if (r == EP_CMD_SCREEN) {
            dialog(g.f.station_step ? EP_WAIT_KEY : EP_WAIT_NONE, g.f.station_step ? M_TITLE : M_IDLE);
        } else if (r == EP_CMD_PAUSE) {
            paused_from = M_TITLE;
            mode = M_PAUSE;
        } else {
            after_idle(r);
        }
        break;
    }
    case M_IDLE:
        after_idle(ep_station_idle(&g));
        pace();
        break;
    case M_FLIGHT: after_frame(ep_flight_frame(&g)); break;
    case M_PAUSE: {
        int r = ep_pause_idle(&g);
        pace();
        if (g.f.station_step) {
            dialog(EP_WAIT_YN, M_PAUSE);
        } else if (r == EP_CMD_RESUME) {
            if (g.f.resume == EP_RESUME_FLIGHT) {
                mode = M_FLIGHT;
                after_frame(ep_flight_resume(&g));
            } else if (g.f.resume == EP_RESUME_IDLE) {
                mode = M_IDLE;
                after_idle(ep_station_resume(&g));
            } else {
                mode = paused_from;
            }
        }
        break;
    }
    default: break;
    }
}

int main(int argc, char **argv)
{
    const char *data = NULL, *saves = NULL;
    int protection = 0, adlib = 1;
    for (int k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "--data") && k + 1 < argc)
            data = argv[++k];
        else if (!strcmp(argv[k], "--saves") && k + 1 < argc)
            saves = argv[++k];
        else if (!strcmp(argv[k], "--version")) {
            printf("Witchspace %s\n", WS_VERSION);
            return 0;
        } else if (!strcmp(argv[k], "--protection"))
            protection = 1;
        else if (!strcmp(argv[k], "--speaker"))
            adlib = 0;
        else if (!strcmp(argv[k], "--shots") && k + 1 < argc)
            shots = argv[++k];
    }
    /* your copy of the game: --data, or the folder this program is in, the current one, or
     * original/ (where the sources keep it) */
    char path[1100], here[1024] = ".";
    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(here, sizeof here, "%s", base);
        SDL_free(base);
    }
    const char *look[3] = { here, ".", "original" };
    for (int k = 0; !data && k < 3; k++)
        if (files_find(look[k], "ELITE.EXE", path, sizeof path)) data = look[k];
    if (!data || !files_find(data, "ELITE.EXE", path, sizeof path)) {
        fprintf(stderr,
                "Witchspace needs your copy of Elite Plus: ELITE.EXE was not found in %s.\n"
                "Put this program in the game's folder, or give it with --data DIR.\n",
                data ? data : "this program's folder, the current one or original/");
        return 1;
    }
    if (!saves) saves = data; /* the commanders beside the game, as the original kept them */
    size_t n;
    uint8_t *exe = files_slurp(path, &n); /* the game's tables */
    if (!exe) {
        fprintf(stderr, "%s: cannot be read\n", path);
        return 1;
    }
    int r = ep_data_load(exe, n);
    free(exe);
    if (r) {
        fprintf(stderr, "%s: %s\n", path, ep_data_error(r));
        return 1;
    }
    files_find(data, "ELITE.GRF", path, sizeof path);
    uint8_t *grf = files_slurp(path, &n);
    if (grf) ep_data_grf(grf, n);
    free(grf);
    if (!grf_load(path)) fprintf(stderr, "no %s: the pictures are left out (see --data)\n", path);
    files_init(saves, data);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    win = SDL_CreateWindow("Witchspace", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W,
                                  SCREEN_H)
              : NULL;
    if (!tex) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    audio_init();
    screen_init();

    uint8_t t[4];
    time_of_day(t);
    g.io = &files_io;
    g.wait = wait_for;
    g.protection = (uint8_t)protection;
    ep_boot(&g, 2, adlib ? 1 : 2, t[1], t[2], t[3]); /* MCGA; the AdLib or the PC speaker */
    t0 = SDL_GetPerformanceCounter();
    if (protection)
        dialog(ep_protection_ask(&g), M_TITLE_OPENING);
    else
        title();
    int asked = protection;
    while (running) {
        pump();
        advance();
        if (asked && mode == M_TITLE_OPENING) { /* the question answered: the title */
            asked = 0;
            title();
        }
        step();
        present();
#ifdef __EMSCRIPTEN__
        emscripten_sleep(0); /* the browser's turn: a frame drawn, events delivered */
#endif
    }
    audio_quit();
    SDL_Quit();
    return 0;
}
