/* ------------------------------------------------------------------ frame hook */

static int __stdcall hfr_frame(void* ctx) {
    double now = now_s();
    replay_check();
    if (g_t0 == 0) { g_t0 = now; g_ticks_run = 0; }
    /* how many ticks we should have run by now (long-term schedule) minus how many we did */
    double expected = (now - g_t0) * (double)g_logic_rate;
    long long deficit = (long long)floor(expected) - g_ticks_run;
    if (deficit > 60 || deficit < -60) {              /* stall / clock jump: re-anchor instead of catching up */
        g_t0 = now - (double)g_ticks_run / (double)g_logic_rate; deficit = 0;
    }
    if (!(cfg.vsync && g_vsync_effective)) {
        /* Pace presentation independently: stock replays still present at HFR. */
        if (g_next == 0 || fabs(now - g_next) > 0.25) g_next = now;
        double due = g_next;
        while (now < due) {
            double rem = due - now;
            if (rem > 0.0025) Sleep(1); else if (rem > 0.0008) Sleep(0); else YieldProcessor();
            now = now_s();
        }
        expected = (now - g_t0) * (double)g_logic_rate;
        deficit = (long long)floor(expected) - g_ticks_run;
        g_next = due + 1.0 / (double)g_refresh;
    }
    limiter_stats(now);
    int n = ticks_for_slot();
    if (deficit > 6) { n += 1; g_stat_catchup++; }         /* presentation is slower than the display rate: catch up (6 = hysteresis for the DWM present queue) */
    else if (deficit < -6 && n > 0) { n -= 1; g_stat_skipped++; } /* presentation is faster: hold a tick back */
    for (int i = 0; i + 1 < n; i++) { advance_tick(); int r = update_only_tick(); if (r) return r; }
    if (n >= 1) { advance_tick(); g_skip_update = 0; } else g_skip_update = 1;
    g_ticks_run += n;
    int r = orig_frame_vsync(ctx);
    g_skip_update = 0;
    return r;
}
