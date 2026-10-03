/* Elite Plus objects in space, reconstructed from ELITE.EXE (see ep_objects.h). */
#include "ep_objects.h"

#include "ep_tables.h"

static uint16_t get16(const ep_object *o, int off) { return (uint16_t)(o->b[off] | o->b[off + 1] << 8); }

static void set16(ep_object *o, int off, uint16_t v)
{
    o->b[off] = (uint8_t)v;
    o->b[off + 1] = (uint8_t)(v >> 8);
}

ep_rot ep_rot_from_angle(uint16_t angle)
{
    ep_rot r;
    r.sin = ep_sin2048[angle & 0x7ff];
    r.cos = ep_sin2048[(angle + 0x200) & 0x7ff];
    return r;
}

/* imul, then the high word of the product doubled plus the next bit (rounding) */
static uint16_t rmul(int16_t a, int16_t b)
{
    uint32_t p = (uint32_t)((int32_t)a * b);
    return (uint16_t)((uint16_t)(p >> 15) + ((p >> 14) & 1));
}

void ep_rotate_pair(const ep_rot *r, int16_t *a, int16_t *b)
{
    int16_t a2 = (int16_t)(uint16_t)((uint16_t)*a << 1);
    int16_t b2 = (int16_t)(uint16_t)((uint16_t)*b << 1);
    uint16_t na = (uint16_t)(rmul(a2, r->cos) - rmul(b2, r->sin));
    uint16_t nb = (uint16_t)(rmul(b2, r->cos) + rmul(a2, r->sin));
    *a = (int16_t)na;
    *b = (int16_t)nb;
}

void ep_rotate_by_player(const ep_space *s, int16_t p[3])
{
    ep_rotate_pair(&s->rot[0], &p[1], &p[2]);
    ep_rotate_pair(&s->rot[1], &p[0], &p[2]);
    ep_rotate_pair(&s->rot[2], &p[0], &p[1]);
}

uint16_t ep_abs16(uint16_t v) { return (v & 0x8000) ? (uint16_t)(0u - v) : v; }

/* 4217: every coordinate fits 16 bits (the high byte is the word's sign extension) */
static int position_fits(const ep_object *o)
{
    for (int k = 0; k < 3; k++) {
        uint8_t hi = o->b[EP_OBJ_POS_HI + k];
        int neg = o->b[EP_OBJ_POS + 2 * k + 1] & 0x80;
        if (!((hi == 0 && !neg) || (hi == 0xff && neg))) return 0;
    }
    return 1;
}

int ep_object_in_range(ep_object *o)
{
    if (!position_fits(o)) return 0;
    uint16_t c[3];
    for (int k = 0; k < 3; k++) {
        c[k] = ep_abs16(get16(o, EP_OBJ_POS + 2 * k));
        if (c[k] >= 0x2ee0) return 0;
    }
    uint16_t sum = (uint16_t)(((uint32_t)c[0] * c[0]) >> 16);
    sum = (uint16_t)(sum + (((uint32_t)c[1] * c[1]) >> 16));
    if (sum >= 0x895) return 0;
    sum = (uint16_t)(sum + (((uint32_t)c[2] * c[2]) >> 16));
    if (sum >= 0x895) return 0;
    o->b[EP_OBJ_DIST] = (uint8_t)(sum >> 6);
    o->b[EP_OBJ_FLAGS] |= 0x40;
    return 1;
}

/* 2995: the blip's place on the scanner from the camera position; an object that lands on
 * the scanner is marked (+1e bit 1, which also holds back its explosion timer) */
static void scanner_blip(ep_object *o, const int16_t p[3])
{
    int16_t y = (int16_t)((p[1] >> 2) - ((p[1] >> 2) >> 2));
    int16_t z = (int16_t)((p[2] >> 2) + ((p[2] >> 2) >> 2));
    uint8_t xh = (uint8_t)((uint8_t)((uint16_t)p[0] >> 8) + 0xa0);
    if (xh < 0x60 || xh >= 0xdf) return;
    uint8_t zh = (uint8_t)(0xb0 - (uint8_t)((uint16_t)z >> 8));
    if (zh < 0xa0 || zh >= 0xc1) return;
    uint8_t top = (uint8_t)(zh + (uint8_t)((uint16_t)y >> 8));
    if (top < 0xa0 || top >= 0xc1) return;
    o->b[EP_OBJ_FLAGS1E] |= 2;
}

/* 4359: scanner, in flight and for the first 20 slots: ships blink when +1e bit 5 is set
 * (bit 6: hidden, +34 counts the phases) */
static void scanner(const ep_space *s, ep_object *o, const int16_t p[3])
{
    if (!s->in_flight || o >= &s->obj[20]) return;
    uint8_t type = (uint8_t)((o->b[EP_OBJ_FLAGS] >> 1) & 0x1f);
    if (type == 30 || type == 31) return;
    uint8_t *f = &o->b[EP_OBJ_FLAGS1E];
    if (*f & 0x20) {
        if (*f & 0x40) {
            *f |= 2;
            if (--o->b[EP_OBJ_TIMER]) return;
            *f ^= 0x40;
            o->b[EP_OBJ_TIMER] = 0x14;
        } else if (!--o->b[EP_OBJ_TIMER]) {
            *f ^= 0x40;
            o->b[EP_OBJ_TIMER] = 0x19;
        }
    }
    scanner_blip(o, p);
}

void ep_object_rotate(ep_space *s, ep_object *o, int16_t p[3])
{
    ep_rotate_by_player(s, p);
    o->b[EP_OBJ_ZHI] = (uint8_t)((uint16_t)p[2] >> 8);
    scanner(s, o, p);
    if (s->extra_angle) {
        s->rot[5] = ep_rot_from_angle((uint16_t)(0u - s->extra_angle));
        ep_rotate_pair(&s->rot[5], &p[0], &p[2]);
    }
}

/* 6e6c: angle of a ratio a/b <= 1 (a, b unsigned) by binary search in the tangent table;
 * 100h (45 degrees) when the ratio rounds to 1 or the division fails */
static uint16_t atan_ratio(uint16_t a, uint16_t b)
{
    uint32_t num = (uint32_t)a << 15;
    if (!b || num / b > 0xffff || num / b >= 0x7fff) return 0x100;
    uint16_t ratio = (uint16_t)(num / b);
    uint16_t lo = 0, hi = 0x1fe, mid = 0;
    for (int n = 9; n > 0; n--) {
        mid = (uint16_t)(((uint16_t)(lo + hi) >> 1) & 0xfffe);
        uint16_t t = ep_tan256[mid >> 1];
        if (t == ratio) break;
        if (t < ratio)
            lo = mid;
        else
            hi = mid;
    }
    return (uint16_t)(mid >> 1);
}

/* 6e5c */
static uint16_t atan_octant(uint16_t a, uint16_t b)
{
    if (b >= a) return (uint16_t)(0x200 - atan_ratio(a, b));
    return atan_ratio(b, a);
}

uint16_t ep_atan2(int16_t x, int16_t y)
{
    uint16_t ax = (uint16_t)x, bx = (uint16_t)y, r;
    if (y >= 0) {
        if (x >= 0) {
            r = (uint16_t)(0x200 - atan_octant(ax, bx));
        } else {
            r = (uint16_t)(atan_octant((uint16_t)(0u - ax), bx) - 0x200);
        }
    } else if (x >= 0) {
        r = (uint16_t)(atan_octant(ax, (uint16_t)(0u - bx)) + 0x200);
    } else {
        r = (uint16_t)(-0x200 - atan_octant((uint16_t)(0u - ax), (uint16_t)(0u - bx)));
    }
    return r & 0x7ff;
}

/* |coordinate k| as 24 bits */
static uint32_t abs24(const ep_object *o, int k)
{
    uint32_t v = (uint32_t)o->b[EP_OBJ_POS_HI + k] << 16 | get16(o, EP_OBJ_POS + 2 * k);
    if (v & 0x800000) v = (0u - v) & 0xffffff;
    return v;
}

uint8_t ep_planet_scale(const ep_object *o)
{
    uint32_t m = abs24(o, 0);
    for (int k = 1; k < 3; k++)
        if (abs24(o, k) > m) m = abs24(o, k);
    uint8_t cl = 0;
    while (m >> 16) {
        cl++;
        m >>= 1;
    }
    while (m >= 0x24b8) {
        cl++;
        m >>= 1;
    }
    return cl;
}

void ep_planet_to_camera(ep_space *s, ep_object *o)
{
    uint8_t cl = ep_planet_scale(o);
    o->b[EP_OBJ_ANGLE] = cl;
    int16_t p[3];
    for (int k = 0; k < 3; k++) { /* 6eb9: 24-bit arithmetic shift, low word kept */
        int32_t v = (int32_t)((uint32_t)o->b[EP_OBJ_POS_HI + k] << 24 | (uint32_t)get16(o, EP_OBJ_POS + 2 * k)
                                                                            << 8) >>
                    8;
        p[k] = (int16_t)(uint16_t)(v >> cl);
    }
    ep_object_rotate(s, o, p);
    for (int k = 0; k < 3; k++) set16(o, EP_OBJ_CAM + 2 * k, (uint16_t)p[k]);
    o->b[EP_OBJ_FLAGS] |= 0xc0;
}

uint16_t ep_apparent_size(const ep_object *o, uint16_t size)
{
    uint32_t sum = 0;
    for (int k = 0; k < 3; k++) {
        int16_t c = (int16_t)get16(o, EP_OBJ_CAM + 2 * k);
        sum += (uint32_t)((int32_t)c * c);
    }
    /* integer square root of the high word, counted in an 8-bit register */
    uint16_t hi = (uint16_t)(sum >> 16), odd = 0xffff;
    uint8_t n = 0;
    for (;;) {
        odd = (uint16_t)(odd + 2);
        n++;
        if (hi < odd) break;
        hi = (uint16_t)(hi - odd);
    }
    int32_t num = (int32_t)((uint32_t)size << 16);
    num >>= (o->b[EP_OBJ_ANGLE] & 0xff) < 32 ? o->b[EP_OBJ_ANGLE] : 31;
    uint16_t d = (uint16_t)(n << 8);
    if (!d || (uint32_t)num / d > 0xffff) return 0xff; /* divide error -> 46dd */
    uint32_t q = (uint32_t)num / d;
    return q < 0x100 ? (uint16_t)q : 0xff;
}

ep_ship_view ep_ship_view_of(const ep_space *s, const ep_object *o)
{
    ep_ship_view v;
    v.flags0 = o->b[EP_OBJ_FLAGS];
    for (int k = 0; k < 3; k++) {
        v.angle[k] = get16(o, EP_OBJ_ANGLE + 2 * k);
        v.cam[k] = (int16_t)get16(o, EP_OBJ_CAM + 2 * k);
        v.player_angle[k] = s->player_angle[k];
    }
    v.flags1e = o->b[EP_OBJ_FLAGS1E];
    v.extra_angle = s->extra_angle;
    return v;
}
