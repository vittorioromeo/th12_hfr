#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
/* Renderer-independent quad history, shared by TH08 and later D3D9 adapters.
 * Rotation follows an arc rather than shrinking a rotating sprite along chords.
 * Reused VMs, script restarts, teleports and gaps snap to the native geometry. */
struct FixedQuadHistory {
    uintptr_t key, script; uint64_t tick, shared_tick; int age, valid;
    float previous[4][3], current[4][3];
};
static void fixed_quad_shape(const struct FixedQuadHistory* h, double alpha, int predict, float out[4][3]) {
    const float a = (float)alpha;
    float pc[3] = {0, 0, 0}, cc[3] = {0, 0, 0};
    for (unsigned c = 0; c < 4; ++c) for (unsigned i = 0; i < 3; ++i) { pc[i] += h->previous[c][i] * 0.25f; cc[i] += h->current[c][i] * 0.25f; }
    float centre[3];
    for (unsigned i = 0; i < 3; ++i) centre[i] = (predict ? cc[i] : pc[i]) + (cc[i] - pc[i]) * a;
    /* the turn, from the first corner's arm */
    float ax = h->previous[0][0] - pc[0], ay = h->previous[0][1] - pc[1];
    float bx = h->current[0][0] - cc[0],  by = h->current[0][1] - cc[1];
    float cross = ax * by - ay * bx, dot = ax * bx + ay * by;
    float la = ax * ax + ay * ay, lb = bx * bx + by * by;
    float cs = 1.0f, sn = 0.0f; int turning = 0;
    if (la > 0.25f && lb > 0.25f && fabsf(cross) > 1e-4f * sqrtf(la * lb)) {
        float theta = atan2f(cross, dot);
        if (fabsf(theta) < 1.0f) { cs = cosf(theta * a); sn = sinf(theta * a); turning = 1; }
        else { cs = 2.0f; }                                /* not a rotation: straight lines */
    }
    for (unsigned c = 0; c < 4; ++c) {
        if (cs > 1.5f) {
            for (unsigned i = 0; i < 3; ++i)
                out[c][i] = (predict ? h->current[c][i] : h->previous[c][i]) + (h->current[c][i] - h->previous[c][i]) * a;
            continue;
        }
        float ox = h->previous[c][0] - pc[0], oy = h->previous[c][1] - pc[1];
        float nx = h->current[c][0] - cc[0],  ny = h->current[c][1] - cc[1];
        float fx, fy;
        if (!turning) {                                    /* pure translation and scale: the arms are parallel */
            fx = (predict ? nx : ox) + (nx - ox) * a; fy = (predict ? ny : oy) + (ny - oy) * a;
        } else {
            float lo = sqrtf(ox * ox + oy * oy), ln = sqrtf(nx * nx + ny * ny);
            float from_x = predict ? nx : ox, from_y = predict ? ny : oy, from_l = predict ? ln : lo;
            float want = from_l + (ln - lo) * a, k = from_l > 1e-3f ? want / from_l : 1.0f;
            if (k < 0) k = 0;
            fx = (from_x * cs - from_y * sn) * k; fy = (from_x * sn + from_y * cs) * k;
        }
        out[c][0] = centre[0] + fx; out[c][1] = centre[1] + fy;
        out[c][2] = (predict ? h->current[c][2] : h->previous[c][2]) + (h->current[c][2] - h->previous[c][2]) * a;
    }
}
static int fixed_quad_pose(struct FixedQuadHistory* h, uintptr_t key, uintptr_t script, uint64_t tick, int age,
                          float pos[4][3], double alpha, int predict, float out[4][3]) {
    for (unsigned c = 0; c < 4; ++c) for (unsigned i = 0; i < 3; ++i) if (!isfinite(pos[c][i])) { h->valid = 0; return 0; }
    if (h->key != key || h->script != script || tick > h->tick + 1 || tick < h->tick || age < h->age) {
        if (h->key != key) h->shared_tick = 0;
        h->key = key; h->script = script; h->tick = tick; h->age = age; h->valid = 0;
        memcpy(h->current, pos, sizeof h->current); memcpy(h->previous, pos, sizeof h->previous);
    } else if (tick != h->tick) {
        float cx = 0, cy = 0, px = 0, py = 0;
        for (unsigned c = 0; c < 4; ++c) { cx += pos[c][0]; cy += pos[c][1]; px += h->current[c][0]; py += h->current[c][1]; }
        float dx = (cx - px) * 0.25f, dy = (cy - py) * 0.25f;
        memcpy(h->previous, h->current, sizeof h->previous); memcpy(h->current, pos, sizeof h->current);
        h->tick = tick; h->age = age; h->valid = dx * dx + dy * dy < 64.0f * 64.0f;
        /* A VM that has recently stood in for several sprites still is one: its first draw of a
           tick would otherwise be smoothed from wherever its last draw of the previous tick
           happened to be, which is a different sprite's position (text does this per glyph). */
        if (h->shared_tick && tick - h->shared_tick < 120) h->valid = 0;
    } else if (memcmp(h->current, pos, sizeof h->current)) {
        h->valid = 0; h->shared_tick = tick; memcpy(h->current, pos, sizeof h->current);
    }
    if (!h->valid) return 0;
    fixed_quad_shape(h, alpha, predict, out);
    return memcmp(out, pos, sizeof h->current) != 0;
}
