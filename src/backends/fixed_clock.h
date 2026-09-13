#pragma once
#include <math.h>
struct FixedClock {double next;};
/* One update per outer iteration, with a bounded debt after blocking/loading.
   Actual presentation cadence determines phase, not the requested FPS. */
static int fixed_clock_step(struct FixedClock* clock,double now,double frequency,int rate,double* phase) {
    double step=frequency/60.0;
    if (!clock->next || now-clock->next>step*4) clock->next=now;
    int major=rate==60 || now>=clock->next;
    if (major) clock->next+=step;
    *phase=fmax(0.0,fmin(1.0,(now-(clock->next-step))/step));
    return major;
}
