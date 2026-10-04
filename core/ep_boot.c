/* Elite Plus start-up, reconstructed from ELITE.EXE (see ep_boot.h). */
#include "ep_boot.h"

#include "ep_adlib.h"
#include "ep_dsmap.h"
#include "ep_tables.h"

#include <string.h>

void ep_protection_pick(ep_game *g)
{
    ep_flight *f = &g->f;
    uint16_t ax = g->rng.w[0], bx = g->rng.w[1];
    ep_rng_step(&g->rng);
    uint8_t al = (uint8_t)ax, ah = (uint8_t)(ax >> 8), bl = (uint8_t)bx;
    al = (uint8_t)(al >> 1 | al << 7);
    bl = (uint8_t)(bl << 1 | bl >> 7);
    ah = (uint8_t)~ah;
    al ^= bl;
    ah ^= (uint8_t)(bx >> 8);
    al ^= ah;
    uint16_t at = 0x5070; /* the records, a 0 byte after the last */
    for (; al; al--) {
        at = (uint16_t)(at + 3);
        if (!ep_ds_initial[at]) at = 0x5070;
    }
    uint8_t r0 = ep_ds_initial[at], r1 = ep_ds_initial[at + 1], r2 = ep_ds_initial[at + 2];
    f->prot_page[0] = r0 & 0x3f;
    f->prot_paragraph = (uint8_t)((r1 & 0x38) >> 3);
    f->prot_line = r1 & 7;
    f->prot_word = r2 & 7;
    f->prot_hash = (uint16_t)((r2 >> 3) | (r1 & 0xc0) >> 1 | (r0 & 0x40) << 1 | (r0 & 0x80) << 1);
}

uint16_t ep_protection_hash(const uint8_t *word)
{
    uint16_t h = 0;
    for (; *word; word++) h = (uint16_t)((2 * h + (uint8_t)(*word - 'A')) & 0x1ff);
    return h;
}

void ep_boot(ep_game *g, uint8_t video, uint8_t sound, uint8_t minute, uint8_t second, uint8_t hundredths)
{
    static uint8_t ds[EP_DS_SIZE];
    memset(ds, 0, sizeof ds);
    memcpy(ds, ep_ds_initial, sizeof ep_ds_initial);
    const ep_io *io = g->io; /* the frontend's settings stay */
    ep_wait_fn wait = g->wait;
    void *frontend = g->frontend;
    uint8_t protection = g->protection, fixes = g->fixes;
    memset(g, 0, sizeof *g);
    ep_ds_load(g, ds);
    memcpy(g->adlib.fx, ep_drv_initial + EP_FX_CS, EP_FX_SIZE); /* the driver's segment as loaded */
    g->io = io;
    g->wait = wait;
    g->frontend = frontend;
    g->protection = protection;
    g->fixes = fixes;
    g->rng = ep_rng_init(); /* 0047 */
    ep_rng_seed(&g->rng, (uint8_t)(hundredths ^ second ^ minute));
    g->f.sound_mode = 5;                       /* 49b4 */
    memset(g->in.key, 0x80, sizeof g->in.key); /* 01bc: every key up */
    g->f.video = video;
    g->f.sound_device = sound;
    g->pit = 0x5555; /* 49b4: the game's timer interrupt */
    g->int8 = EP_INT8_GAME;
    if (sound != 2) g->adlib.drv[0xb5b7 - EP_ADLIB_DS] = sound == 1; /* 4ecf: an AdLib */
    ep_protection_pick(g);                                           /* 31f6 */
    if (sound != 2) ep_adlib_init(g);                                /* 4f03 */
    g->cmdr_saved = g->cmdr;                                         /* 71b0 */
    /* 141b: the location's numbers as text in the question */
    ep_flight *f = &g->f;
    uint8_t n = f->prot_page[0], tens = 0x20;
    while (n >= 10) {
        n = (uint8_t)(n - 10);
        tens++;
    }
    /* a space for no tens digit, else 20h + n + 10h, the digit n */
    f->prot_page[0] = tens == 0x20 ? tens : (uint8_t)(tens + 0x10);
    f->prot_page[1] = (uint8_t)(n + '0');
    f->prot_paragraph = (uint8_t)(f->prot_paragraph + '0');
    f->prot_line = (uint8_t)(f->prot_line + '0');
    f->prot_word = (uint8_t)(f->prot_word + '0');
    g->in.last_key = 0xff;
}
