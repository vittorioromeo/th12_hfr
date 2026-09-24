#pragma once
#include <math.h>
struct FixedClock {double next;};
/* One update per outer iteration, with a bounded debt after blocking/loading.
   Actual presentation cadence determines phase, not the requested FPS. */
/* speed_pct: the game speed in percent. The native tick comes every 1/60 s at 100%, every
   1/30 s at 50%; above 100% it is still at most one per outer iteration, so fast forward is
   capped at the presentation rate over 60. */
static int fixed_clock_step_at(struct FixedClock* clock,double now,double frequency,int rate,int speed_pct,double* phase) {
    double step=frequency/(60.0*(speed_pct>0?speed_pct:100)/100.0);
    if (!clock->next || now-clock->next>step*4) clock->next=now;
    int major=(rate==60 && speed_pct==100) || now>=clock->next;
    if (major) clock->next+=step;
    *phase=fmax(0.0,fmin(1.0,(now-(clock->next-step))/step));
    return major;
}
static int fixed_clock_step(struct FixedClock* clock,double now,double frequency,int rate,double* phase) {
    return fixed_clock_step_at(clock,now,frequency,rate,100,phase);
}
