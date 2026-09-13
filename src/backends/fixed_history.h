#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
/* Renderer-independent pose history. A gap, script restart, or teleport snaps. */
struct FixedPose {
    uintptr_t key, script;
    uint64_t tick;
    int age, valid, rolled;   /* rolled: this call advanced the history to a new tick */
    float previous[3], current[3];
    float turn_previous[3], turn_current[3];     /* rotation, radians */
    float size_previous[2], size_current[2];     /* scale multipliers */
};
/* Rotation has to take the short way round, or a sprite crossing the wrap point spins
   backwards through a whole turn in one frame. Scale is an ordinary lerp. */
static float fixed_turn(float from, float to, double alpha, int predict) {
    const float turn = 6.2831853071795864f;
    float delta = to - from;
    while (delta > turn * 0.5f) delta -= turn;
    while (delta < -turn * 0.5f) delta += turn;
    return (predict ? to : from) + delta * (float)alpha;
}
/* alpha runs from the previous native position to the current one. `predict` continues past
   the current one instead, which is what the sub-tick pass needs: once the player is advancing
   between native ticks, everything drawn beside it has to be where it is now, not where it was
   a frame ago, or the two disagree by a frame of bullet travel exactly when that matters. */
static int fixed_pose(struct FixedPose* h, uintptr_t key, uintptr_t script,
                      uint64_t tick, int age, const float pos[3], double alpha, int predict,
                      float out[3]) {
    h->rolled=0;
    int finite=1;
    for (int i=0;i<3;++i) finite &= isfinite(pos[i]);
    if (!finite) {h->valid=0;return 0;}
    if (h->key!=key || h->script!=script || tick>h->tick+1 || tick<h->tick || age<h->age) {
        h->key=key; h->script=script; h->tick=tick; h->age=age; h->valid=0;
        memcpy(h->current,pos,sizeof h->current);
        memcpy(h->previous,pos,sizeof h->previous);
    } else if (tick!=h->tick) {
        h->rolled=1;
        float distance=0;
        for (int i=0;i<3;++i) {float d=pos[i]-h->current[i];distance+=d*d;}
        memcpy(h->previous,h->current,sizeof h->previous);
        memcpy(h->current,pos,sizeof h->current);
        h->tick=tick; h->age=age; h->valid=distance<64.0f*64.0f;
    } else if (memcmp(h->current,pos,sizeof h->current)) {
        /* One VM reused for multiple sprites, or positions changed by a draw callback. */
        h->valid=0;
        memcpy(h->current,pos,sizeof h->current);
    }
    if (!h->valid) return 0;
    const float* from=predict?h->current:h->previous;
    for (int i=0;i<3;++i) out[i]=from[i]+(h->current[i]-h->previous[i])*(float)alpha;
    return memcmp(out,pos,sizeof h->current)!=0;
}
/* Rotation and scale, smoothed on the decision fixed_pose has already made for this VM.
   Menus and HUDs animate by spinning and scaling at least as much as by moving, so
   position alone leaves them looking like the 60 Hz they are. Call straight after
   fixed_pose, which records in the history whether this call advanced a tick. */
static int fixed_pose_extra(struct FixedPose* h, const float turn[3], const float size[2],
                            double alpha, int predict, float out_turn[3], float out_size[2]) {
    if (h->rolled) {   /* consume it, so calling twice for one tick cannot roll twice */
        h->rolled=0;
        memcpy(h->turn_previous,h->turn_current,sizeof h->turn_previous);
        memcpy(h->size_previous,h->size_current,sizeof h->size_previous);
    }
    memcpy(h->turn_current,turn,sizeof h->turn_current);
    memcpy(h->size_current,size,sizeof h->size_current);
    if (!h->valid) return 0;
    for (int i=0;i<3;++i)
        out_turn[i]=fixed_turn(h->turn_previous[i],h->turn_current[i],alpha,predict);
    for (int i=0;i<2;++i) {
        const float from=predict?h->size_current[i]:h->size_previous[i];
        out_size[i]=from+(h->size_current[i]-h->size_previous[i])*(float)alpha;
    }
    return memcmp(out_turn,turn,sizeof h->turn_current)!=0 ||
           memcmp(out_size,size,sizeof h->size_current)!=0;
}
