#pragma once
#include <stdint.h>
#include <math.h>
/* Sub-tick player movement, engine-independent half.
 *
 * The native update applies a whole frame of player displacement at once. When sub-tick
 * movement is on, that step is scaled to zero and this pass owns the motion instead: at
 * every presented iteration it advances the player by the time elapsed since it last did
 * so, using input polled at that instant. Over one 60 Hz frame the fractions sum to
 * exactly one frame of time, so the player still travels one frame's distance while
 * holding one direction -- but a direction change is acted on within the frame, which is
 * the whole point. Everything else (collision, shots, scripts, RNG, replays) still runs
 * once per 60 Hz frame, on the player position this pass has produced.
 *
 * Time is measured in frames: tau = completed native ticks + phase within the current one.
 */
struct SubtickPlayer {
    double moved_to;     /* tau up to which displacement has been applied */
    int    armed;        /* the native movement site ran on the last native tick */
};
/* The input word's direction and focus bits, and the resulting per-frame displacement.
   Mirrors the native switch: right wins over left, up over down, focus and diagonal pick
   one of four speeds the player object stores. */
enum { SUBTICK_FOCUS = 0x04, SUBTICK_UP = 0x10, SUBTICK_DOWN = 0x20,
       SUBTICK_LEFT = 0x40, SUBTICK_RIGHT = 0x80 };
static void subtick_direction(unsigned input, const float straight[2], const float diagonal[2],
                              float* out_x, float* out_y) {
    int focused = (input & SUBTICK_FOCUS) != 0;
    float s = straight[focused], d = diagonal[focused], x = 0, y = 0;
    if (input & SUBTICK_UP) {
        if (input & SUBTICK_RIGHT)     { x =  d; y = -d; }
        else if (input & SUBTICK_LEFT) { x = -d; y = -d; }
        else                           {         y = -s; }
    } else if (input & SUBTICK_DOWN) {
        if (input & SUBTICK_RIGHT)     { x =  d; y =  d; }
        else if (input & SUBTICK_LEFT) { x = -d; y =  d; }
        else                           {         y =  s; }
    } else {
        if (input & SUBTICK_RIGHT)     { x =  s; }
        else if (input & SUBTICK_LEFT) { x = -s; }
    }
    *out_x = x; *out_y = y;
}
/* How much of a frame to apply now. Zero when the pass must not move the player: it is
   not armed, time went backwards, or a stall/fast-forward left a gap too large to
   attribute to the current frame (the native tick will cover that). */
static double subtick_slice(struct SubtickPlayer* s, double tau, int armed) {
    double dt = tau - s->moved_to;
    s->moved_to = tau;
    if (!armed || !(dt > 0.0) || dt > 2.0) return 0.0;
    return dt;
}
/* The native clamp, which runs immediately after the native step. */
static float subtick_clamp(float value, float low, float size) {
    if (!(value >= low)) return low;
    return value > low + size ? low + size : value;
}
