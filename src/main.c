/* Elite Plus port: SDL2 frontend. So far it shows the title screen from the core: the ship
 * cycle and the disc at the original's timing, drawn at the window's resolution in the
 * original's MCGA colours. Escape quits. */
#include "font.h"
#include "gfx.h"

#include "ep_tables.h"
#include "ep_title.h"

#include <SDL.h>
#include <stdio.h>
#include <time.h>

extern const unsigned char font_ttf[];
extern const int font_ttf_len;

#define TICK_HZ (1193182.0 / 0x5555) /* the timer rate the original programs */
#define VIEW_X  8                    /* the 3D view on the 320 x 200 screen */
#define VIEW_Y  9

static rgba game_colour(uint8_t c)
{
    const uint8_t *d = &ep_dac[3 * ep_mcga_colour[c]];
    return (rgba){ d[0] / 63.f, d[1] / 63.f, d[2] / 63.f, 1.f };
}

typedef struct {
    float s, ox, oy; /* screen pixel -> window: x * s + ox */
} view;

static float vx(const view *v, float x) { return (VIEW_X + x) * v->s + v->ox; }
static float vy(const view *v, float y) { return (VIEW_Y + y) * v->s + v->oy; }

static void draw_title(const ep_title *t, const view *v)
{
    /* the 3D view */
    gfx_rect(vx(v, 0), vy(v, 0), 304 * v->s, 124 * v->s, rgb_hex(0x000000, 1));
    rgba disc = game_colour(0xb6);
    for (int k = 0; k < t->disc.n; k++) {
        const ep_span *sp = &t->disc.span[k];
        gfx_rect(vx(v, sp->x), vy(v, sp->row), sp->w * v->s, v->s, disc);
    }
    for (int k = 0; k < t->render.nprim; k++) {
        const ep_prim *p = &t->render.prim[k];
        rgba c = game_colour(p->colour);
        float q[8];
        for (int j = 0; j < 4; j++) {
            q[2 * j] = vx(v, p->pt[2 * j] + 0.5f);
            q[2 * j + 1] = vy(v, p->pt[2 * j + 1] + 0.5f);
        }
        if (p->kind == EP_PRIM_TRI)
            gfx_tri(q[0], q[1], q[2], q[3], q[4], q[5], c);
        else if (p->kind == EP_PRIM_QUAD)
            gfx_quad(q, c);
        else
            gfx_line(q[0], q[1], q[2], q[3], v->s, c);
    }
    gfx_flush();
    float size = 9 * v->s;
    font_draw(160 * v->s + v->ox, 12 * v->s + v->oy, size, game_colour(0x11), ALIGN_CENTER,
              ep_ship_names[t->ship_type % 30]);
    font_draw(160 * v->s + v->ox, 120 * v->s + v->oy, size, game_colour(0x0a), ALIGN_CENTER,
              "Press spacebar to start game");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Window *win = SDL_CreateWindow("Elite Plus", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 600,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!win || !ren) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    gfx_init(ren);
    if (!font_init(ren, font_ttf, font_ttf_len)) {
        fprintf(stderr, "font_init failed\n");
        return 1;
    }

    static ep_title t;
    ep_title_init(&t);
    t.mcga = 1;
    t.rng = ep_rng_init();
    ep_rng_seed(&t.rng, (uint8_t)time(NULL));

    Uint64 t0 = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
        }
        /* the original's clock: run title frames until the tick count catches up */
        uint32_t ticks = (uint32_t)((double)(SDL_GetPerformanceCounter() - t0) / (double)freq * TICK_HZ);
        int guard = 0; /* a frame ends two ticks after the last one, as the frame wait does */
        while (t.flip + 2 <= ticks && guard++ < 8) ep_title_frame(&t, 0);
        if (t.flip + 2 <= ticks) t.flip = t.clock = ticks; /* far behind (window dragged): skip */

        int w, h;
        SDL_GetRendererOutputSize(ren, &w, &h);
        view v;
        v.s = (float)w / 320.f < (float)h / 200.f ? (float)w / 320.f : (float)h / 200.f;
        v.ox = (w - 320 * v.s) / 2;
        v.oy = (h - 200 * v.s) / 2;
        SDL_SetRenderDrawColor(ren, 16, 16, 20, 255);
        SDL_RenderClear(ren);
        draw_title(&t, &v);
        SDL_RenderPresent(ren);
    }
    SDL_Quit();
    return 0;
}
