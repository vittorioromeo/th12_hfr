/* ------------------------------------------------------------------ frame hook */

/* The work that belongs to the window and the menu rather than to the simulation. It has to
   happen once a frame in every configuration, including a game whose per-frame function we do
   not know -- so when there is no frame hook the Present hook calls it instead. Returns
   non-zero when the frame should be abandoned. */
static int hfr_housekeeping(void) {
    conflict_check_late();
    hfr_ui_apply_pending(g_dev);                      /* menu changes that touch D3D or the schedule */
    return window_pump(g_dev);                        /* minimised, or resizing the swap chain */
}

static int ticks_sliced(void) { return cfg.substep && g_logic_rate != 60; }
/* A replay's own fast-forward, while the ticks are sliced. The engines fast-forward a replay
   by answering "run the list again" from a node near the end of it: another whole frame inside
   the same pass. Obeyed inside a tick that is a fraction of a frame, that gives the stepped
   systems the fraction for a whole frame and reads the per-tick input once for several frames,
   and the playback parts from the recording. So the runner counts the request instead
   (g_ff_frames), and here, after the presentation, each frame asked for is run as the ticks
   the schedule would have run for it -- the rest of the frame in progress, then the next one
   whole -- so the sequence of ticks is the one a normal-speed playback goes through. A frame
   run here can ask for another, as it can in the game; sixteen a presentation is enough for
   the fastest native fast-forward (8x). Returns what a catch-up tick returned when the game
   asked to leave. */
static int run_ff_frames(void) {
    unsigned done = 0;
    while (g_ff_frames && done < 16) {
        --g_ff_frames; ++done;
        int majors = 0;
        for (int guard = 0; guard < 4096; ++guard) {
            int next_major = !ticks_sliced() || g_tick == 0 || (g_units_total / UNITS_PER_FRAME) != g_prev_frame;
            if (next_major && majors) break;
            advance_tick(); g_skip_update = 0;
            majors += g_major;
            int r = update_only_tick();
            if (r) { g_ff_frames = 0; return r; }
        }
    }
    g_ff_frames = 0;
    static unsigned logged;
    if (done && logged < 4 && ++logged) LOG("replay fast-forward: %u extra frame(s) run as whole tick sequences", done);
    return 0;
}
static int __stdcall hfr_frame(void* ctx);
/* The same hook for a game whose frame callbacks are thiscall: the context arrives in ECX and
   the callee pops nothing, which is what fastcall with one argument compiles to. */
static int __attribute__((fastcall)) hfr_frame_ecx(void* ctx) { return hfr_frame(ctx); }
static int __stdcall hfr_frame(void* ctx) {
    double now = now_s();
    if (hfr_housekeeping()) { Sleep(1); return 0; }
    replay_check();
    if (g_t0 == 0) { g_t0 = now; g_ticks_run = 0; }
    /* how many ticks we should have run by now (long-term schedule) minus how many we did;
       at a game speed other than 100% the schedule's clock runs that much faster or slower */
    const double tick_rate = (double)g_logic_rate * (double)g_speed_pct / 100.0;
    double expected = (now - g_t0) * tick_rate;
    long long deficit = (long long)floor(expected) - g_ticks_run;
    /* stall / clock jump: re-anchor instead of catching up. It is also the ceiling on fast
       forward: a machine that cannot run the ticks asked for falls behind by more than this
       and is re-anchored, so it runs as fast as it can rather than ever further behind. */
    const long long slack = 60 * (g_speed_pct > 100 ? g_speed_pct / 100 : 1);
    if (deficit > slack || deficit < -slack) {
        g_t0 = now - (double)g_ticks_run / tick_rate; deficit = 0;
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
        expected = (now - g_t0) * tick_rate;
        deficit = (long long)floor(expected) - g_ticks_run;
        g_next = due + 1.0 / (double)g_refresh;
    }
    limiter_stats(now);
    int n = ticks_for_slot();
    /* Never ask for more than one tick a frame when the catch-up tick cannot be made: the
       loop below would call it, it would decline, and the schedule would think it had caught
       up when it had not. */
    if (!catchup_available() && n > 1) n = 1;
    if (deficit > 6) { n += 1; g_stat_catchup++; }         /* presentation is slower than the display rate: catch up (6 = hysteresis for the DWM present queue) */
    else if (deficit < -6 && n > 0) { n -= 1; g_stat_skipped++; } /* presentation is faster: hold a tick back */
    if (!catchup_available() && n > 1) n = 1;
    for (int i = 0; i + 1 < n; i++) { advance_tick(); int r = update_only_tick(); if (r) return r; }
    if (n >= 1) { advance_tick(); g_skip_update = 0; } else g_skip_update = 1;
    g_ticks_run += n;
    if (n && g_speed_pct != 100 && !g_replay_playing && g_stream_stage >= 0 && g_stream_stage < HFR_STAGES) g_speed_stages |= 1u << g_stream_stage;
    /* Where a frame's time goes: inside the game's frame function (its draw and the present,
       vsync included) or outside it (its loop, its own waits). The stats line reports both. */
    if (g_frame_out_at > 0) { double outside = now - g_frame_out_at; if (outside > g_gap_outside_max) g_gap_outside_max = outside; if (outside > 0.008) g_gap_outside_long++; }
    double t_in = now_s(); g_t_first_draw = g_t_present_in = g_t_present_out = 0;
    int r = frame_call_original(ctx);
    g_frame_out_at = now_s();
    if (cfg.debug && g_frame_out_at - t_in > 0.012) {   /* a long frame: say where inside it the time went */
        static unsigned logged; if (logged++ < 60)
            LOG("long frame: %.1f ms inside the frame function: before the first draw %.1f, drawing %.1f, in Present %.1f, after %.1f",
                (g_frame_out_at - t_in) * 1000.0, g_t_first_draw ? (g_t_first_draw - t_in) * 1000.0 : -1.0, g_t_first_draw && g_t_present_in ? (g_t_present_in - g_t_first_draw) * 1000.0 : -1.0, g_t_present_in && g_t_present_out ? (g_t_present_out - g_t_present_in) * 1000.0 : -1.0, g_t_present_out ? (g_frame_out_at - g_t_present_out) * 1000.0 : -1.0);
    }
    { double inside = g_frame_out_at - t_in; if (inside > g_gap_inside_max) g_gap_inside_max = inside; if (inside > 0.008) g_gap_inside_long++;
      if (g_t_first_draw > 0 && g_t_present_in > 0 && g_t_present_out > 0) {
          double span[4] = { g_t_first_draw - t_in, g_t_present_in - g_t_first_draw, g_t_present_out - g_t_present_in, g_frame_out_at - g_t_present_out };
          for (int i = 0; i < 4; ++i) if (span[i] > g_span_max[i]) g_span_max[i] = span[i];
      } }
    if (g_ff_frames) { int f = run_ff_frames(); if (f && !r) r = f; }
    g_skip_update = 0;
    return r;
}
