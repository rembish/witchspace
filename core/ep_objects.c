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

static uint16_t abs16(uint16_t v) { return (v & 0x8000) ? (uint16_t)(0u - v) : v; }

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

/* 4264: in range (each |coordinate| < 12000, squared distance below 0895h << 16) */
static int in_range(ep_object *o)
{
    if (!position_fits(o)) return 0;
    uint16_t c[3];
    for (int k = 0; k < 3; k++) {
        c[k] = abs16(get16(o, EP_OBJ_POS + 2 * k));
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

/* 42c8 / 4317: camera-space position and the in-view test */
static void to_camera(ep_space *s, ep_object *o)
{
    int16_t p[3];
    for (int k = 0; k < 3; k++) p[k] = (int16_t)get16(o, EP_OBJ_POS + 2 * k);
    ep_rotate_by_player(s, p);
    o->b[EP_OBJ_ZHI] = (uint8_t)((uint16_t)p[2] >> 8);
    /* 4359: scanner blip, only in flight (not reconstructed yet) */
    if (s->extra_angle) {
        s->rot[5] = ep_rot_from_angle((uint16_t)(0u - s->extra_angle));
        ep_rotate_pair(&s->rot[5], &p[0], &p[2]);
    }
    /* 42d5: scooping (fuel scoops fitted) is not reconstructed yet */
    if (p[2] < 100) return;
    for (int k = 0; k < 3; k++) set16(o, EP_OBJ_CAM + 2 * k, (uint16_t)p[k]);
    uint16_t z = (uint16_t)p[2];
    if (z < (uint16_t)(abs16((uint16_t)p[0]) << 1)) return;
    if (z < (uint16_t)(abs16((uint16_t)p[1]) << 1)) return;
    o->b[EP_OBJ_FLAGS] |= 0x80;
}

static ep_ship_view ship_view(const ep_space *s, const ep_object *o)
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

int ep_update_objects(ep_space *s, ep_render *r, int drawn[EP_OBJECTS])
{
    for (int k = 0; k < 3; k++) s->rot[k] = ep_rot_from_angle(s->player_angle[k]);
    int n = s->count < EP_OBJECTS ? s->count : EP_OBJECTS;
    for (int i = 0; i < n; i++) {
        ep_object *o = &s->obj[i];
        if (!(o->b[EP_OBJ_FLAGS] & 1)) continue;
        o->b[EP_OBJ_FLAGS] &= 0x3f;
        if (o->b[EP_OBJ_FLAGS] >= 0x3c) continue; /* 433c: planet and sun, not yet */
        if (o->b[EP_OBJ_TIMER] && !(o->b[EP_OBJ_FLAGS1E] & 2))
            if (++o->b[EP_OBJ_TIMER] == 0) continue; /* 7e82: explosion over, not yet */
        o->b[EP_OBJ_FLAGS1E] &= 0xfd;
        if (in_range(o)) to_camera(s, o);
    }
    /* 41aa: planet and sun first, not yet. 41e3: ships, farthest camera z first. */
    int nd = 0;
    for (;;) {
        uint16_t far = 0;
        int pick = -1;
        for (int i = 0; i < n; i++) {
            const ep_object *o = &s->obj[i];
            if ((o->b[EP_OBJ_FLAGS] & 0xc1) != 0xc1) continue;
            uint16_t z = get16(o, EP_OBJ_CAM + 4);
            if (far < z) {
                far = z;
                pick = i;
            }
        }
        if (!far) break;
        s->obj[pick].b[EP_OBJ_FLAGS] &= 0xbf;
        ep_ship_view v = ship_view(s, &s->obj[pick]);
        ep_draw_ship(r, &v);
        drawn[nd++] = pick;
    }
    /* 487e: compass, only in flight */
    return nd;
}
