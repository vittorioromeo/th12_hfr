/* Research probe, not a game or collision test.
 *
 * Executes the production x86 scheduler and models the New Classic projectile
 * call order in hfr64.c:update_first using the production clock/slice helpers.
 * The model assumes an armed projectile callback, ideal presentation intervals,
 * and no pauses, scene changes, stalls or fast-forward. Re-audit that call order
 * if update_first changes. It does not execute the game's projectile callback.
 *
 * See docs/FIXED_STEP_RESEARCH.md for interpretation and build instructions.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static struct { int substep; } cfg;
static unsigned g_class_count = 1;
#define LOG(...) ((void)0)
#include "../../src/core/timing.c"
static double now_s(void) { return 0; }
#include "../../src/backends/fixed_clock.h"
#include "../../src/backends/subtick.h"

int main(void) {
    const int rates[] = {60, 120, 144, 165, 240, 360, 480, 960};
    puts("rate,x86_min_step_ms,x86_max_step_ms,x86_majors,x86_frames,"
         "nc_min_slice_ms,nc_max_slice_ms,nc_frames");
    for (unsigned r = 0; r < sizeof rates / sizeof *rates; ++r) {
        int rate = rates[r];
        cfg.substep = 1;
        set_logic_rate(rate);
        double lo = 1e9, hi = 0, sum = 0;
        unsigned majors = 0;
        for (int i = 0; i < rate; ++i) {
            advance_tick();
            double dt = g_dt / 60.0 * 1000;
            if (dt < lo) lo = dt;
            if (dt > hi) hi = dt;
            sum += g_dt;
            majors += g_major;
        }

        struct FixedClock clock = {0};
        struct SubtickPlayer slice = {0};
        unsigned ticks = 0;
        int armed = 0;
        double nlo = 1e9, nhi = 0, nsum = 0;
        /* Integer-valued QPC units: each presentation advances 60 units and
         * each native frame spans `rate` units. Include the final boundary
         * to finish precisely one second of projectile motion. */
        for (int i = 0; i <= rate; ++i) {
            double phase;
            int major = fixed_clock_step(&clock, 1000.0 + 60.0 * i,
                                         60.0 * rate, rate, &phase);
            double tau = major ? (double)ticks + 1.0 : ticks + phase;
            double dt = subtick_slice(&slice, tau, armed);
            if (major) {
                ++ticks;
                armed = 1;
            }
            if (dt > 0) {
                nsum += dt;
                dt = dt / 60.0 * 1000;
                if (dt < nlo) nlo = dt;
                if (dt > nhi) nhi = dt;
            }
        }
        printf("%d,%.9f,%.9f,%u,%.9f,%.9f,%.9f,%.9f\n",
               rate, lo, hi, majors, sum, nlo, nhi, nsum);
        if (majors != 60 || sum != 60.0 || fabs(nsum - 60.0) > 1e-9) {
            fprintf(stderr, "Unexpected time accounting at %d Hz\n", rate);
            return 1;
        }
    }
    return 0;
}
