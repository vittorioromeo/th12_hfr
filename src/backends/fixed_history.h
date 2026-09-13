#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
/* Renderer-independent pose history. A gap, script restart, or teleport snaps. */
struct FixedPose {
    uintptr_t key, script;
    uint64_t tick;
    int age, valid;
    float previous[3], current[3];
};
/* alpha runs from the previous native position to the current one. `predict` continues past
   the current one instead, which is what the sub-tick pass needs: once the player is advancing
   between native ticks, everything drawn beside it has to be where it is now, not where it was
   a frame ago, or the two disagree by a frame of bullet travel exactly when that matters. */
static int fixed_pose(struct FixedPose* h, uintptr_t key, uintptr_t script,
                      uint64_t tick, int age, const float pos[3], double alpha, int predict,
                      float out[3]) {
    int finite=1;
    for (int i=0;i<3;++i) finite &= isfinite(pos[i]);
    if (!finite) {h->valid=0;return 0;}
    if (h->key!=key || h->script!=script || tick>h->tick+1 || tick<h->tick || age<h->age) {
        h->key=key; h->script=script; h->tick=tick; h->age=age; h->valid=0;
        memcpy(h->current,pos,sizeof h->current);
        memcpy(h->previous,pos,sizeof h->previous);
    } else if (tick!=h->tick) {
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
