/* ------------------------------------------------------- another patch in the same game
 * vpatch (VsyncPatch, by swmpLV/75E) replaces the game's frame limiter and calls Present on
 * its own schedule. So does this patch. Two frame schedulers in one process means the game
 * runs at whichever one's rate, and nothing anywhere says why -- their patch sites do not
 * overlap ours, so the frozen signatures cannot notice, and both installs report success.
 *
 * Timing is the whole difficulty. vpatch launches the game suspended and injects itself with
 * CreateRemoteThread + LoadLibrary, and it is tempting to assume it therefore gets there
 * first. It does not: that injected thread runs loader initialisation before its LoadLibrary
 * call, and loader initialisation is what loads this DLL, since the game imports it. Traced
 * under Wine, the game process loads DINPUT8.dll and only then vpatch_th12.dll. So at the
 * moment this patch installs, the game is still clean and there is nothing to find.
 *
 * Hence two checks. At install time, which catches an executable already modified on disk
 * and anything that did get in earlier, we refuse outright. On the first frame, by which
 * point everything that is going to load has loaded, it is far too late to refuse -- so we
 * say plainly what is happening instead of leaving someone to wonder why a patch they
 * installed appears to do nothing.
 */

/* Is a patch that rewrites the game's frame loop in this process? Two independent tests:
   a module whose name we recognise, and the game's own frame-loop code no longer being the
   code it shipped with. The second is the more general of the two -- it does not care whose
   patch it is -- but it only works for a game with recorded sites. */
static int conflict_module(char* out, size_t n) {
    static const char* const known[] = { "vpatch", "openinputlagpatch" };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32 me; me.dwSize = sizeof me;
    int found = 0;
    if (Module32First(snap, &me)) do {
        char lower[MAX_PATH];
        size_t i = 0;
        for (; i + 1 < sizeof lower && me.szModule[i]; ++i)
            lower[i] = (char)tolower((unsigned char)me.szModule[i]);
        lower[i] = 0;
        for (size_t k = 0; k < sizeof known / sizeof *known; ++k)
            if (strstr(lower, known[k])) { snprintf(out, n, "%s", me.szModule); found = 1; break; }
    } while (!found && Module32Next(snap, &me));
    CloseHandle(snap);
    return found;
}

/* thprac (the practice tool) starts the game itself, and its launcher stamps a four-byte
   marker into the DOS header padding immediately before the PE header: 'CARP' written as a
   DWORD, which is the bytes "PRAC" in memory. Those four bytes are zero in every executable
   these games shipped with, so the marker says plainly who launched this process -- and it
   is there before the game's first instruction runs, earlier than thprac's own module exists
   to be found by name.

   thprac is not a conflict. It and this patch share a game quite happily: their patch sites
   are disjoint in all four games, and the replacement update runner leaves the instruction
   its menu hook needs (update_runner.c). Its launcher is what needs saying something about,
   and only when it has been asked to bring vpatch or openinputlagpatch along. */
static int thprac_present(void) {
    const uint8_t* base = (const uint8_t*)g_image_base;
    if (memcmp(base, "MZ", 2) != 0) return 0;
    uint32_t lfanew = *(const uint32_t*)(base + 0x3c);
    if (lfanew < 8 || lfanew > 0x1000) return 0;
    if (memcmp(base + lfanew, "PE\0\0", 4) != 0) return 0;
    return memcmp(base + lfanew - 4, "PRAC", 4) == 0;
}

/* Said from a thread of its own. A message box called from DllMain would hold the loader
   lock while it waited for the user; the thread cannot start until that lock is released,
   which is exactly when showing one becomes safe. Shared with the wrapper warning in the
   Direct3D backend, so there is one piece of dialog machinery rather than two. */
static char g_notice_text[900];
static LONG g_notice_shown;
static DWORD WINAPI notice_dialog(LPVOID unused) {
    (void)unused;
    MessageBoxA(NULL, g_notice_text, "Touhou HFR",
                MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
    return 0;
}
/* At most one notice a run: two boxes stacked over a game nobody has looked at yet is worse
   than one, and the buffer is shared. */
static int show_notice(const char* text) {
    if (InterlockedCompareExchange(&g_notice_shown, 1, 0) != 0) return 0;
    snprintf(g_notice_text, sizeof g_notice_text, "%s", text);
    HANDLE t = CreateThread(NULL, 0, notice_dialog, NULL, 0, NULL);
    if (t) CloseHandle(t);
    return 1;
}

/* Returns non-zero when something else has the frame loop. `installed` says whether this
   patch is already in place, which is the difference between a refusal and a warning. */
static int conflict_found(int installed) {
    if (!g_game) return 0;
    char module[MAX_PATH] = "";
    int has_module = conflict_module(module, sizeof module);
    int site = conflict_scan(g_game->identity, (const uint8_t*)g_image_base, g_game->identity->image_size);
    if (!has_module && site < 0) return 0;

    const struct ConflictSite* c = site >= 0 ? &g_game->identity->conflicts[site] : NULL;
    if (has_module && c) LOG("CONFLICT: %s is loaded and has patched %s at 0x%06x", module, c->what, (unsigned)c->addr);
    else if (has_module) LOG("CONFLICT: %s is loaded in this process", module);
    else                 LOG("CONFLICT: %s at 0x%06x is not the code this game shipped with", c->what, (unsigned)c->addr);

    char who[MAX_PATH + 2] = "";
    if (has_module) snprintf(who, sizeof who, ": %s", module);

    char text[900];
    /* thprac's launcher pulled it in: say so, because "start the game through touhou_hfr.exe
       rather than the other patch's launcher" is useless advice to someone who wants thprac's
       launcher -- and they can keep it, they just have to untick one box. */
    if (has_module && thprac_present())
        snprintf(text, sizeof text,
            "thprac started this game and brought another patch in with it%s.\n\n"
            "thprac's launcher loads any vpatch*.dll or openinputlagpatch.dll it finds beside "
            "the game. That is its \"Use VsyncPatch (if avaliable)\" and \"Use OpenInputLagPatch "
            "(if avaliable)\" options, both on by default. That patch and Touhou HFR each replace "
            "the game's frame limiter and each decide when frames are presented, so together they "
            "leave the game running at the other one's frame rate.\n\n"
            "%sUntick those options in thprac's launcher, or delete that file from the game's "
            "folder. thprac itself runs alongside Touhou HFR and needs no change.", who,
            installed ? "" : "Touhou HFR has not installed. The game will run without it.\n\n");
    else if (!installed)
        snprintf(text, sizeof text,
            "Another patch has already modified this game%s.\n\n"
            "It and Touhou HFR both replace the game's frame limiter and both decide when "
            "frames are presented, so together they would leave the game running at the other "
            "patch's frame rate -- the one thing this patch exists to change.\n\n"
            "Touhou HFR has not installed. The game will run without it.\n\n"
            "Start the game through touhou_hfr.exe rather than the other patch's launcher, or "
            "remove the other patch from the game's folder.", who);
    else
        snprintf(text, sizeof text,
            "Another patch is running in this game alongside Touhou HFR%s.\n\n"
            "Both replace the game's frame limiter and both decide when frames are presented. "
            "It loaded after Touhou HFR did, too late to be refused, and the game is now being "
            "paced by whichever of the two got the last word -- so the high frame rate, the "
            "smoothing and the frame pacing may all behave oddly or not at all.\n\n"
            "Start the game through touhou_hfr.exe rather than the other patch's launcher, or "
            "remove the other patch from the game's folder.\n\n"
            "Touhou HFR replaces what vpatch did for these games, so you should not need both.", who);

    show_notice(text);
    return 1;
}

/* Everything loaded into this process that did not come out of the system directory: the
   game's own libraries and anything another patch injected. Not a conflict -- most of what
   turns up here is harmless, and translation patches such as thcrap share the executable
   with this one quite happily -- but "what else is in there" is the first question every
   report raises, and the log is the only thing anyone can send. One line, once. */
static void log_modules(void) {
    char sysdir[MAX_PATH];
    UINT n = GetSystemDirectoryA(sysdir, sizeof sysdir);
    if (!n || n >= sizeof sysdir) return;
    HANDLE snap = INVALID_HANDLE_VALUE;
    /* Documented to fail with ERROR_BAD_LENGTH while modules are still loading; retry. */
    for (int try_ = 0; try_ < 8 && snap == INVALID_HANDLE_VALUE; ++try_)
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) { LOG("modules: no snapshot available (%lu)", GetLastError()); return; }
    MODULEENTRY32 me; me.dwSize = sizeof me;
    char line[900]; size_t used = 0; int count = 0;
    line[0] = 0;
    if (Module32First(snap, &me)) do {
        if (!_strnicmp(me.szExePath, sysdir, n)) continue;      /* Windows' own */
        int w = snprintf(line + used, sizeof line - used, "%s%s", count ? ", " : "", me.szModule);
        if (w < 0 || (size_t)w >= sizeof line - used) break;
        used += (size_t)w; ++count;
    } while (Module32Next(snap, &me));
    CloseHandle(snap);
    LOG("modules from outside the system directory: %s", count ? line : "(none)");
}

/* Called from the frame hook. Everything that is going to load has loaded by now. */
static void conflict_check_late(void) {
    static int done;
    if (done) return;
    done = 1;
    g_frame_seen = 1;
    log_modules();
    if (thprac_present()) LOG("thprac launched this game (its marker is in the executable header)");
    conflict_found(1);
}
