/* ------------------------------------------------------------------ frame limiter */
static LARGE_INTEGER g_qpf;
static double now_s(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart / (double)g_qpf.QuadPart; }
static double g_next = 0;
static double g_stat_last = 0; static unsigned g_stat_ticks = 0;

static int g_vsync_effective = 1;   /* vsync appears to pace us: no sleeping in the limiter */
static double g_rate_win_start = 0; static unsigned g_rate_win_ticks = 0;
static unsigned g_stat_catchup; static long long g_stat_ticks_run_last; static long long g_ticks_run;

#define G_FRAME_FLAG34   (*(uint32_t*)g_game->addr.frame_context_ptr)
#define G_FRAME_FLAG38   (*(uint32_t*)g_game->addr.frame_flag)
static void call_with_esi(uintptr_t fn, uintptr_t esi) {
    uintptr_t s = esi;
    __asm__ volatile ("call *%1" : "+S"(s) : "r"(fn) : "eax", "ecx", "edx", "memory", "cc");
}
/* an update pass without drawing/presenting (used to catch up after a missed vblank) */
static int update_only_tick(void) {
    G_FRAME_FLAG34 = g_game->addr.frame_context_value;
    G_FRAME_FLAG38 = 1;
    int r = hfr_runner(G_UPDATE_RUNNER);
    if (r == 0)  { call_with_esi(g_game->addr.cleanup_fn, g_game->addr.cleanup_this); return 1; }
    if (r == -1) { call_with_esi(g_game->addr.cleanup_fn, g_game->addr.cleanup_this); return 2; }
    return 0;
}

static void limiter_stats(double now) {
    g_stat_ticks++;
    { static double last = 0; double period = 1.0 / (double)g_refresh; if (last > 0) { double gap = now - last; if (gap > 1.5 * period) g_stat_long++; if (gap > 3 * period) g_stat_vlong++; } last = now; }
    g_rate_win_ticks++;
    if (g_rate_win_start == 0) g_rate_win_start = now;
    else if (now - g_rate_win_start >= 1.0) {
        double rate = g_rate_win_ticks / (now - g_rate_win_start);
        if (cfg.vsync && g_vsync_effective && rate > g_refresh * 1.08) { g_vsync_effective = 0; LOG("vsync does not seem to pace presentation (%.1f/s) — using software limiter", rate); }
        g_rate_win_start = now; g_rate_win_ticks = 0;
    }
    if (now - g_stat_last >= 5.0) {
        if (g_stat_last > 0)
            LOG("stats: %.2f presents/s (target %d), extra ticks %u, skipped ticks %u, sub calls %u, frame calls %u, logical %.3f, long gaps %u/%u, ticks/s %.2f, subtick polls %u applied %u",
                g_stat_ticks / (now - g_stat_last), g_refresh, g_stat_catchup, g_stat_skipped, g_stat_sub_calls, g_stat_frame_calls, g_logical, g_stat_long, g_stat_vlong,
                (double)(g_ticks_run - g_stat_ticks_run_last) / (now - g_stat_last), g_stat_subtick_polls, g_stat_subtick_applied);
        g_stat_last = now; g_stat_ticks = 0; g_stat_sub_calls = g_stat_frame_calls = g_stat_long = g_stat_vlong = g_stat_catchup = g_stat_skipped = 0; g_stat_ticks_run_last = g_ticks_run;
        g_stat_subtick_polls = g_stat_subtick_applied = 0;
        if (cfg.debug) {
            uint8_t* rm=G_REPLAY_MANAGER;
            if (rm) LOG("state: stage=%d replay_frame=%d replay_mode=%d input=%08x",
                *(int*)(rm+g_game->layout.replay_stage),*(int*)(rm+g_game->layout.replay_frame),*(int*)(rm+0x10),(unsigned)G_GAME_INPUT);
        }
    }
}
