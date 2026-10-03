/* Elite Plus system descriptions, reconstructed from ELITE.EXE (see ep_desc.h). */
#include "ep_desc.h"

#include "ep_tables.h"

#include <string.h>

typedef struct {
    uint8_t out[EP_DESC_MAX + 2]; /* out[0] is the byte before the buffer (template end, 0) */
    int di;                       /* write position in out */
    uint16_t r0, r1;              /* ds:8351, ds:8353 */
    uint8_t caps;                 /* ds:5a34 */
    uint8_t nb[EP_NAMEBUF];       /* ds:8338..8341 */
} desc_state;

static void put(desc_state *d, uint8_t c)
{
    if (d->di <= EP_DESC_MAX) d->out[d->di] = c;
    d->di++;
}

static uint8_t prev(const desc_state *d)
{
    return d->di > 0 && d->di <= EP_DESC_MAX + 1 ? d->out[d->di - 1] : 0;
}

/* 6496 */
static uint16_t desc_random(desc_state *d)
{
    uint16_t old = d->r0;
    d->r0 = d->r1;
    d->r1 = (uint16_t)(old + d->r1);
    return d->r1;
}

/* 64a5: the name as "Name " in the scratch buffer ds:63f2; returns the position of the space.
 * The name is cut at the length byte, which may belong to a later random name. */
static int name_scratch(desc_state *d, uint8_t tmp[16])
{
    d->nb[d->nb[9] < EP_NAMEBUF ? d->nb[9] : EP_NAMEBUF - 1] = 0;
    int i = 0, j = 0;
    tmp[j++] = d->nb[i++];
    while (i < EP_NAMEBUF && d->nb[i]) tmp[j++] = (uint8_t)(d->nb[i++] | 0x20);
    tmp[j] = ' ';
    tmp[j + 1] = 0;
    return j;
}

static void print(desc_state *d, const uint8_t *s);

static void print_code(desc_state *d, uint8_t code)
{
    uint8_t tmp[16];
    int j;
    switch (code) {
    case 1: /* 6401: system name */
        name_scratch(d, tmp);
        print(d, tmp);
        break;
    case 2: /* 6414: name + "ian", dropping a final vowel */
        j = name_scratch(d, tmp);
        if (strchr("aeiou", tmp[j - 1]) && tmp[j - 1]) j--;
        memcpy(tmp + j, ep_desc_ian, 5);
        print(d, tmp);
        break;
    case 3: { /* 6446: random name from the description seeds; the length byte stays changed */
        uint8_t saved[8];
        memcpy(saved, d->nb, 8);
        ep_seed s = { { d->r0, d->r1, (uint16_t)(d->r0 ^ d->r1) } };
        ep_planet_name(&s, d->nb);
        name_scratch(d, tmp);
        memcpy(d->nb, saved, 8);
        print(d, tmp);
        break;
    }
    case 4: /* 6488: back one character */
        d->di--;
        break;
    case 5: d->caps = 1; break;
    case 6: d->caps = 0; break;
    default: break;
    }
}

/* 6396 */
static void print(desc_state *d, const uint8_t *s)
{
    for (uint8_t c; (c = *s++) != 0;) {
        if (c < 0x20) {
            print_code(d, c);
        } else if (c >= 0x80) {
            const char *const *opts = ep_desc_tokens[(c - 0x80) % EP_DESC_TOKENS];
            uint8_t pick = (uint8_t)((desc_random(d) & 0xff) / 0x34);
            print(d, (const uint8_t *)opts[pick]);
        } else {
            if (c == ' ' && prev(d) == ' ') continue;
            if (d->caps == 1 && prev(d) == ' ' && c >= 0x60 && c < 0x79) c &= 0xdf;
            put(d, c);
        }
    }
}

void ep_system_description(const ep_seed *s, char out[EP_DESC_MAX + 1])
{
    desc_state d;
    memset(&d, 0, sizeof d);
    ep_system sys;
    ep_system_data(s, &sys);
    ep_seed t = *s;
    ep_planet_name(&t, d.nb);
    d.r0 = sys.desc_seed[0];
    d.r1 = sys.desc_seed[1];
    d.di = 1;
    print(&d, (const uint8_t *)ep_desc_template);
    put(&d, 0);
    d.out[EP_DESC_MAX + 1] = 0;
    memcpy(out, d.out + 1, EP_DESC_MAX + 1);
}
