/* ------------------------------------------------------------------ frame limiter */
static LARGE_INTEGER g_qpf;
static double now_s(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart / (double)g_qpf.QuadPart; }
/* What the clock this file measures with is actually worth on this machine: the counter's
   frequency, the smallest step it was seen to take between back-to-back reads, and the
   kernel timer resolution in force (whether timeBeginPeriod(1) took). A log full of
   multiples of 15.6 ms is a coarse clock, not a stuttering game. */
/* Who else is using the CPU in this process, and on which cores it is allowed to run. A game
   that pins itself to one core (old games did, for the counter's sake) and runs a busy sound
   thread beside the main one shows up here as a thread eating most of a core. Logged for the
   first few stats windows only. */
static HWND g_wnd;   /* window.c */
/* The window and the monitors: a window that straddles onto a slower monitor is synchronised
   to that monitor by the compositor. */
static BOOL CALLBACK monitor_line(HMONITOR m, HDC dc, LPRECT r, LPARAM p) {
    (void)dc; (void)r; char* line = (char*)p; size_t n = strlen(line);
    MONITORINFOEXA mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi; if (!GetMonitorInfoA(m, (MONITORINFO*)&mi)) return TRUE;
    DEVMODEA dm; memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm; int hz = EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) ? (int)dm.dmDisplayFrequency : 0;
    HWND w = g_wnd; HMONITOR wm = w ? MonitorFromWindow(w, MONITOR_DEFAULTTONULL) : NULL;
    snprintf(line + n, 600 - n, " %s %ld,%ld %ldx%ld %d Hz%s%s;", mi.szDevice, (long)mi.rcMonitor.left, (long)mi.rcMonitor.top, (long)(mi.rcMonitor.right - mi.rcMonitor.left), (long)(mi.rcMonitor.bottom - mi.rcMonitor.top), hz, (mi.dwFlags & MONITORINFOF_PRIMARY) ? " primary" : "", wm == m ? " (the window's)" : "");
    return TRUE;
}
static void monitor_probe(void) {
    char line[700]; RECT r = {0,0,0,0}; if (g_wnd) GetWindowRect(g_wnd, &r);
    LONG st = g_wnd ? GetWindowLongA(g_wnd, GWL_STYLE) : 0, ex = g_wnd ? GetWindowLongA(g_wnd, GWL_EXSTYLE) : 0; ULONG cls = g_wnd ? (ULONG)GetClassLongA(g_wnd, GCL_STYLE) : 0;
    BOOL cloaked = 0, ncr = 0; DWORD cl = 0; if (g_wnd) { DwmGetWindowAttribute(g_wnd, DWMWA_CLOAKED, &cl, sizeof cl); DwmGetWindowAttribute(g_wnd, DWMWA_NCRENDERING_ENABLED, &ncr, sizeof ncr); cloaked = cl != 0; }
    HRGN rgn = CreateRectRgn(0, 0, 0, 0); int has_rgn = g_wnd ? GetWindowRgn(g_wnd, rgn) : 0; DeleteObject(rgn);
    snprintf(line, sizeof line, "monitors: window at %ld,%ld %ldx%ld, style %08lx ex %08lx class %08lx%s%s%s;", (long)r.left, (long)r.top, (long)(r.right - r.left), (long)(r.bottom - r.top), (unsigned long)st, (unsigned long)ex, (unsigned long)cls, cloaked ? " cloaked" : "", ncr ? " nc-rendered" : "", has_rgn && has_rgn != ERROR ? " has-region" : "");
    EnumDisplayMonitors(NULL, NULL, monitor_line, (LPARAM)line);
    LOG("%s", line);
}
/* Compatibility shims change how a window is composed; they show as the __COMPAT_LAYER
   variable, the shim engine's modules, and a version the game is lied to about. */
static void compat_probe(void) {
    char layer[256] = "(none)"; GetEnvironmentVariableA("__COMPAT_LAYER", layer, sizeof layer);
    OSVERSIONINFOA v; memset(&v, 0, sizeof v); v.dwOSVersionInfoSize = sizeof v;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    GetVersionExA(&v);
#pragma GCC diagnostic pop
    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOA*); OSVERSIONINFOA r; memset(&r, 0, sizeof r); r.dwOSVersionInfoSize = sizeof r;
    HMODULE nt = GetModuleHandleA("ntdll.dll"); RtlGetVersionFn rgv = nt ? (RtlGetVersionFn)(void*)GetProcAddress(nt, "RtlGetVersion") : NULL; if (rgv) rgv(&r);
    LOG("process: compat layer %s; version seen %lu.%lu.%lu, real %lu.%lu.%lu", layer, v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber, r.dwMajorVersion, r.dwMinorVersion, r.dwBuildNumber);
    /* every module not from the Windows directory: the game's, ours, the driver's, and anything injected */
    char windir[MAX_PATH] = ""; GetWindowsDirectoryA(windir, sizeof windir); size_t wl = strlen(windir);
    HMODULE hs[512]; DWORD need = 0; char line[1500] = "modules:"; size_t n = strlen(line);
    if (EnumProcessModules(GetCurrentProcess(), hs, sizeof hs, &need)) {
        for (DWORD i = 0; i < need / sizeof *hs && n < sizeof line - 80; ++i) {
            char path[MAX_PATH] = ""; if (!GetModuleFileNameA(hs[i], path, sizeof path)) continue;
            if (wl && _strnicmp(path, windir, wl) == 0) continue;
            const char* b = strrchr(path, '\\'); b = b ? b + 1 : path;
            n += snprintf(line + n, sizeof line - n, " %s", b);
        }
    }
    LOG("%s", line);
}
/* The window's standing with the system: foreground or not, and the process's power
   throttling (EcoQoS), which Windows 11 uses to slow processes it does not consider active.
   The throttle is switched off here as well, since a game is never a background job. */
static void focus_probe(void) {
    HWND fg = GetForegroundWindow(); DWORD fgpid = 0; if (fg) GetWindowThreadProcessId(fg, &fgpid);
    char cls[64] = "", title[64] = ""; if (fg) { GetClassNameA(fg, cls, sizeof cls); GetWindowTextA(fg, title, sizeof title); }
    typedef BOOL (WINAPI *GetPI)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD); typedef BOOL (WINAPI *SetPI)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
    HMODULE k = GetModuleHandleA("kernel32.dll"); GetPI gpi = k ? (GetPI)(void*)GetProcAddress(k, "GetProcessInformation") : NULL; SetPI spi = k ? (SetPI)(void*)GetProcAddress(k, "SetProcessInformation") : NULL;
    PROCESS_POWER_THROTTLING_STATE st; memset(&st, 0, sizeof st); st.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    char thr[120] = "unknown"; if (gpi && gpi(GetCurrentProcess(), ProcessPowerThrottling, &st, sizeof st)) snprintf(thr, sizeof thr, "control %lx state %lx", (unsigned long)st.ControlMask, (unsigned long)st.StateMask);
    static int done; int set = 0;
    if (!done && spi) { done = 1; PROCESS_POWER_THROTTLING_STATE off; memset(&off, 0, sizeof off); off.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION; off.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION; off.StateMask = 0; set = spi(GetCurrentProcess(), ProcessPowerThrottling, &off, sizeof off) ? 1 : -1; }
    LOG("focus: foreground window %p%s (pid %lu, class \"%s\", title \"%s\"), ours %p active %p; power throttling %s%s", (void*)fg, fg == g_wnd ? " (ours)" : "", (unsigned long)fgpid, cls, title, (void*)g_wnd, (void*)GetActiveWindow(), thr, set == 1 ? "; throttling switched off" : set == -1 ? "; could not switch throttling off" : "");
}
static void thread_probe(double window) {
    static int windows; if (windows++ >= 4) return;
    if (windows == 1) compat_probe();
    focus_probe();
    if (windows == 1 || windows == 4) monitor_probe();
    DWORD_PTR pmask = 0, smask = 0; GetProcessAffinityMask(GetCurrentProcess(), &pmask, &smask);
    DWORD_PTR tmask = SetThreadAffinityMask(GetCurrentThread(), pmask); if (tmask) SetThreadAffinityMask(GetCurrentThread(), tmask);
    char line[600]; int n = snprintf(line, sizeof line, "threads: process affinity %08lx of %08lx, this thread %08lx priority %d, class %lu;", (unsigned long)pmask, (unsigned long)smask, (unsigned long)tmask, GetThreadPriority(GetCurrentThread()), (unsigned long)GetPriorityClass(GetCurrentProcess()));
    static struct { DWORD tid; ULONGLONG cpu; } seen[64]; static int nseen;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te; te.dwSize = sizeof te; DWORD pid = GetCurrentProcessId();
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID); if (!h) continue;
            FILETIME c, e, k, u; ULONGLONG cpu = 0; if (GetThreadTimes(h, &c, &e, &k, &u)) cpu = (((ULONGLONG)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((ULONGLONG)u.dwHighDateTime << 32) | u.dwLowDateTime);
            int prio = GetThreadPriority(h); CloseHandle(h);
            int i; for (i = 0; i < nseen && seen[i].tid != te.th32ThreadID; ++i);
            ULONGLONG prev = i < nseen ? seen[i].cpu : cpu;
            if (i == nseen && nseen < 64) { seen[nseen].tid = te.th32ThreadID; seen[nseen].cpu = cpu; nseen++; } else if (i < nseen) seen[i].cpu = cpu;
            double pct = window > 0 ? (double)(cpu - prev) / 1e7 / window * 100.0 : 0;
            if (n < (int)sizeof line - 40 && (pct >= 1.0 || te.th32ThreadID == GetCurrentThreadId())) n += snprintf(line + n, sizeof line - n, " %lu%s prio %d %.0f%%", (unsigned long)te.th32ThreadID, te.th32ThreadID == GetCurrentThreadId() ? "(main)" : "", prio, pct);
        }
        CloseHandle(snap);
    }
    LOG("%s", line);
}
/* What the compositor and the swap chain say reached the screen: DWM's composition rate and
   frame counts over the window, and the chain's own present statistics (presents versus
   the refreshes they were shown on). Presents that DWM never composed are dropped frames. */
static IDirect3DSwapChain9* g_swap; static int g_own_present;   /* scaler.c */
static const GUID g_iid_swapchain9ex = { 0x91886caf, 0x1c3d, 0x4d2e, { 0xa0, 0xab, 0x3e, 0x4c, 0x7d, 0x8d, 0x33, 0x03 } };
static void present_probe(double window) {
    static int windows; if (windows++ >= 12) return;
    char line[400]; int n = snprintf(line, sizeof line, "screen:");
    DWM_TIMING_INFO ti; memset(&ti, 0, sizeof ti); ti.cbSize = sizeof ti;
    static ULONGLONG last_frames, last_dropped, last_refresh; static int have;
    if (SUCCEEDED(DwmGetCompositionTimingInfo(NULL, &ti))) {
        double rate = ti.rateCompose.uiDenominator ? (double)ti.rateCompose.uiNumerator / ti.rateCompose.uiDenominator : 0;
        if (have) n += snprintf(line + n, sizeof line - n, " DWM composes at %.1f Hz, %.0f compositions/s, %llu frames missed, %llu refreshes/s in the window;", rate, (double)(ti.cFrame - last_frames) / window, (unsigned long long)(ti.cFramesDropped - last_dropped), (unsigned long long)((ti.cRefresh - last_refresh) / (ULONGLONG)(window > 0 ? window : 1)));
        last_frames = ti.cFrame; last_dropped = ti.cFramesDropped; last_refresh = ti.cRefresh; have = 1;
    } else n += snprintf(line + n, sizeof line - n, " no DWM timing;");
    if (g_own_present && g_swap) {
        IDirect3DSwapChain9Ex* ex = NULL;
        if (SUCCEEDED(g_swap->lpVtbl->QueryInterface(g_swap, &g_iid_swapchain9ex, (void**)&ex)) && ex) {
            D3DPRESENTSTATS ps; static UINT lp, lpr, lsr; static int have2;
            if (SUCCEEDED(ex->lpVtbl->GetPresentStats(ex, &ps))) {
                if (have2) n += snprintf(line + n, sizeof line - n, " chain: %u presents, shown on %u refreshes, %u refreshes elapsed", ps.PresentCount - lp, ps.PresentRefreshCount - lpr, ps.SyncRefreshCount - lsr);
                lp = ps.PresentCount; lpr = ps.PresentRefreshCount; lsr = ps.SyncRefreshCount; have2 = 1;
            } else n += snprintf(line + n, sizeof line - n, " chain: no present statistics");
            ex->lpVtbl->Release(ex);
        } else n += snprintf(line + n, sizeof line - n, " chain: not 9Ex");
    }
    LOG("%s", line);
}
static void clock_probe(void) {
    LARGE_INTEGER a, b; double smallest = 1e9; int zero = 0;
    QueryPerformanceCounter(&a);
    for (int i = 0; i < 20000; ++i) { QueryPerformanceCounter(&b); if (b.QuadPart != a.QuadPart) { double d = (double)(b.QuadPart - a.QuadPart) / (double)g_qpf.QuadPart; if (d < smallest) smallest = d; a = b; } else zero++; }
    ULONG minr = 0, maxr = 0, cur = 0;
    typedef LONG (WINAPI *NtQTR)(PULONG, PULONG, PULONG);
    HMODULE nt = GetModuleHandleA("ntdll.dll"); NtQTR q = nt ? (NtQTR)(void*)GetProcAddress(nt, "NtQueryTimerResolution") : NULL;
    if (q) q(&maxr, &minr, &cur);
    LOG("clock: counter %.3f MHz, smallest step seen %.1f us (%d unchanged reads of 20000), timer resolution %.2f ms (range %.2f-%.2f)",
        (double)g_qpf.QuadPart / 1e6, smallest >= 1e9 ? 0.0 : smallest * 1e6, zero, cur / 10000.0, minr / 10000.0, maxr / 10000.0);
}
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
/* Whether the catch-up tick can be made at all. It sets up the game's frame context by hand
   and may have to run the game's own end-of-pass cleanup, so it needs five addresses that a
   profile describing only the scheduler has not got yet. Without them the frame hook still
   runs -- one tick per frame, which is the whole feature -- and a hitch simply is not caught
   up, which at a 60 Hz logic rate costs a frame nobody sees. */
static int catchup_available(void) {
    return g_game->addr.frame_context_ptr && g_game->addr.frame_flag &&
           g_game->addr.frame_context_value && g_game->addr.cleanup_fn && g_game->addr.cleanup_this;
}
/* an update pass without drawing/presenting (used to catch up after a missed vblank) */
static int update_only_tick(void) {
    if (!catchup_available()) return 0;
    G_FRAME_FLAG34 = g_game->addr.frame_context_value;
    G_FRAME_FLAG38 = 1;
    int r = hfr_runner(G_UPDATE_RUNNER);
    if (r == 0)  { call_with_esi(g_game->addr.cleanup_fn, g_game->addr.cleanup_this); return 1; }
    if (r == -1) { call_with_esi(g_game->addr.cleanup_fn, g_game->addr.cleanup_this); return 2; }
    return 0;
}

/* How many frames were presented with no logic tick behind them. This is the number that
   answers "is the high frame rate real, or is it showing the same frame twice?" -- a repeated
   frame is exactly a present with nothing simulated since the last one. At the intended
   settings it is zero: the tick rate is the present rate, so every frame has its own tick.
   It is legitimately non-zero when the tick rate is deliberately set below the display rate
   (fps=60 on a 144 Hz screen asks for stock logic shown at 144 Hz), and when the game is
   paused or between stages and the simulation is not running at all. */
static unsigned g_stat_repeat_frames;
static uint64_t g_stat_ticks_run_prev;

/* The tick counter restarts when a replay begins or ends, so the difference across a window
   that contains one of those goes negative -- and a log that reports -2936 ticks/s is a log
   nobody can use to judge whether the frame rate is real. Report nothing rather than nonsense. */
static double tick_rate_since(double now) {
    double dt = now - g_stat_last;
    if (dt <= 0 || g_ticks_run < g_stat_ticks_run_last) return 0.0;
    return (double)(g_ticks_run - g_stat_ticks_run_last) / dt;
}

static void limiter_stats(double now) {
    g_stat_ticks++;
    if (g_ticks_run == g_stat_ticks_run_prev) g_stat_repeat_frames++;
    g_stat_ticks_run_prev = g_ticks_run;
    { static double last = 0; static unsigned hitches_logged; double period = 1.0 / (double)g_refresh;
      if (last > 0) { double gap = now - last; if (gap > 1.5 * period) g_stat_long++;
          g_gap_hist[gap < 0.001 ? 0 : gap < 0.002 ? 1 : gap < 0.004 ? 2 : gap < 0.008 ? 3 : gap < 0.012 ? 4 : gap < 0.020 ? 5 : 6]++;
          if (gap > 3 * period) { g_stat_vlong++;
              /* A hitch: say how long, and what this frame did, so a stutter report carries its own evidence. */
              if (gap > 0.04 && hitches_logged < 40) { hitches_logged++;
                  static unsigned last_tex_done, last_tex_redone;
                  LOG("hitch: %.1f ms between presents (target %.1f); this frame: %u draw calls, %u forced batch flushes, %u sprite VM draws, textures upscaled +%u redone +%u",
                      gap * 1000.0, period * 1000.0, g_frame_draws, g_frame_flushes, g_frame_vms, g_stat_tex_done - last_tex_done, g_stat_tex_redone - last_tex_redone);
                  last_tex_done = g_stat_tex_done; last_tex_redone = g_stat_tex_redone; } } }
      last = now; g_frame_draws = g_frame_flushes = g_frame_vms = 0; }
    g_rate_win_ticks++;
    if (g_rate_win_start == 0) g_rate_win_start = now;
    else if (now - g_rate_win_start >= 1.0) {
        double rate = g_rate_win_ticks / (now - g_rate_win_start);
        if (cfg.vsync && g_vsync_effective && rate > g_refresh * 1.08) { g_vsync_effective = 0; LOG("vsync does not seem to pace presentation (%.1f/s) — using software limiter", rate); }
        g_rate_win_start = now; g_rate_win_ticks = 0;
    }
    if (now - g_stat_last >= 5.0) {
        if (g_stat_last > 0)
            LOG("stats: %.2f presents/s (target %d), extra ticks %u, skipped ticks %u, sub calls %u, frame calls %u, logical %.3f, long gaps %u/%u, ticks/s %.2f, subtick polls %u applied %u, repeated frames %u/%u",
                g_stat_ticks / (now - g_stat_last), g_refresh, g_stat_catchup, g_stat_skipped, g_stat_sub_calls, g_stat_frame_calls, g_logical, g_stat_long, g_stat_vlong,
                tick_rate_since(now), g_stat_subtick_polls, g_stat_subtick_applied,
                g_stat_repeat_frames, g_stat_ticks);
        if (g_stat_last > 0 && (g_gap_inside_long || g_gap_outside_long))
            LOG("frame time: inside the game's frame function max %.1f ms (%u over 8 ms), outside it max %.1f ms (%u over 8 ms); longest spans: before first draw %.1f, drawing %.1f, in Present %.1f, after Present %.1f; present gaps <1/1-2/2-4/4-8/8-12/12-20/>20 ms: %u/%u/%u/%u/%u/%u/%u",
                g_gap_inside_max * 1000.0, g_gap_inside_long, g_gap_outside_max * 1000.0, g_gap_outside_long,
                g_span_max[0] * 1000.0, g_span_max[1] * 1000.0, g_span_max[2] * 1000.0, g_span_max[3] * 1000.0,
                g_gap_hist[0], g_gap_hist[1], g_gap_hist[2], g_gap_hist[3], g_gap_hist[4], g_gap_hist[5], g_gap_hist[6]);
        g_gap_inside_max = g_gap_outside_max = 0; g_gap_inside_long = g_gap_outside_long = 0; memset(g_span_max, 0, sizeof g_span_max); memset(g_gap_hist, 0, sizeof g_gap_hist);
        if (g_stat_joy_polls) { LOG("joystick: %u real polls, longest %.1f ms (on our thread)", g_stat_joy_polls, g_stat_joy_max * 1000.0); g_stat_joy_polls = 0; g_stat_joy_max = 0; }
        { static int probed; if (!probed) { probed = 1; clock_probe(); if (cfg.debug) sampler_start(); }
          if (cfg.debug) { thread_probe(now - g_stat_last); present_probe(now - g_stat_last); if (g_sampler_thread) sampler_report(); } }
        { static unsigned last_done, last_redone; if (g_stat_tex_done != last_done || g_stat_tex_redone != last_redone) { LOG("textures: %u upscaled, %u redone after a rewrite, %u MB", g_stat_tex_done, g_stat_tex_redone, (unsigned)(g_tex_bytes >> 20)); last_done = g_stat_tex_done; last_redone = g_stat_tex_redone; } }
        g_stat_last = now; g_stat_ticks = 0; g_stat_sub_calls = g_stat_frame_calls = g_stat_long = g_stat_vlong = g_stat_catchup = g_stat_skipped = 0; g_stat_ticks_run_last = g_ticks_run;
        g_stat_subtick_polls = g_stat_subtick_applied = 0; g_stat_repeat_frames = 0;
        if (cfg.debug) {
            node_census_report();
            uint8_t* rm=G_REPLAY_MANAGER;
            if (rm) LOG("state: stage=%d replay_frame=%d replay_mode=%d input=%08x",
                *(int*)(rm+g_game->layout.replay_stage),*(int*)(rm+g_game->layout.replay_frame),*(int*)(rm+0x10),(unsigned)G_GAME_INPUT);
        }
    }
}
