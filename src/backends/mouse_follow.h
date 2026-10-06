#pragma once
#include "subtick.h"

/* The controller emits ordinary native input; it never writes a player position.
   Pick the legal one-frame step that gets closest to the cursor. Including the
   stationary candidate prevents oscillation when the target is between pixels. */
static unsigned mouse_follow_direction(float x, float y, float tx, float ty,
                                      const float straight[2], const float diagonal[2],
                                      const float scale[2], const float bounds[4], int focused) {
    if (!isfinite(x) || !isfinite(y) || !isfinite(tx) || !isfinite(ty) ||
        !isfinite(scale[0]) || !isfinite(scale[1]) ||
        !(straight[focused] > 0) || !isfinite(straight[focused]) ||
        !(diagonal[focused] > 0) || !isfinite(diagonal[focused])) return 0;
    static const unsigned choices[] = {SUBTICK_UP, SUBTICK_DOWN, SUBTICK_LEFT, SUBTICK_RIGHT,
        SUBTICK_UP|SUBTICK_LEFT, SUBTICK_UP|SUBTICK_RIGHT,
        SUBTICK_DOWN|SUBTICK_LEFT, SUBTICK_DOWN|SUBTICK_RIGHT};
    tx=subtick_clamp(tx,bounds[0],bounds[2]); ty=subtick_clamp(ty,bounds[1],bounds[3]);
    float best=(tx-x)*(tx-x)+(ty-y)*(ty-y); unsigned result=0;
    for (unsigned i=0;i<sizeof choices/sizeof *choices;++i) {
        float dx,dy;
        subtick_direction(choices[i]|(focused?SUBTICK_FOCUS:0),straight,diagonal,&dx,&dy);
        float nx=subtick_clamp(x+dx*scale[0],bounds[0],bounds[2]);
        float ny=subtick_clamp(y+dy*scale[1],bounds[1],bounds[3]);
        float d=(tx-nx)*(tx-nx)+(ty-ny)*(ty-ny);
        if (d+0.00001f<best) {best=d;result=choices[i];}
    }
    return result;
}

/* The New Classic sprite renderer scales both axes by render_height / 480,
   after adding its live playfield offset. Convert client pixels into that same
   render surface, allowing for client/backbuffer sizes that differ under DPI. */
static int mouse_follow_target(float cx,float cy,int cw,int ch,int rw,int rh,
                               const float origin[2],const float bounds[4],float* tx,float* ty) {
    if (cw<=0 || ch<=0 || rw<=0 || rh<=0 || cx<0 || cy<0 || cx>=cw || cy>=ch ||
        !isfinite(cx) || !isfinite(cy) || !isfinite(origin[0]) || !isfinite(origin[1])) return 0;
    for (int i=0;i<4;++i) if (!isfinite(bounds[i])) return 0;
    if (!(bounds[2]>0) || !(bounds[3]>0)) return 0;
    *tx=subtick_clamp(cx/cw*rw*480.0f/rh-origin[0],bounds[0],bounds[2]);
    *ty=subtick_clamp(cy/ch*480.0f-origin[1],bounds[1],bounds[3]);
    return 1;
}

struct MouseButtons { unsigned blocked; };
/* A click that dismissed a menu or focused the window must first be released. */
static unsigned mouse_follow_buttons(struct MouseButtons* state,unsigned raw,int active) {
    if (!active) {state->blocked=7;return 0;}
    state->blocked &= raw;
    unsigned allowed=raw & ~state->blocked;
    return ((allowed&1)?0x01:0) | ((allowed&2)?0x04:0) | ((allowed&4)?0x02:0);
}
