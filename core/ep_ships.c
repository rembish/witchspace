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

/* 6d83's rounded product, for the DX it leaves */
static uint16_t rmul(int16_t a, int16_t b)
{
    uint32_t p = (uint32_t)((int32_t)a * b);
    return (uint16_t)((uint16_t)(p >> 15) + ((p >> 14) & 1));
}

uint16_t ep_ship_velocity(ep_game *g, ep_object *o)
{
    set_slot(g, 3, get16(o, 0x0a));
    set_slot(g, 4, get16(o, 0x0c));
    int16_t a = 0, b = (int8_t)o->b[0x18];
    rot(g, 4, &a, &b);
    o->b[0x19] = (uint8_t)a;
    uint16_t dx = rmul((int16_t)(uint16_t)((uint16_t)b << 1), g->space.rot[3].sin);
    a = 0;
    rot(g, 3, &a, &b);
    o->b[0x1a] = (uint8_t)a;
    o->b[0x1b] = (uint8_t)b;
    return dx;
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

int ep_particle_update(ep_object *o)
{
    if (--o->b[0x2e] == 0) {
        o->b[EP_OBJ_FLAGS] &= 0xfe;
        return 0;
    }
    o->b[0x2f]++;
    set16(o, 0x0e, (uint16_t)(get16(o, 0x0e) + (int8_t)o->b[0x26]));
    set16(o, 0x0a, (uint16_t)(get16(o, 0x0a) + (int8_t)o->b[0x27]));
    ep_ship_move(o);
    return 1;
}

/* 7e58 leaves DX the sign of the z velocity */
static uint8_t move_dl(const ep_object *o) { return (o->b[0x1b] & 0x80) ? 0xff : 0; }

static void move(ep_game *g, ep_object *o)
{
    ep_ship_move(o);
    g->f.reg_dl = move_dl(o);
}

static void velocity(ep_game *g, ep_object *o) { g->f.reg_dl = (uint8_t)ep_ship_velocity(g, o); }

static void particle(ep_game *g, ep_object *o)
{
    if (ep_particle_update(o)) g->f.reg_dl = move_dl(o);
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
        particle(g, p);
        if (f->exploding_station)
            for (int k = 0; k < 10; k++) particle(g, p);
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
        move(g, s);
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

/* ---- the class handlers of 77e0 (re/AI.md) ---- */

/* a slot from a data address (+29, +3a), NULL for 0 or anything else */
static ep_object *obj_at(ep_game *g, uint16_t addr)
{
    if (addr < 0x76de) return NULL;
    unsigned off = addr - 0x76deu;
    if (off % 0x40 || off / 0x40 >= EP_OBJECTS) return NULL;
    return &g->space.obj[off / 0x40];
}

static uint16_t abs16(uint16_t v) { return (v & 0x8000) ? (uint16_t)(0u - v) : v; }

/* 6b54: every |coordinate| below d */
static int in_box3(uint16_t x, uint16_t y, uint16_t z, uint16_t d)
{
    return abs16(x) < d && abs16(y) < d && abs16(z) < d;
}

/* 6b4b: the low position words (the high bytes are not looked at) */
static int in_box(const ep_object *o, uint16_t d)
{
    return in_box3(get16(o, EP_OBJ_POS), get16(o, EP_OBJ_POS + 2), get16(o, EP_OBJ_POS + 4), d);
}

/* mov dh,[di+1c] with DL as some earlier routine left it */
static uint16_t stale_range(const ep_game *g, const ep_object *o)
{
    return (uint16_t)(o->b[0x1c] << 8 | g->f.reg_dl);
}

static int is_rock(const ep_object *o)
{
    int t = type_of(o);
    return t == 12 || t == 6 || t == 5 || t == 11;
}

static int is_police(const ep_object *o) { return type_of(o) == 28 && get16(o, 0x3a) == 1; }

static uint8_t legal(const ep_game *g) { return g->cmdr.b[EP_CMDR_LEGAL]; }

static void add_legal_sat(ep_game *g, unsigned v)
{
    unsigned l = legal(g) + v;
    g->cmdr.b[EP_CMDR_LEGAL] = (uint8_t)(l > 0xff ? 0xff : l);
}

/* 886d: the station's peace stops this ship firing */
static int held_by_safe_zone(const ep_game *g, const ep_object *o)
{
    if (g->f.station_angry == 1 || is_police(o)) return 0;
    return safe_zone(g);
}

static int player_untouchable(const ep_game *g) { return g->f.scoop_lock | g->f.no_crash | g->f.ai_hold; }

typedef struct {
    uint16_t a, b; /* the target angles */
} aim_t;

/* 7de8 (towards the player) or 7dde (away) and 7df2 */
static aim_t aim_at_player(ep_game *g, const ep_object *o, int away)
{
    aim_t t;
    int16_t x = (int16_t)get16(o, EP_OBJ_POS), y = (int16_t)get16(o, EP_OBJ_POS + 2),
            z = (int16_t)get16(o, EP_OBJ_POS + 4);
    if (!away) {
        x = (int16_t)(0u - (uint16_t)x);
        y = (int16_t)(0u - (uint16_t)y);
        z = (int16_t)(0u - (uint16_t)z);
    }
    ep_aim(g, x, y, z, &t.a, &t.b);
    return t;
}

/* 8034 */
static uint16_t turn_step(const ep_object *o, uint16_t target, uint16_t cur, uint16_t *err)
{
    int16_t d = (int16_t)((target & 0x7ff) - (cur & 0x7ff));
    uint16_t m = abs16((uint16_t)d);
    uint16_t turn = o->b[0x1d];
    *err = m;
    if (m < turn) return (uint16_t)d;
    return d < 0 ? (uint16_t)(0u - turn) : turn;
}

typedef struct {
    uint16_t ea, eb; /* |angle errors| */
} steer_t;

/* 8019: turn towards the angles, at most +1d per frame each */
static steer_t steer(ep_game *g, ep_object *o, aim_t t)
{
    steer_t s;
    set16(o, 0x0a, (uint16_t)(get16(o, 0x0a) + turn_step(o, t.a, get16(o, 0x0a), &s.ea)));
    set16(o, 0x0c, (uint16_t)(get16(o, 0x0c) + turn_step(o, t.b, get16(o, 0x0c), &s.eb)));
    g->f.reg_dl = (uint8_t)t.b;
    return s;
}

/* 8059: fire the laser at the player when nearly lined up */
static void fire_laser(ep_game *g, ep_object *o, steer_t s)
{
    ep_flight *f = &g->f;
    if (!(o->b[EP_OBJ_FLAGS1E] & 2)) return;
    if ((uint8_t)rng(g) >= o->b[0x30]) return;
    if (held_by_safe_zone(g, o) || player_untouchable(g)) return;
    uint8_t a = (uint8_t)(o->b[0x30] - 5);
    if (a >= 0x14) o->b[0x30] = a;
    f->reg_dl = 0xc8;
    if (!in_box3(0, s.eb, s.ea, 200)) return;
    f->attacker = SLOT_ADDR(slot_index(g, o));
    f->under_fire = 2;
    f->hit_from_behind = o->b[0x3c];
    f->reg_dl = 0x46;
    if (in_box3(o->b[0x3c], s.eb, s.ea, 70)) f->under_fire = 1; /* ax is +3c by now: close too */
}

/* 81e5: launch a missile (14h), escape pod (15h), thargon (7) or Krait (5) from o */
static int launch_child(ep_game *g, ep_object *o, uint8_t kind)
{
    ep_object *c = ep_free_ship_slot(g);
    if (!c) return 0;
    if (kind != 0x14 && kind != 0x15 && kind != 7 && kind != 5) return 0;
    memcpy(c->b, o->b, sizeof c->b);
    switch (kind) {
    case 0x14: /* 7b85 */
        ep_ship_init(c, 0);
        c->b[0x33] = 2;
        for (int k = 0; k < 3; k++) move(g, c);
        set16(c, 0x29, 0);
        break;
    case 0x15: /* 7bac, 7e1f */
        ep_ship_init(c, 1);
        c->b[0x33] = 3;
        set16(c, 0x0a, rng(g));
        set16(c, 0x0c, rng(g));
        set16(c, 0x0e, rng(g));
        velocity(g, c);
        for (int k = 0; k < 3; k++) move(g, c);
        break;
    case 7: /* 7bd3 */
        ep_ship_init(c, 28);
        c->b[0x33] = 5;
        velocity(g, c);
        move(g, c);
        move(g, c);
        set16(c, 0x3a, SLOT_ADDR(slot_index(g, o)));
        break;
    default: /* 7bc6 */
        ep_ship_init(c, 17);
        c->b[0x33] = 6;
        velocity(g, c);
        move(g, c);
        move(g, c);
    }
    return 1;
}

/* 829a: maybe launch a missile (chance p in 65536) */
static void launch_missile(ep_game *g, ep_object *o, uint16_t p)
{
    if (g->cmdr.b[EP_CMDR_KILLS] < 3 || !(o->b[EP_OBJ_FLAGS1E] & 1) || held_by_safe_zone(g, o) ||
        !o->b[0x32] || player_untouchable(g))
        return;
    if (rng(g) >= p) return;
    g->f.reg_dl = 0x14;
    if (launch_child(g, o, 0x14)) o->b[0x32]--;
}

/* 82d1: a Thargoid launches a thargon now and then */
static void launch_thargon(ep_game *g, ep_object *o)
{
    if (type_of(o) != 22 || !o->b[0x1f]) return;
    if (rng(g) >= 0x12c) return;
    g->f.reg_dl = 7;
    if (launch_child(g, o, 7)) o->b[0x1f]--;
}

/* 85bf, 87f3: a slow weave while fleeing */
static void weave(ep_game *g, ep_object *o)
{
    if (!o->b[0x35]) {
        for (int k = 0; k < 2; k++) {
            uint16_t r = rng(g);
            uint8_t lo = (uint8_t)r;
            uint16_t w = (uint16_t)(0x100 | (uint8_t)(lo >> 1 | lo << 7));
            if (lo & 1) w = (uint16_t)(0u - w);
            set16(o, 0x36 + 2 * k, w);
        }
        o->b[0x35] = 10;
    }
    if (--o->b[0x35] == 0) {
        o->b[0x35] = 10;
        set16(o, 0x36, (uint16_t)(0u - get16(o, 0x36)));
        set16(o, 0x38, (uint16_t)(0u - get16(o, 0x38)));
    }
}

/* flee from the player, weaving */
static void flee(ep_game *g, ep_object *o)
{
    aim_t t = aim_at_player(g, o, 0);
    t.a = (uint16_t)(t.a + 0x400 + get16(o, 0x36));
    t.b = (uint16_t)(t.b + get16(o, 0x38));
    steer(g, o, t);
}

static void add_roll(ep_object *o, uint16_t v) { set16(o, 0x0e, (uint16_t)(get16(o, 0x0e) + v)); }

/* 7e86: the station's ECM destroys every missile */
static void ecm_sweep(ep_game *g)
{
    for (int i = 0; i < g->space.ship_slots && i < EP_OBJECTS; i++) {
        ep_object *m = &g->space.obj[i];
        if ((m->b[EP_OBJ_FLAGS] & 1) && type_of(m) == 20) m->b[EP_OBJ_FLAGS] &= 0xfe;
    }
}

/* 83f5: the station launches traffic when hit, and watches for missiles */
static void ai_station(ep_game *g, ep_object *o)
{
    ep_flight *f = &g->f;
    add_roll(o, 10);
    if (f->danger_gov >= 1 && (o->b[EP_OBJ_FLAGS1E] & 1) && fits16(o)) {
        f->reg_dl = 0xc2;
        if (!in_box(o, 0x1c2) && legal(g) >= 10) {
            int ok = legal(g) >= 40 ? rng(g) < 0x7d0 : rng(g) < 0x5a;
            ep_object *s;
            if (ok && (s = ep_free_ship_slot(g))) {
                memcpy(s->b, o->b, sizeof s->b);
                uint16_t r = rng(g);
                if (r >= 10000) { /* 7a50: a Viper */
                    ep_ship_init(s, 14);
                    s->b[0x33] = 4;
                    set16(s, 0x3a, 1);
                    s->b[0x30] = 0x64;
                } else if (r >= 5000) { /* 7bb9: a shuttle */
                    ep_ship_init(s, 7);
                    s->b[0x33] = 3;
                } else {
                    spawn_trader(g, s);
                }
                uint32_t z = (uint32_t)s->b[EP_OBJ_POS_HI + 2] << 16 | get16(s, EP_OBJ_POS + 4);
                uint32_t nz = (uint32_t)get16(s, EP_OBJ_POS + 4) + 0xf0;
                set16(s, EP_OBJ_POS + 4, (uint16_t)nz);
                s->b[EP_OBJ_POS_HI + 2] = (uint8_t)((z >> 16) + (nz >> 16));
                set16(s, 0x0c, (uint16_t)(get16(s, 0x0c) + 0x400));
                set16(s, 0x0e, (uint16_t)(0u - get16(s, 0x0e)));
                velocity(g, s);
            }
        }
    }
    if (f->station_ecm == 0) { /* 8471: missiles at the station or at the police nearby */
        unsigned add = 0;
        uint16_t self = SLOT_ADDR(slot_index(g, o));
        for (int i = 0; i < g->space.ship_slots && i < EP_OBJECTS && !add; i++) {
            const ep_object *m = &g->space.obj[i];
            uint16_t t = get16(m, 0x29);
            if (!(m->b[EP_OBJ_FLAGS] & 1) || type_of(m) != 20 || !t) continue;
            const ep_object *to = obj_at(g, t);
            if (t == self)
                add = 40;
            else if (to && is_police(to) && safe_zone(g))
                add = 10;
        }
        if (!add) return;
        add_legal_sat(g, add);
        if (f->energy_drain == 1) return;
        f->station_ecm = 20;
    }
    if (f->station_angry == 1) {
        f->station_ecm = 0;
        return;
    }
    f->ecm_shown = 1;
    ecm_sweep(g);
    f->station_ecm--;
}

/* 8352: a missile homes in on its target (+29) or the player */
static void ai_missile(ep_game *g, ep_object *o)
{
    ep_flight *f = &g->f;
    f->missile_alert = 0;
    add_roll(o, 0x28);
    uint16_t taddr = get16(o, 0x29);
    ep_object *t = obj_at(g, taddr);
    uint16_t d[3];
    if (taddr) {
        if (!t || !(t->b[EP_OBJ_FLAGS] & 1)) {
            ep_explode(g, o);
            return;
        }
        for (int k = 0; k < 3; k++)
            d[k] = (uint16_t)(get16(t, EP_OBJ_POS + 2 * k) - get16(o, EP_OBJ_POS + 2 * k));
    } else {
        for (int k = 0; k < 3; k++) d[k] = (uint16_t)(0u - get16(o, EP_OBJ_POS + 2 * k));
        f->missile_alert = 1;
    }
    f->reg_dl = 0xc8;
    if (!in_box3(d[0], d[1], d[2], 200)) {
        aim_t a;
        ep_aim(g, (int16_t)d[0], (int16_t)d[1], (int16_t)d[2], &a.a, &a.b);
        steer(g, o, a);
        velocity(g, o);
        move(g, o);
        return;
    }
    ep_explode(g, o);
    taddr = get16(o, 0x29);
    t = obj_at(g, taddr);
    if (!taddr) {
        ep_damage(g, 0x320);
        return;
    }
    if (!t) return;
    if (type_of(t) <= 1) {
        if (f->station_angry == 1) {
            uint8_t e = t->b[0x2b];
            t->b[0x2b] = (uint8_t)(e - 10);
            if (e < 10) {
                ep_explode(g, t);
                f->station_hit = 1;
                f->station_angry = 0;
            }
        } else {
            add_legal_sat(g, 5);
        }
        return;
    }
    ep_kill_reward(g, t);
    if (!(t->b[EP_OBJ_FLAGS1E] & 4)) ep_explode(g, t);
}

/* 84e1: junk drifts; rocks tumble */
static void ai_junk(ep_game *g, ep_object *o)
{
    move(g, o);
    if (!is_rock(o)) return;
    uint16_t a = 0x37, b = 0xffdf;
    if (!(o->b[EP_OBJ_FLAGS] & 2)) {
        a = 0xffdf;
        b = 0x37;
    }
    set16(o, 0x0a, (uint16_t)(get16(o, 0x0a) + a));
    add_roll(o, b);
}

/* 84fc: traders and police: fly on, attack when hit (police: when wanted), flee */
static void ai_trader(ep_game *g, ep_object *o)
{
    uint8_t *state = &o->b[0x17];
    if (is_rock(o)) { /* the armed asteroid: a Krait hides inside */
        add_roll(o, 20);
        if ((o->b[EP_OBJ_FLAGS1E] & 1) && !(o->b[EP_OBJ_FLAGS1E] & 0x10)) {
            g->f.reg_dl = 5;
            if (launch_child(g, o, 5)) o->b[EP_OBJ_FLAGS1E] |= 0x10;
        }
        move(g, o);
        return;
    }
    switch (*state) {
    case 0: *state = 1; break;
    case 1:
        if (is_police(o) && legal(g) >= 5)
            *state = 2;
        else if (o->b[EP_OBJ_FLAGS1E] & 1)
            *state = rng(g) < 0x53fc ? 3 : 2;
        break;
    case 2: {
        if (!is_police(o) && o->b[0x2b] < 8) {
            *state = 3;
            break;
        }
        g->f.reg_dl = 0x20;
        if (in_box(o, 0x320)) {
            *state = 4;
            break;
        }
        steer_t s = steer(g, o, aim_at_player(g, o, 0));
        fire_laser(g, o, s);
        if (is_police(o) && legal(g)) launch_missile(g, o, legal(g) < 10 ? 100 : 3000);
        velocity(g, o);
        break;
    }
    case 3:
        if (o->b[0x2b] < 3) launch_missile(g, o, 1000);
        weave(g, o);
        flee(g, o);
        velocity(g, o);
        if (rng(g) < 0x32) g->f.station_ecm = 20;
        break;
    default:
        if (!in_box(o, stale_range(g, o))) {
            *state = 2;
            break;
        }
        if (o->b[0x2b] < 5 && !is_police(o)) {
            *state = 3;
            break;
        }
        steer(g, o, aim_at_player(g, o, 1));
        velocity(g, o);
    }
    move(g, o);
}

/* 8645: pirates, Thargoids and thargons: attack in passes */
static void ai_hostile(ep_game *g, ep_object *o)
{
    uint8_t *state = &o->b[0x17];
    int type = type_of(o);
    if (type == 22) {
        if (rng(g) < 100) g->f.station_ecm = 30;
        add_roll(o, 20);
    } else if (type == 7) {
        add_roll(o, 20);
    }
    switch (*state) {
    case 0: *state = 1; break;
    case 1:
        if (!((o->b[EP_OBJ_FLAGS1E] & 1) && o->b[0x30] < 10)) {
            o->b[0x16] = (uint8_t)(((rng(g) >> 8) & 3) + 2);
            *state = 2;
        }
        break;
    case 2: {
        g->f.reg_dl = 0xe8;
        if (in_box(o, 1000)) {
            *state = 3;
            break;
        }
        steer_t s = steer(g, o, aim_at_player(g, o, 0));
        fire_laser(g, o, s);
        launch_missile(g, o, 1000);
        launch_thargon(g, o);
        velocity(g, o);
        move(g, o);
        add_roll(o, 15);
        return;
    }
    case 3: {
        if (in_box(o, stale_range(g, o))) { /* fly through and away */
            steer(g, o, aim_at_player(g, o, 1));
            velocity(g, o);
            launch_thargon(g, o);
            break;
        }
        int give_up = --o->b[0x16] == 0;
        if (!give_up && type == 7) {
            const ep_object *p = obj_at(g, get16(o, 0x3a));
            if (get16(o, 0x3a) && (!p || type_of(p) != 22 || !(p->b[EP_OBJ_FLAGS1E] & 2))) give_up = 1;
        }
        if (give_up) {
            o->b[0x30] = 9;
            o->b[EP_OBJ_FLAGS1E] &= 0xfe;
            *state = type == 7 ? 10 : 1;
        } else {
            *state = 2;
        }
        break;
    }
    default: /* an orphaned thargon slows down */
        if (o->b[0x18] >= 10) {
            o->b[0x18] -= 3;
            velocity(g, o);
        }
    }
    move(g, o);
}

/* 82ef: other loners on the scanner (inactive slots too); the last one found */
static int count_loners(const ep_game *g, const ep_object *o, uint16_t *last)
{
    int n = 0;
    for (int i = 0; i < g->space.ship_slots && i < EP_OBJECTS; i++) {
        const ep_object *s = &g->space.obj[i];
        if (s->b[0x33] != 6 || !(s->b[EP_OBJ_FLAGS1E] & 2) || s == o) continue;
        *last = SLOT_ADDR(i);
        n++;
    }
    return (uint8_t)n;
}

/* 873b: loners and bounty hunters */
static void ai_loner(ep_game *g, ep_object *o)
{
    uint8_t *state = &o->b[0x17];
    switch (*state) {
    case 0: {
        if (!(o->b[EP_OBJ_FLAGS1E] & 2)) {
            steer(g, o, aim_at_player(g, o, 0));
            velocity(g, o);
            break;
        }
        if (o->b[EP_OBJ_FLAGS1E] & 1) {
            *state = 3;
            break;
        }
        uint16_t last = 0;
        int n = count_loners(g, o, &last);
        if (n >= 2) {
            if (legal(g) >= 40 || rng(g) < 0x32) *state = 1;
        } else if (n == 1) {
            set16(o, 0x29, last);
            *state = 2;
        }
        break;
    }
    case 1: {
        g->f.reg_dl = 0xe8;
        if (in_box(o, 1000)) {
            *state = 3;
            break;
        }
        steer_t s = steer(g, o, aim_at_player(g, o, 0));
        fire_laser(g, o, s);
        launch_missile(g, o, 1500);
        velocity(g, o);
        add_roll(o, 20);
        break;
    }
    case 2: { /* 8314: close in on the other loner */
        const ep_object *t = obj_at(g, get16(o, 0x29));
        uint16_t d[3];
        for (int k = 0; k < 3; k++) {
            int16_t tv = t ? (int16_t)get16(t, EP_OBJ_POS + 2 * k) : 0;
            d[k] = (uint16_t)((tv >> 2) - ((int16_t)get16(o, EP_OBJ_POS + 2 * k) >> 2));
        }
        g->f.reg_dl = 0xf4;
        if (in_box3(d[0], d[1], d[2], 2000 >> 2)) {
            *state = 4;
            break;
        }
        aim_t a;
        ep_aim(g, (int16_t)d[0], (int16_t)d[1], (int16_t)d[2], &a.a, &a.b);
        steer(g, o, a);
        velocity(g, o);
        break;
    }
    case 3:
        if (!in_box(o, stale_range(g, o))) {
            *state = 0;
            break;
        }
        weave(g, o);
        flee(g, o);
        launch_missile(g, o, 1500);
        velocity(g, o);
        break;
    default: {
        g->f.reg_dl = 0x88;
        if (in_box(o, 5000)) { /* close: start over */
            *state = 0;
            break;
        }
        steer_t s = steer(g, o, aim_at_player(g, o, 0));
        fire_laser(g, o, s);
        launch_missile(g, o, 2500);
        velocity(g, o);
    }
    }
    move(g, o);
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
        case 0: break; /* 83f4: nothing (sun, planet, hulk) */
        case 1: ai_station(g, o); break;
        case 2: ai_missile(g, o); break;
        case 3: ai_junk(g, o); break;
        case 4: ai_trader(g, o); break;
        case 5: ai_hostile(g, o); break;
        case 6: ai_loner(g, o); break;
        case 7: particle(g, o); break;                 /* 81c7 */
        default: ep_event_add(g, EP_EV_UNPORTED, cls); /* a jump through the table's tail */
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
