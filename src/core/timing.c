/* ------------------------------------------------------------------ tick state */
static int    g_refresh = 60;      /* presents per second (display) */
static int    g_logic_rate = 60;   /* logic ticks per second (== g_refresh normally; a replay's recording rate during playback) */
static int g_replay_rate, g_replay_playing;
static int    g_skip_update = 0;   /* present without running the update list (duplicate frame) */
static unsigned g_lacc = 0;        /* Bresenham remainder: logic ticks per present */
static float  g_dt = 1.0f;         /* game frames per tick for SUB nodes */
static unsigned g_tick = 0;        /* sub-tick counter */
static float g_ptf_prev, g_ptf_cur;   /* player state timer (float) before/after the last Player node call */
static float g_move_residual[2];
static unsigned g_stat_tex_done, g_stat_tex_redone; static size_t g_tex_bytes;   /* texscale.c */
static int g_dim_frame_done, g_dim_ingame_frames, g_dim_available, g_dim_trace_frames, g_dim_trace_n;   /* dimming.c: the background quad has been drawn this frame */
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
    int want = cfg.substep ? (g_replay_playing ? (g_replay_rate ? g_replay_rate : 60) : refresh) : 60;
    if (want != g_logic_rate || g_tick == 0) set_logic_rate(want);
    else g_lacc = 0;
}
/* number of logic ticks to run for the next present slot */
static int ticks_for_slot(void) {
    g_lacc += (unsigned)g_logic_rate;
    int n = (int)(g_lacc / (unsigned)g_refresh);
    g_lacc -= (unsigned)n * (unsigned)g_refresh;
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
