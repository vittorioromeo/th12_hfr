/* Dyadic sub-step scheduling for an engine whose timers are integers.
   
   The x86 runtime's schedule (timing.c) lets a tick straddle a frame boundary: the
   engine's own float timers accumulate the step and cross the integer frame count
   somewhere inside a tick, and "major" simply means the first tick that starts in a new
   frame. New Classic has no float timers -- this runtime decides itself when the 60 Hz
   logic runs -- so a straddling step would apply part of the next frame's motion before
   that frame's logic had run.

   Here the partition is exact instead. The R ticks of a second are dealt out to the 60
   frames by one Bresenham, and each frame's units (1/256 of a frame, so every step and
   every partial sum is exact in float32) are dealt out to that frame's ticks by another.
   A tick therefore never crosses a boundary: the steps between two boundary ticks sum to
   exactly one frame, and exactly 60 frames pass per second at every rate. */
#ifndef HFR_SUBSTEP_H
#define HFR_SUBSTEP_H

#define SUBSTEP_UNITS 256u              /* units per game frame */
#define SUBSTEP_FRAMES 60u              /* frames per second */

struct Substep {
    unsigned frame_acc;    /* Bresenham remainder dealing ticks out to frames */
    unsigned ticks_left;   /* ticks remaining in the current frame, 0 before the first */
    unsigned units_left;   /* units remaining in the current frame */
    unsigned tick;         /* ticks since the last reset */
};

struct SubstepTick {
    int major;      /* this tick starts a frame: the 60 Hz logic belongs to it */
    float dt;       /* this tick's length in frames; what continuous motion scales by */
    double phase;   /* where this tick starts inside its frame, [0,1) */
};

static void substep_reset(struct Substep* s) {
    s->frame_acc = 0; s->ticks_left = 0; s->units_left = 0; s->tick = 0;
}

/* Advance one tick. `rate` is ticks per second (60..1000); at 60 each frame gets exactly
   one tick of exactly one frame, which is what makes sub-stepping inert there. */
static struct SubstepTick substep_advance(struct Substep* s, int rate) {
    struct SubstepTick t;
    if (rate < 60) rate = 60;
    if (rate > 1000) rate = 1000;
    t.major = (s->ticks_left == 0);
    if (t.major) {
        s->frame_acc += (unsigned)rate;
        s->ticks_left = s->frame_acc / SUBSTEP_FRAMES;
        s->frame_acc -= s->ticks_left * SUBSTEP_FRAMES;
        if (!s->ticks_left) s->ticks_left = 1;   /* a rate below 60 cannot starve a frame */
        s->units_left = SUBSTEP_UNITS;
    }
    t.phase = (double)(SUBSTEP_UNITS - s->units_left) / (double)SUBSTEP_UNITS;
    /* Ceiling share of what is left: the last tick of the frame then lands exactly on the
       boundary, whatever the remainder was. */
    unsigned u = (s->units_left + s->ticks_left - 1) / s->ticks_left;
    s->units_left -= u; s->ticks_left--;
    t.dt = (float)u / (float)SUBSTEP_UNITS;
    s->tick++;
    return t;
}
#endif
