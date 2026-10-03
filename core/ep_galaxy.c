/* Elite Plus galaxy generation, reconstructed from ELITE.EXE (see ep_galaxy.h). */
#include "ep_galaxy.h"

#include "ep_tables.h"

#include <string.h>

void ep_twist(ep_seed *s)
{
    uint16_t sum = (uint16_t)(s->w[0] + s->w[1] + s->w[2]);
    s->w[0] = s->w[1];
    s->w[1] = s->w[2];
    s->w[2] = sum;
}

ep_seed ep_galaxy_seed(int galaxy)
{
    ep_seed s;
    memcpy(s.w, ep_galaxy_seeds[galaxy & 7], sizeof s.w);
    return s;
}

ep_seed ep_system_seed(int galaxy, int n)
{
    ep_seed s = ep_galaxy_seed(galaxy);
    for (int i = 0; i < 4 * n; i++) ep_twist(&s);
    return s;
}

void ep_planet_name(ep_seed *s, uint8_t nb[EP_NAMEBUF])
{
    /* Digrams are stored as words and the position only advances past letters, so a pair can
     * leave a stray space behind the name; bytes 4..7 are pre-filled with spaces. */
    memset(nb + 4, ' ', 4);
    uint8_t long_name = (uint8_t)(s->w[0] & 0x40);
    int len = 0;
    for (int k = 4; k > 0; k--) {
        int idx = (s->w[2] >> 8) & 0x1f;
        ep_twist(s);
        if (k == 1 && !long_name) continue;
        char a = ep_digrams[2 * idx], b = ep_digrams[2 * idx + 1];
        nb[len] = (uint8_t)a;
        nb[len + 1] = (uint8_t)b;
        if (b != ' ') len++;
        if (a != ' ') len++;
    }
    nb[9] = (uint8_t)len;
    nb[len] = 0;
}

void ep_system_name(ep_seed *s, char out[EP_NAME_MAX + 1])
{
    uint8_t nb[EP_NAMEBUF] = { 0 };
    ep_planet_name(s, nb);
    memcpy(out, nb, EP_NAME_MAX + 1);
}

void ep_system_data(const ep_seed *s, ep_system *out)
{
    uint8_t w0lo = (uint8_t)s->w[0], w0hi = (uint8_t)(s->w[0] >> 8);
    uint8_t w1lo = (uint8_t)s->w[1], w1hi = (uint8_t)(s->w[1] >> 8);
    uint8_t w2lo = (uint8_t)s->w[2], w2hi = (uint8_t)(s->w[2] >> 8);
    (void)w0lo;

    out->x = w1hi;
    out->y = (uint8_t)(w0hi >> 1);

    uint8_t gov = (uint8_t)((w1lo >> 3) & 7);
    out->government = gov;
    uint8_t eco = (uint8_t)((((gov >> 1) ? 0 : 2) | (w0hi & 7)) ^ 7);
    out->economy = eco;
    uint8_t tech = (uint8_t)(((eco + 3) & w1hi) + (w2lo & 1));
    out->tech = tech;
    out->population = (uint8_t)((uint8_t)(tech * eco) / 2 + 0x14 + gov);

    if (!(w2lo & 0x80)) {
        out->species[0] = 0xff;
        memset(out->species + 1, 0, 3); /* not written by the original: left as they were */
    } else {
        uint8_t mix = (uint8_t)((w0hi ^ w1hi) & 7);
        out->species[0] = (uint8_t)((w2hi >> 2) & 7);
        out->species[1] = (uint8_t)(w2hi >> 5);
        out->species[2] = mix;
        out->species[3] = (uint8_t)(((w2hi & 3) + mix) & 7);
    }

    /* (gov + 8)^2 fits a byte, the 8-bit multiply by the population a word */
    out->productivity = (uint16_t)((uint16_t)((gov + 8) * (gov + 8) * out->population) << 2);

    uint16_t r = (uint16_t)(w0hi * 0x101u);
    r = (uint16_t)((r << 2) | (r >> 14));
    uint16_t sw = (uint16_t)(((unsigned)w2lo << 8 | w2hi) & 0x3ff);
    out->radius = (uint16_t)(((r ^ sw) & 0xfff) + 0x10e1);

    out->desc_seed[0] = (uint16_t)(s->w[0] ^ s->w[1]);
    out->desc_seed[1] = (uint16_t)(out->desc_seed[0] ^ s->w[2]);

    ep_seed t = *s;
    ep_system_name(&t, out->name);
}
