/* Elite Plus ships: spawning, the AI frame, explosions, reconstructed from ELITE.EXE (see
 * ep_ships.h and re/SHIPS.md). */
#include "ep_ships.h"

#include "ep_combat.h"
#include "ep_tables.h"

#include <string.h>

#define SLOT_ADDR(i) ((uint16_t)(0x76de + 0x40 * (i)))

static uint16_t get16(const ep_object *o, int off) { return (uint16_t)(o->b[off] | o->b[off + 1] << 8); }

static void set16(ep_object *o, int off, uint16_t v)
{
    o->b[off] = (uint8_t)v;
    o->b[off + 1] = (uint8_t)(v >> 8);
}

static int type_of(const ep_object *o) { return (o->b[EP_OBJ_FLAGS] >> 1) & 0x1f; }

static int slot_index(const ep_game *g, const ep_object *o) { return (int)(o - g->space.obj); }

static uint16_t rng(ep_game *g) { return ep_flight_random(g); }

static void rot(ep_game *g, int n, int16_t *a, int16_t *b) { ep_rotate_pair(&g->space.rot[n], a, b); }

static void set_slot(ep_game *g, int n, uint16_t angle) { g->space.rot[n] = ep_rot_from_angle(angle); }

/* 24-bit coordinate k := v (16-bit, sign-extended) */
static void put_pos(ep_object *o, int k, int16_t v)
{
    set16(o, EP_OBJ_POS + 2 * k, (uint16_t)v);
    o->b[EP_OBJ_POS_HI + k] = v < 0 ? 0xff : 0;
}

/* 24-bit coordinate k += v (sign-extended) */
static void add_pos(ep_object *o, int k, int16_t v)
{
    uint32_t p = (uint32_t)o->b[EP_OBJ_POS_HI + k] << 16 | get16(o, EP_OBJ_POS + 2 * k);
    uint32_t d = v < 0 ? 0x1000000u - (uint32_t)(-(int32_t)v) : (uint32_t)v;
    p = (p + d) & 0xffffffu;
    set16(o, EP_OBJ_POS + 2 * k, (uint16_t)p);
    o->b[EP_OBJ_POS_HI + k] = (uint8_t)(p >> 16);
}

/* 4217 */
static int fits16(const ep_object *o)
{
    for (int k = 0; k < 3; k++) {
        uint8_t hi = o->b[EP_OBJ_POS_HI + k];
        int neg = o->b[EP_OBJ_POS + 2 * k + 1] & 0x80;
        if (!((hi == 0 && !neg) || (hi == 0xff && neg))) return 0;
    }
    return 1;
}

void ep_ship_init(ep_object *o, int entry)
{
    const uint8_t *e = ep_spawn[entry];
    o->b[0x17] = 0;
    o->b[EP_OBJ_FLAGS1E] = 0;
    o->b[0x30] = 0;
    o->b[0x34] = 1;
    o->b[EP_OBJ_FLAGS] = (uint8_t)(e[0] << 1 | 1);
    o->b[0x18] = e[1];
    o->b[0x1d] = e[2];
    o->b[0x31] = e[3];
    o->b[0x32] = e[4];
    o->b[0x2c] = e[5];
    o->b[0x2d] = e[6];
    o->b[0x2b] = e[7];
    o->b[0x3f] = e[8];
    o->b[0x1c] = e[9];
    o->b[0x25] = 0;
}

void ep_aim(ep_game *g, int16_t x, int16_t y, int16_t z, uint16_t *a, uint16_t *c)
{
    x = (int16_t)(x >> 2);
    y = (int16_t)(y >> 2);
    z = (int16_t)(z >> 2);
    uint16_t t = ep_atan2(y, z);
    set_slot(g, 5, t);
    rot(g, 5, &y, &z);
    uint16_t u = ep_atan2(x, z);
    *a = (uint16_t)(0u - t);
    *c = (uint16_t)(0u - u);
}

/* 800c: point at the player */
static void face_player(ep_game *g, ep_object *o)
{
    uint16_t a, c;
    ep_aim(g, (int16_t)(0u - get16(o, EP_OBJ_POS)), (int16_t)(0u - get16(o, EP_OBJ_POS + 2)),
           (int16_t)(0u - get16(o, EP_OBJ_POS + 4)), &a, &c);
    set16(o, 0x0a, a);
    set16(o, 0x0c, c);
}

void ep_ship_velocity(ep_game *g, ep_object *o)
{
    set_slot(g, 3, get16(o, 0x0a));
    set_slot(g, 4, get16(o, 0x0c));
    int16_t a = 0, b = (int8_t)o->b[0x18];
    rot(g, 4, &a, &b);
    o->b[0x19] = (uint8_t)a;
    a = 0;
    rot(g, 3, &a, &b);
    o->b[0x1a] = (uint8_t)a;
    o->b[0x1b] = (uint8_t)b;
}

void ep_ship_move(ep_object *o)
{
    for (int k = 0; k < 3; k++) add_pos(o, k, (int8_t)o->b[0x19 + k]);
    if (!fits16(o)) o->b[EP_OBJ_FLAGS] &= 0xfe;
}

/* 7d72: about 10000 ahead along the current rotation slots 3 and 4 */
static void spawn_position(ep_game *g, ep_object *o)
{
    uint16_t r = rng(g);
    int16_t a = (int16_t)((r & 0x7ff) >> 1);
    if (r & 1) a = (int16_t)-a;
    int16_t b = 10000;
    rot(g, 3, &a, &b);
    int16_t x = a;
    r = rng(g);
    a = (int16_t)((r & 0x7ff) >> 1);
    if (r & 1) a = (int16_t)-a;
    rot(g, 4, &a, &b);
    put_pos(o, 1, a);
    put_pos(o, 0, x);
    put_pos(o, 2, b);
}

/* 7daf */
static void face_and_roll(ep_game *g, ep_object *o)
{
    face_player(g, o);
    set16(o, 0x0e, rng(g));
}

static void spawn_common(ep_game *g, ep_object *o, int entry, uint8_t cls)
{
    ep_ship_init(o, entry);
    spawn_position(g, o);
    face_and_roll(g, o);
    o->b[0x33] = cls;
}

/* 7be0 */
static void spawn_junk(ep_game *g, ep_object *o)
{
    uint16_t r = rng(g);
    spawn_common(g, o, 1 + (((r >> 1) ^ (r >> 9)) & 7), 3);
    o->b[0x1d] = 0x1e;
    ep_ship_velocity(g, o);
}

/* 7c01 */
static void spawn_trader(ep_game *g, ep_object *o)
{
    spawn_common(g, o, 9 + (rng(g) & 0xff) / 43, 4);
    ep_ship_velocity(g, o);
    if (type_of(o) != 28) return;
    uint16_t police = rng(g) & 1;
    set16(o, 0x3a, police);
    if (police) set16(o, 0x30, g->cmdr.b[EP_CMDR_LEGAL]); /* +30 = legal status, +31 = 0 */
}

/* 7c34 */
static void spawn_loner(ep_game *g, ep_object *o)
{
    spawn_common(g, o, 15 + (rng(g) & 0xff) / 37, 6);
    o->b[0x30] = (uint8_t)(rng(g) & 0x1f);
    ep_ship_velocity(g, o);
}

/* 7c59 */
static void spawn_pirate(ep_game *g, ep_object *o)
{
    int n = g->f.hyperspace ? 5 : (rng(g) & 0xff) / 52;
    spawn_common(g, o, 22 + n, 5);
    uint8_t a = (uint8_t)(rng(g) & 0x3f);
    if (g->f.danger_gov == 0) a = (uint8_t)(a + 0x20);
    o->b[0x30] = a;
    ep_ship_velocity(g, o);
    if (type_of(o) == 22) {
        uint16_t r = rng(g);
        o->b[0x1f] = (uint8_t)((((uint8_t)r ^ (uint8_t)(r >> 8)) >> 3 & 3) + 2);
    }
}

/* 7ca7: convoy ship, the leader (type 24) or an escort (type 18/19) */
static void spawn_mission(ep_game *g, ep_object *o, int leader)
{
    uint8_t type = 0x18;
    if (!leader) type = (rng(g) & 0x8000) ? 0x12 : 0x13;
    ep_ship_init(o, 23);
    spawn_position(g, o);
    face_and_roll(g, o);
    o->b[EP_OBJ_FLAGS] = (uint8_t)(type << 1 | 1);
    o->b[0x33] = 5;
    o->b[0x30] = (uint8_t)(rng(g) & 0x7f);
    o->b[0x2b] = 0x96;
    o->b[0x2c] = 0;
    o->b[0x32] = 6;
    o->b[0x31] = 0xc8;
}

/* 7ce9 */
static void spawn_thargoid(ep_game *g, ep_object *o)
{
    spawn_common(g, o, 27, 5);
    o->b[0x30] = (uint8_t)(rng(g) & 0x7f);
    o->b[0x2b] = 0x32;
    o->b[0x2c] = 0;
    o->b[0x32] = 6;
    o->b[0x1f] = 8;
}

/* 7a12: a copy of the leader's flags, position and angles, moved by up to +-1024 */
static void copy_and_jitter(ep_game *g, ep_object *o, const ep_object *from)
{
    memcpy(o->b, from->b, 0x10);
    for (int k = 0; k < 3; k++) add_pos(o, k, (int16_t)((rng(g) & 0x7ff) - 0x400));
}

ep_object *ep_free_ship_slot(ep_game *g)
{
    int end = g->space.ship_slots;
    for (int i = 3; i < end && i < EP_OBJECTS; i++)
        if (!(g->space.obj[i].b[EP_OBJ_FLAGS] & 1)) return &g->space.obj[i];
    return NULL;
}

/* 8183: a free debris slot, else the oldest (the last of equals) */
static ep_object *debris_slot(ep_game *g)
{
    int n = g->space.debris_slots;
    for (int i = 20; i < 20 + n && i < EP_OBJECTS; i++)
        if (!(g->space.obj[i].b[EP_OBJ_FLAGS] & 1)) return &g->space.obj[i];
    ep_object *pick = NULL;
    uint8_t age = 0;
    for (int i = 20; i < 20 + n && i < EP_OBJECTS; i++)
        if (g->space.obj[i].b[0x2f] >= age) {
            age = g->space.obj[i].b[0x2f];
            pick = &g->space.obj[i];
        }
    return pick;
}

void ep_particle_update(ep_object *o)
{
    if (--o->b[0x2e] == 0) {
        o->b[EP_OBJ_FLAGS] &= 0xfe;
        return;
    }
    o->b[0x2f]++;
    set16(o, 0x0e, (uint16_t)(get16(o, 0x0e) + (int8_t)o->b[0x26]));
    set16(o, 0x0a, (uint16_t)(get16(o, 0x0a) + (int8_t)o->b[0x27]));
    ep_ship_move(o);
}

static int safe_zone(const ep_game *g) { return g->f.safe_zone & 1; }

void ep_explode(ep_game *g, ep_object *o)
{
    ep_flight *f = &g->f;
    /* 7fe5: a convoy ship */
    if (f->convoy_left && type_of(o) == 24 && o->b[0x31] == 0xc8) {
        f->convoy_left--;
        if (o->b[EP_OBJ_FLAGS1E] & 0x20) f->convoy_leader_dead = 1;
    }
    if (!(o->b[EP_OBJ_FLAGS] & 0x80)) { /* out of view: just gone */
        o->b[EP_OBJ_FLAGS] &= 0xfe;
        return;
    }
    o->b[EP_OBJ_FLAGS] &= 0xfe;
    ep_event_add(g, EP_EV_SOUND, 0x13); /* 4dff */
    f->exploding_station = type_of(o) <= 1;
    int mining = f->mining == 1;
    for (int n = o->b[0x2d]; n > 0; n--) {
        ep_object *p = debris_slot(g);
        if (!p) break;
        memcpy(p->b, o->b, sizeof p->b);
        p->b[0x17] = 0;
        uint16_t r = rng(g);
        set16(p, 0x26, r);
        int8_t al = (int8_t)((r & 0x1f) - 15), ah = (int8_t)(((r >> 8) & 0x1f) - 15);
        p->b[0x19] = (uint8_t)((int8_t)p->b[0x19] >> 1);
        p->b[0x1a] = (uint8_t)((int8_t)p->b[0x1a] >> 1);
        if (mining) {
            al = (int8_t)(al >> 1);
            ah = (int8_t)(ah >> 1);
        }
        p->b[0x19] = (uint8_t)(p->b[0x19] + (uint8_t)al);
        p->b[0x1a] = (uint8_t)(p->b[0x1a] + (uint8_t)ah);
        int8_t bl = (int8_t)(((r >> 3) & 0x1f) - 15);
        p->b[0x1b] = (uint8_t)((int8_t)p->b[0x1b] >> 1);
        if (mining) bl = (int8_t)(bl >> 1);
        p->b[0x1b] = (uint8_t)(p->b[0x1b] + (uint8_t)bl);
        p->b[EP_OBJ_FLAGS1E] = 8;
        if (mining && rng(g) < 2000) p->b[EP_OBJ_FLAGS1E] |= 0x10;
        p->b[0x2b] = 0;
        set16(p, 0x2c, 0);
        p->b[0x2f] = 0;
        p->b[0x33] = 7;
        uint8_t t = (uint8_t)(rng(g) & 0xf);
        if (mining) t = (uint8_t)(t + 0x3c);
        p->b[0x2e] = (uint8_t)(t + 0x14);
        p->b[EP_OBJ_FLAGS] = 0x17;
        ep_particle_update(p);
        if (f->exploding_station)
            for (int k = 0; k < 10; k++) ep_particle_update(p);
    }
    /* 7f80: canisters */
    unsigned count;
    if (o->b[EP_OBJ_FLAGS1E] & 0x20) {
        count = 1;
    } else {
        uint8_t c = o->b[0x2c];
        if (!c) return;
        uint8_t q = (uint8_t)(0xff / (c + 1));
        count = (rng(g) & 0xff) / (uint8_t)(q + 1);
        if (!count) return;
    }
    for (; count; count--) {
        ep_object *s = ep_free_ship_slot(g); /* can be the ship's own slot, now free */
        if (!s) continue;
        uint8_t mission = o->b[EP_OBJ_FLAGS1E] & 0x20; /* read before the copy, as the original */
        memcpy(s->b, o->b, sizeof s->b);
        ep_ship_init(s, 3);
        s->b[0x33] = 3;
        s->b[EP_OBJ_FLAGS1E] = (uint8_t)(8 | mission << 1);
        uint16_t r = rng(g);
        set16(s, 0x0a, r);
        set16(s, 0x0c, (uint16_t)(r >> 8 | r << 8));
        ep_ship_velocity(g, s);
        ep_ship_move(s);
    }
}

/* 79fa: any of the first ship slots carries a mission ship (stale slots too) */
static int any_mission_ship(const ep_game *g)
{
    for (int i = 0; i < g->space.ship_slots && i < EP_OBJECTS; i++)
        if (g->space.obj[i].b[EP_OBJ_FLAGS1E] & 0x20) return 1;
    return 0;
}

/* 7ad3: mission 4, the Viper */
static void mission4(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->mission != 4 || f->mission_state != 2) return;
    if (f->class_count[6] >= (&ep_spawn_limit[0][0])[(f->spawn_row + 3) % 32]) return; /* last frame's row */
    for (int i = 3; i < g->space.ship_slots && i < EP_OBJECTS; i++) {
        const ep_object *o = &g->space.obj[i];
        if ((o->b[EP_OBJ_FLAGS] & 0x3f) == 0x39 && o->b[0x25] == 1) return;
    }
    if (rng(g) > 0x190) return;
    ep_object *s = ep_free_ship_slot(g);
    if (!s) return;
    ep_ship_init(s, 14);
    s->b[0x33] = 5;
    s->b[0x30] = 0xc8;
    s->b[0x25] = 1;
}

/* 7a66: mission 5, Thargoids */
static void mission5(ep_game *g)
{
    ep_flight *f = &g->f;
    ep_object *s;
    if (f->mission == 5 && f->mission5_phase == 1 && rng(g) <= 0x3c && !safe_zone(g) &&
        f->class_count[6] < 1 && (s = ep_free_ship_slot(g))) {
        spawn_thargoid(g, s);
        s->b[0x1f] = 2;
    }
    if ((f->mission5_count & 7) == 5 && f->mission5_flag == 1 && rng(g) <= 0x1e && !safe_zone(g) &&
        (s = ep_free_ship_slot(g))) {
        spawn_thargoid(g, s);
        s->b[0x1f] = 4;
        f->mission5_flag = 0;
        if (rng(g) <= 0x96) f->mission5_flag++;
    }
}

/* 7b32: mission 6 */
static void mission6(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->mission != 6 || g->cmdr.b[EP_CMDR_CURRENT + EP_SYSREC_INDEX] != f->mission_system ||
        f->mission_state <= 1)
        return;
    uint16_t r = rng(g);
    if (r > 0x1388) return;
    ep_object *s;
    if (r <= 0x12c && (s = ep_free_ship_slot(g))) spawn_pirate(g, s);
    if (!(s = ep_free_ship_slot(g))) return;
    spawn_common(g, s, 5, 3);
    s->b[0x1d] = 0x1e;
    s->b[0x25] = 2;
    ep_ship_velocity(g, s);
}

static uint16_t jump(const ep_game *g, uint16_t p) { return g->f.jump_speed ? (uint16_t)(p << 5) : p; }

void ep_ai_frame(ep_game *g)
{
    ep_flight *f = &g->f;
    memset(f->class_count, 0, sizeof f->class_count);
    for (int i = 0; i < g->space.count && i < EP_OBJECTS; i++) {
        ep_object *o = &g->space.obj[i];
        if (!(o->b[EP_OBJ_FLAGS] & 1)) continue;
        uint8_t cls = o->b[0x33];
        if (cls != 7) f->class_count[0]++;
        if (cls < 8) f->class_count[1 + cls]++;
        switch (cls) {
        case 0: break;                        /* 83f4: nothing */
        case 7: ep_particle_update(o); break; /* 81c7 */
        default: {
            static const uint16_t handler[8] = { 0x83f4, 0x83f5, 0x8352, 0x84e1,
                                                 0x84fc, 0x8645, 0x873b, 0x81c7 };
            ep_event_add(g, EP_EV_UNPORTED, handler[cls & 7]); /* the class handlers: next */
        }
        }
    }
    f->ai_hold = 0;
    if (f->class_count[0] >= 10) return;
    if (g->space.ship_slots < f->class_count[0]) return;
    ep_object *s;
    if (f->convoy_left) {
        if (f->convoy_countdown == 1) { /* 794e: the convoy */
            if (f->convoy_leader_dead == 1) return;
            if (f->class_count[6] == 0) {
                if (!(s = ep_free_ship_slot(g))) return;
                f->convoy_leader = SLOT_ADDR(slot_index(g, s));
                spawn_mission(g, s, 1);
                s->b[EP_OBJ_FLAGS1E] |= 0x20;
                s->b[0x2c] = 0x14;
                s->b[0x30] = 0;
                const ep_object *lead = &g->space.obj[(f->convoy_leader - 0x76de) / 0x40 % EP_OBJECTS];
                for (int k = 0; k < 2; k++) {
                    if (!(s = ep_free_ship_slot(g))) return;
                    spawn_mission(g, s, 0);
                    copy_and_jitter(g, s, lead);
                }
                return;
            }
            if (f->class_count[6] >= 3) return;
            if (!(s = ep_free_ship_slot(g))) return;
            spawn_mission(g, s, 0);
            if (any_mission_ship(g) || f->convoy_leader_dead == 1) return;
            s->b[EP_OBJ_FLAGS1E] |= 0x20;
            s->b[0x2c] = 0x14;
            s->b[0x30] = 0;
            s->b[EP_OBJ_FLAGS] = 0x31;
            return;
        }
    } else if ((f->station_angry & f->siege) && f->station_hit != 1) { /* 79d1 */
        if (!safe_zone(g) || f->class_count[6] >= 8) return;
        if ((s = ep_free_ship_slot(g))) spawn_thargoid(g, s);
        return;
    }
    mission4(g);
    mission5(g);
    mission6(g);
    uint8_t gov = g->cmdr.b[EP_CMDR_CURRENT + EP_SYSREC_GOVERNMENT];
    f->spawn_gov8 = (uint16_t)(gov * 8);
    f->spawn_row = (uint16_t)(f->danger_gov * 4);
    const uint8_t *limit = &ep_spawn_limit[0][0] + f->spawn_row;
    const uint16_t *chance = &ep_spawn_chance[0][0] + f->spawn_gov8 / 2;
    if (!f->hyperspace) {
        uint8_t thr = (uint8_t)(limit[0] + (g->cmdr.b[EP_CMDR_EQUIPMENT + 11] == 1));
        if (f->class_count[4] < thr && rng(g) < jump(g, chance[0])) {
            if (!(s = ep_free_ship_slot(g))) return;
            spawn_junk(g, s);
        }
        if (f->class_count[5] < limit[1] && rng(g) < jump(g, chance[1])) {
            if (!(s = ep_free_ship_slot(g))) return;
            spawn_trader(g, s);
        }
        if (f->class_count[7] < limit[2] && rng(g) < jump(g, chance[2])) {
            if (!(s = ep_free_ship_slot(g))) return;
            spawn_loner(g, s);
        }
    }
    if (f->class_count[6] >= limit[3]) return;
    if (!f->hyperspace) {
        uint16_t r = rng(g);
        if (r >= jump(g, chance[3])) return;
        if (f->danger_gov && r >= 0x1c2) return;
    }
    if ((s = ep_free_ship_slot(g))) spawn_pirate(g, s);
}
