static double now_s(void);   /* limiter.c */
/* ------------------------------------------------------------------ tick state */
static int    g_refresh = 60;      /* presents per second (display) */
static int    g_logic_rate = 60;   /* logic ticks per second (== g_refresh normally; a replay's recording rate during playback) */
static int g_replay_rate, g_replay_playing;
static int    g_skip_update = 0;   /* present without running the update list (duplicate frame) */
static unsigned g_lacc = 0;        /* Bresenham remainder: logic ticks per present, in units of g_refresh * 100 */
/* Game speed, in percent: how many logic ticks run per second of real time, relative to the
   logic rate. It changes nothing but that: every tick is the same length and runs the same
   code, so the simulation, the per-tick input and a replay are what they are at 100%; slow
   motion shows each tick for longer and fast forward shows fewer of them. */
#define SPEED_MIN 10
#define SPEED_MAX 1600
static int g_speed_pct = 100;
/* Whole game frames a replay's own fast-forward asked for on this presentation (the list's
   "run again" answer, counted instead of obeyed while the ticks are sliced: frame.c). */
static unsigned g_ff_frames;
/* The ticks are sliced: a tick is a fraction of a game frame, so "run the list again inside
   this tick" would give the stepped systems that fraction for a whole extra frame. */
static int ticks_sliced(void);
static float  g_dt = 1.0f;         /* game frames per tick for SUB nodes */
static unsigned g_tick = 0;        /* sub-tick counter */
static float g_ptf_prev, g_ptf_cur;   /* player state timer (float) before/after the last Player node call */
static float g_move_residual[2];
static unsigned g_stat_tex_done, g_stat_tex_redone; static size_t g_tex_bytes;   /* texscale.c */
static int g_dim_frame_done, g_dim_ingame_frames, g_dim_available, g_dim_trace_frames, g_dim_trace_n;
static unsigned g_frame_draws, g_frame_flushes, g_frame_vms;
static unsigned g_stat_joy_polls; static double g_stat_joy_max;   /* input.c: the joystick thread's real polls */
static double g_frame_out_at, g_gap_inside_max, g_gap_outside_max; static unsigned g_gap_inside_long, g_gap_outside_long;   /* frame.c: time inside/outside the game's frame function */
static unsigned g_gap_hist[7];   /* present gaps over the stats window: <1 1-2 2-4 4-8 8-12 12-20 >20 ms */
/* Where a long frame's inside time went (frame.c, scaler.c, d3d9.c): first draw call, Present entry and exit, per frame */
static double g_t_first_draw, g_t_present_in, g_t_present_out;
static double g_span_max[4];   /* longest, over the stats window, of: before the first draw, drawing, in Present, after Present */   /* this frame's draw calls, our batch flushes, sprite VM draws (hitch log) */   /* dimming.c: the background quad has been drawn this frame */
static double g_t0 = 0;            /* wall-clock origin of the tick schedule */
static unsigned g_stat_skipped;
static unsigned g_last_units = 0;  /* units of the previous tick */
static unsigned g_prev_frame = 0;  /* frame index (within the 60-frame cycle) of the previous tick */
static int    g_major = 1;         /* this tick is a frame boundary */
static double g_phase = 0;         /* position of this tick inside the current frame, [0,1) */
static float  g_logical = 1.0f;    /* the game's own notion of game speed (1.0, or ECL slow-mo) */
static float  g_factor = 1.0f;     /* factor of the node currently running (1 or dt) */
static float  g_pause_shadow = 1.0f;

/* Sub-step lengths are chosen as multiples of 1/256 frame (dyadic, exact in float32), distributed with a
   Bresenham sequence so that exactly 60 frames elapse every R ticks. The engine's timers accumulate the
   sub-step in float and compare with integer frame counts; with dyadic steps every partial sum is exact,
   so timer events happen on the right tick with no drift (a constant 60/R would not be representable). */
#define UNITS_PER_FRAME 256
static unsigned g_units_acc;   /* Bresenham remainder */
static unsigned g_units_total; /* units elapsed at the start of the current tick (mod 60 frames) */
static void set_game_speed(int pct) {
    if (pct < SPEED_MIN) pct = SPEED_MIN;
    if (pct > SPEED_MAX) pct = SPEED_MAX;
    if (pct == g_speed_pct) return;
    g_speed_pct = pct;
    g_t0 = 0; g_lacc = 0;              /* the long-term schedule starts again from now */
    LOG("game speed: %d%%", pct);
}
/* The fraction of a presentation slot the schedule has accumulated towards the next tick. */
static double schedule_fraction(void) { return (double)g_lacc / ((double)(g_refresh > 0 ? g_refresh : 60) * 100.0); }
static void set_logic_rate(int rate) {
    if (!cfg.substep) rate = 60;
    if (rate < 60) rate = 60;
    if (rate > 1000) rate = 1000;
    g_logic_rate = rate;
    memset(g_move_residual, 0, sizeof g_move_residual);
    g_dt = cfg.substep ? 60.0f / (float)rate : 1.0f;
    g_tick = 0; g_units_acc = 0; g_units_total = 0; g_major = 1; g_phase = 0; g_lacc = 0; g_prev_frame = 0;
    g_t0 = 0;
    LOG("logic rate: %d ticks/s (nominal dt=%.6f frames/tick), present rate %d Hz, substep=%d", g_logic_rate, g_dt, g_refresh, cfg.substep);
}
static void recompute_rate(int refresh) {
    if (refresh < 60) refresh = 60;
    if (refresh > 1000) refresh = 1000;
    g_refresh = refresh;
    /* No classified systems, no sub-stepping, whatever the setting says: leaving 60 would
       make the runner take minor ticks that skip every node, and the game would draw from
       state its own update never ran. */
    int substep = cfg.substep && g_class_count;
    int want = substep ? (g_replay_playing ? (g_replay_rate ? g_replay_rate : 60) : refresh) : 60;
    if (want != g_logic_rate || g_tick == 0) set_logic_rate(want);
    else g_lacc = 0;
}
/* number of logic ticks to run for the next present slot */
static int ticks_for_slot(void) {
    unsigned slot = (unsigned)g_refresh * 100u;
    g_lacc += (unsigned)g_logic_rate * (unsigned)g_speed_pct;
    int n = (int)(g_lacc / slot);
    g_lacc -= (unsigned)n * slot;
    return n;
}
static void advance_tick(void) {
    if (!cfg.substep || g_logic_rate == 60) { g_major = 1; g_dt = 1.0f; g_phase = 0; g_tick++; return; }
    /* this tick starts at g_units_total; it is a frame boundary tick if the previous tick started in an earlier frame */
    unsigned cur_frame = g_units_total / UNITS_PER_FRAME;
    g_major = (g_tick == 0) || (cur_frame != g_prev_frame);
    g_prev_frame = cur_frame;
    g_phase = (double)(g_units_total % UNITS_PER_FRAME) / UNITS_PER_FRAME;
    /* Bresenham step for this tick */
    g_units_acc += 60u * UNITS_PER_FRAME;
    unsigned u = g_units_acc / (unsigned)g_logic_rate;
    g_units_acc -= u * (unsigned)g_logic_rate;
    g_dt = (float)u / (float)UNITS_PER_FRAME;
    g_last_units = u;
    g_units_total += u;
    if (g_units_total >= 60u * UNITS_PER_FRAME) g_units_total -= 60u * UNITS_PER_FRAME; /* every R ticks (1 s) exactly */
    g_tick++;
}
/* Restart the sub-step sequence so that the current (frame boundary) tick is the first tick of the canonical
   sequence for this rate. Used at the first frame of every stage: the sub-step pattern within a frame then only
   depends on the frame number, which makes a recording and its playback run the same sequence of steps
   (at non-integer ratios such as 144/60 the pattern otherwise depends on when the game was started). */
static void schedule_reset_here(void) {
    memset(g_move_residual, 0, sizeof g_move_residual);
    g_ptf_prev = g_ptf_cur = 0;
    if (!cfg.substep || g_logic_rate == 60) return;
    g_units_acc = 60u * UNITS_PER_FRAME;
    unsigned u = g_units_acc / (unsigned)g_logic_rate;
    g_units_acc -= u * (unsigned)g_logic_rate;
    g_units_total = u; g_last_units = u; g_prev_frame = 0; g_phase = 0;
    g_dt = (float)u / (float)UNITS_PER_FRAME;
}
/* Put the schedule where the canonical sequence has it on the frame tick of frame f of a stage
   (frame 0 being the one schedule_reset_here starts). The sequence repeats every 60 frames,
   so this walks at most one period. Unlike the reset it leaves the movement residuals alone:
   it is used in the middle of a stage (update_runner.c, after a pause). */
static void schedule_to_frame(unsigned f) {
    if (!cfg.substep || g_logic_rate == 60) return;
    float residual[2]; memcpy(residual, g_move_residual, sizeof residual);
    float ptf_prev = g_ptf_prev, ptf_cur = g_ptf_cur;
    unsigned tick = g_tick;
    schedule_reset_here();
    memcpy(g_move_residual, residual, sizeof residual); g_ptf_prev = ptf_prev; g_ptf_cur = ptf_cur;
    g_tick = tick ? tick : 1; g_major = 1;
    f %= 60;
    for (unsigned k = 0; f && k < 60000; ++k) {
        advance_tick();
        if (g_major && g_prev_frame == f) break;
    }
}
