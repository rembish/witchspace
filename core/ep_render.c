/* Elite Plus ship rendering, reconstructed from ELITE.EXE (see ep_render.h). */
#include "ep_render.h"

#include "ep_tables.h"

#include <string.h>

int16_t ep_qmul(int16_t a, int16_t b)
{
    int32_t p = (int32_t)a * b;
    return (int16_t)(uint16_t)((uint16_t)((uint32_t)p >> 16) << 1);
}

static int16_t sin_at(uint16_t angle) { return ep_sin1024[angle >> 6]; }

static int16_t cos_at(uint16_t angle) { return ep_sin1024[(uint16_t)(angle + 0x4000) >> 6]; }

static void mat_axis(uint16_t angle, ep_mat *m, int one, int c0, int c1, int s, int ns)
{
    memset(m, 0, sizeof *m);
    m->m[one] = 0x7ffe;
    m->m[c0] = m->m[c1] = cos_at(angle);
    m->m[s] = sin_at(angle);
    m->m[ns] = (int16_t)(uint16_t)(0u - (uint16_t)sin_at(angle));
}

void ep_mat_rot_x(uint16_t angle, ep_mat *m) { mat_axis(angle, m, 0, 4, 8, 5, 7); }

void ep_mat_rot_y(uint16_t angle, ep_mat *m) { mat_axis(angle, m, 4, 0, 8, 2, 6); }

void ep_mat_rot_z(uint16_t angle, ep_mat *m) { mat_axis(angle, m, 8, 0, 4, 1, 3); }

void ep_mat_mul(const ep_mat *a, const ep_mat *b, ep_mat *out)
{
    ep_mat t;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            uint16_t sum = 0;
            for (int k = 0; k < 3; k++)
                sum = (uint16_t)(sum + (uint16_t)ep_qmul(a->m[3 * r + k], b->m[3 * k + c]));
            t.m[3 * r + c] = (int16_t)sum;
        }
    *out = t;
}

/* Row r of m times (x, y, z), summed with 16-bit wrap-around. */
static int16_t row_dot(const ep_mat *m, int r, int16_t x, int16_t y, int16_t z)
{
    uint16_t s = (uint16_t)ep_qmul(m->m[3 * r], x);
    s = (uint16_t)(s + (uint16_t)ep_qmul(m->m[3 * r + 1], y));
    s = (uint16_t)(s + (uint16_t)ep_qmul(m->m[3 * r + 2], z));
    return (int16_t)s;
}

static int16_t rd16(const uint8_t *p) { return (int16_t)(uint16_t)(p[0] | p[1] << 8); }

/* (v * 256 + low byte of v) / z as idiv computes it: 0 on divide error, else 1. The dividend
 * keeps the low byte twice because the original does not clear al. */
static int project(int16_t v, int16_t z, int16_t *q)
{
    int32_t num = (int32_t)v * 256 + ((uint16_t)v & 0xff);
    if (z == 0) return 0;
    int32_t d = num / z;
    if (d < -32768 || d > 32767) return 0;
    *q = (int16_t)d;
    return 1;
}

static void emit(ep_render *r, uint8_t kind, uint8_t colour, const uint8_t *refs, int n)
{
    if (r->nprim >= EP_MAX_PRIMS) return;
    ep_prim *p = &r->prim[r->nprim++];
    p->kind = kind;
    p->colour = colour;
    memset(p->pt, 0, sizeof p->pt);
    for (int k = 0; k < n; k++) {
        const ep_vertex *v = &r->vtx[(uint16_t)rd16(refs + 2 * k) / 10 % EP_MAX_VERTS];
        p->pt[2 * k] = v->sx;
        p->pt[2 * k + 1] = v->sy;
    }
}

void ep_draw_model(ep_render *r, int type, const int16_t pos[3], const ep_mat *m)
{
    uint16_t off = ep_model_offset[type & 31];
    if (off == 0xffff) return;
    const uint8_t *bp = ep_models + off;
    int n = *bp++;
    for (int i = 0; i < n; i++, bp += 6) {
        ep_vertex *v = &r->vtx[i];
        int16_t x = rd16(bp), y = rd16(bp + 2), z = rd16(bp + 4);
        v->x = (int16_t)(row_dot(m, 0, x, y, z) + pos[0]);
        v->y = (int16_t)(row_dot(m, 1, x, y, z) + pos[1]);
        v->z = (int16_t)(row_dot(m, 2, x, y, z) + pos[2]);
        int16_t q;
        if (!project(v->x, v->z, &q)) {
            v->x = v->sy = 400; /* 3d63; screen x keeps whatever was there */
            continue;
        }
        v->sx = (int16_t)(q + 0x98);
        if (!project((int16_t)(v->y - (v->y >> 3)), v->z, &q)) {
            v->x = v->sy = 400;
            continue;
        }
        v->sy = (int16_t)(q + 0x3e);
    }
    static const int size[] = { 8, 10, 6 };
    while (*bp == 1) {
        const ep_vertex *ref = &r->vtx[(uint16_t)rd16(bp + 1) / 10 % EP_MAX_VERTS];
        int16_t nx = row_dot(m, 0, rd16(bp + 3), rd16(bp + 5), rd16(bp + 7));
        int16_t ny = row_dot(m, 1, rd16(bp + 3), rd16(bp + 5), rd16(bp + 7));
        int16_t nz = row_dot(m, 2, rd16(bp + 3), rd16(bp + 5), rd16(bp + 7));
        bp += 9;
        /* The sum wraps, but `jg` after the last add sees the exact sign of that add. */
        int16_t part = (int16_t)(uint16_t)((uint16_t)ep_qmul(ref->x, nx) + (uint16_t)ep_qmul(ref->y, ny));
        int visible = part + ep_qmul(ref->z, nz) <= 0;
        for (; !(*bp & 1); bp += size[*bp >> 1]) {
            if (!visible) continue;
            int npts = *bp == EP_PRIM_TRI ? 3 : *bp == EP_PRIM_QUAD ? 4 : 2;
            emit(r, *bp, bp[1 + 2 * npts], bp + 1, npts);
        }
    }
}

/* Angle word to the matrices' 16-bit angle: times 32, optionally negated first (in unsigned
 * arithmetic, as the 16-bit `neg` / `shl` do). */
static uint16_t to_angle(uint16_t a, int negate)
{
    unsigned u = negate ? 0x10000u - a : a;
    return (uint16_t)(u << 5);
}

void ep_draw_ship(ep_render *r, const ep_ship_view *v)
{
    int type = (v->flags0 >> 1) & 0x1f;
    if (type >= 30 || (v->flags1e & 0x60) == 0x60) return;
    uint16_t a = to_angle((uint16_t)(v->angle[0] + v->player_angle[0]), 1);
    uint16_t b = to_angle(v->angle[1], 1);
    uint16_t c = to_angle(v->angle[2], 0);
    ep_mat rx, rz, ry, p1, p2, p3, t1, t2;
    ep_mat_rot_x(a, &rx);
    ep_mat_rot_z(c, &rz);
    ep_mat_rot_y(b, &ry);
    ep_mat_rot_y(to_angle(v->player_angle[1], 1), &p1);
    ep_mat_rot_z(to_angle(v->player_angle[2], 1), &p2);
    ep_mat_rot_y(to_angle(v->extra_angle, 0), &p3);
    ep_mat_mul(&p3, &p2, &t1); /* 2b78 = 2be4 x 2bd2 */
    ep_mat_mul(&t1, &p1, &t2); /* 2be4 = 2b78 x 2bc0 */
    ep_mat_mul(&t2, &rx, &t1); /* 2bd2 = 2be4 x 2b8a */
    ep_mat_mul(&t1, &ry, &t2); /* 2be4 = 2bd2 x 2b9c */
    ep_mat_mul(&t2, &rz, &t1); /* 2b78 = 2be4 x 2bae */
    int16_t pos[3];
    for (int k = 0; k < 3; k++) {
        int32_t d = (int32_t)v->cam[k] * 2;
        if (d < -32768 || d > 32767) return;
        pos[k] = (int16_t)d;
    }
    ep_draw_model(r, type, pos, &t1);
}
