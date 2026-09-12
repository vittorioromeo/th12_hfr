/* A sampling profile of the game's main thread, for stutter reports: once a millisecond a
   watchdog thread suspends it, reads its instruction pointer and the first return addresses
   on its stack, and resumes it. Every stats window the samples are summarised as the share
   of time per module and the most frequent game-side return addresses seen while the thread
   sat in the system (ntdll/kernelbase/win32u): those name the call the thread blocks in.
   Debug only; the cost is a few percent of one core. */
#include <psapi.h>
#include <winternl.h>
static double now_s(void);
static HANDLE g_sampler_thread, g_sampler_target; static volatile int g_sampler_stop;
static CRITICAL_SECTION g_sampler_lock;
enum { SAMP_MODS = 24, SAMP_SITES = 48 };
static struct { uintptr_t base, end; char name[24]; unsigned hits; } g_samp_mod[SAMP_MODS]; static int g_samp_nmod;
static struct { char key[160]; unsigned hits; } g_samp_site[SAMP_SITES];
static unsigned g_samp_total, g_samp_system;
static uintptr_t g_samp_game_lo, g_samp_game_hi;

static int samp_module_of(uintptr_t a) {
    for (int i = 0; i < g_samp_nmod; ++i) if (a >= g_samp_mod[i].base && a < g_samp_mod[i].end) return i;
    MEMORY_BASIC_INFORMATION mbi; if (!VirtualQuery((void*)a, &mbi, sizeof mbi) || mbi.Type != MEM_IMAGE || g_samp_nmod >= SAMP_MODS) return -1;
    MODULEINFO mi; HMODULE h = (HMODULE)mbi.AllocationBase; char path[MAX_PATH] = "?";
    if (!GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof mi)) return -1;
    GetModuleFileNameA(h, path, sizeof path); const char* p = strrchr(path, '\\'); p = p ? p + 1 : path;
    int i = g_samp_nmod++; g_samp_mod[i].base = (uintptr_t)mi.lpBaseOfDll; g_samp_mod[i].end = g_samp_mod[i].base + mi.SizeOfImage; g_samp_mod[i].hits = 0;
    strncpy(g_samp_mod[i].name, p, sizeof g_samp_mod[i].name - 1); g_samp_mod[i].name[sizeof g_samp_mod[i].name - 1] = 0;
    return i;
}
static int samp_is_system(int m) {
    if (m < 0) return 0; const char* n = g_samp_mod[m].name;
    return !_stricmp(n, "ntdll.dll") || !_stricmp(n, "kernelbase.dll") || !_stricmp(n, "kernel32.dll") || !_stricmp(n, "win32u.dll") || !_stricmp(n, "user32.dll") || !_stricmp(n, "gdi32full.dll");
}
static void samp_site(const char* key) {
    int free = -1;
    for (int i = 0; i < SAMP_SITES; ++i) { if (g_samp_site[i].hits && !strcmp(g_samp_site[i].key, key)) { g_samp_site[i].hits++; return; } if (!g_samp_site[i].hits && free < 0) free = i; }
    if (free >= 0) { strncpy(g_samp_site[free].key, key, sizeof g_samp_site[free].key - 1); g_samp_site[free].key[sizeof g_samp_site[free].key - 1] = 0; g_samp_site[free].hits = 1; }
}
/* the nearest of a few well-known ntdll/win32u entry points below an address: which wait it is */
static const char* samp_syscall(uintptr_t eip) {
    static const char* names[] = { "NtWaitForSingleObject", "NtWaitForMultipleObjects", "NtDelayExecution", "NtWaitForAlertByThreadId", "NtYieldExecution", "NtSignalAndWaitForSingleObject", "NtRemoveIoCompletion", "NtReleaseSemaphore", "NtSetEvent", "NtQueryPerformanceCounter", "NtDeviceIoControlFile", "NtGdiDdDDIWaitForVerticalBlankEvent", "NtGdiDdDDIWaitForVerticalBlankEvent2", "NtGdiDdDDIPresent", "NtGdiDdDDIWaitForSynchronizationObjectFromCpu", "NtGdiDdDDIGetDeviceState", "NtUserMsgWaitForMultipleObjectsEx", "NtGdiDdDDILock2", "NtGdiDdDDIEscape", "NtGdiDdDDISubmitCommand", "NtGdiDdDDICreateAllocation", "NtGdiDdDDIPresentMultiPlaneOverlay3", "NtGdiDdDDIGetPresentHistory", "NtGdiDdDDIQueryAdapterInfo" };
    static uintptr_t addrs[sizeof names / sizeof *names]; static int init;
    if (!init) { init = 1; HMODULE nt = GetModuleHandleA("ntdll.dll"), w = GetModuleHandleA("win32u.dll"); for (size_t i = 0; i < sizeof names / sizeof *names; ++i) { void* f = nt ? (void*)GetProcAddress(nt, names[i]) : NULL; if (!f && w) f = (void*)GetProcAddress(w, names[i]); addrs[i] = (uintptr_t)f; } }
    int best = -1; for (size_t i = 0; i < sizeof names / sizeof *names; ++i) if (addrs[i] && eip >= addrs[i] && eip - addrs[i] < 64 && (best < 0 || addrs[i] > addrs[best])) best = (int)i;
    return best >= 0 ? names[best] : "?";
}
/* Scheduler-state sampling: what the kernel says the main thread is doing -- running, ready
   but not scheduled (starved), or waiting and on what kind of object -- plus its context
   switches. A thread that is Ready most of a tick is being pre-empted; one that is Waiting
   is blocked on something; the wait reason narrows that something down. */
static double g_samp_sleep_total, g_samp_sleep_max; static unsigned g_samp_sleep_n;
static unsigned g_st_hist[9], g_st_wait[40], g_st_samples, g_st_cs_last, g_st_cs; static double g_st_t0;
static void sampler_state_sample(void) {
    typedef LONG (WINAPI *NtQSI)(ULONG, PVOID, ULONG, PULONG);
    static NtQSI q; static uint8_t* buf; static ULONG cap = 2u << 20;
    if (!q) { HMODULE nt = GetModuleHandleA("ntdll.dll"); q = nt ? (NtQSI)(void*)GetProcAddress(nt, "NtQuerySystemInformation") : NULL; if (!q) return; }
    if (!buf) buf = (uint8_t*)malloc(cap); if (!buf) return;
    ULONG need = 0; LONG st = q(5 /* SystemProcessInformation */, buf, cap, &need);
    if (st != 0) { if (need > cap) { free(buf); cap = need + (1u << 20); buf = (uint8_t*)malloc(cap); } return; }
    DWORD pid = GetCurrentProcessId(), tid = GetThreadId(g_sampler_target);
    for (uint8_t* p = buf;;) {
        SYSTEM_PROCESS_INFORMATION* pi = (SYSTEM_PROCESS_INFORMATION*)p;
        if ((DWORD)(uintptr_t)pi->UniqueProcessId == pid) {
            SYSTEM_THREAD_INFORMATION* ti = (SYSTEM_THREAD_INFORMATION*)(p + sizeof *pi);
            for (ULONG i = 0; i < pi->NumberOfThreads; ++i) if ((DWORD)(uintptr_t)ti[i].ClientId.UniqueThread == tid) {
                ULONG state = ti[i].ThreadState, reason = ti[i].WaitReason, cs = ti[i].Reserved3;   /* Reserved3 is ContextSwitches */
                if (state < 9) g_st_hist[state]++; if (state == 5 && reason < 40) g_st_wait[reason]++;
                if (g_st_cs_last) g_st_cs += cs - g_st_cs_last; g_st_cs_last = cs; g_st_samples++;
                break;
            }
            break;
        }
        if (!pi->NextEntryOffset) break; p += pi->NextEntryOffset;
    }
}
static void sampler_state_report(void) {
    if (!g_st_samples) return;
    static const char* states[] = { "init", "ready", "running", "standby", "terminated", "waiting", "transition", "deferred-ready", "gate-wait" };
    static const char* reasons[] = { "Executive", "FreePage", "PageIn", "PoolAlloc", "DelayExecution", "Suspended", "UserRequest", "WrExecutive", "WrFreePage", "WrPageIn", "WrPoolAlloc", "WrDelayExecution", "WrSuspended", "WrUserRequest", "WrEventPair", "WrQueue", "WrLpcReceive", "WrLpcReply", "WrVirtualMemory", "WrPageOut", "WrRendezvous", "WrKeyedEvent", "WrTerminated", "WrProcessInSwap", "WrCpuRateControl", "WrCalloutStack", "WrKernel", "WrResource", "WrPushLock", "WrMutex", "WrQuantumEnd", "WrDispatchInt", "WrPreempted", "WrYieldExecution", "WrFastMutex", "WrGuardedMutex", "WrRundown", "WrAlertByThreadId", "WrDeferredPreempt", "?" };
    char line[700]; int n = snprintf(line, sizeof line, "main thread state (%u samples):", g_st_samples);
    for (int i = 0; i < 9; ++i) if (g_st_hist[i]) n += snprintf(line + n, sizeof line - n, " %s %u%%", states[i], g_st_hist[i] * 100 / g_st_samples);
    n += snprintf(line + n, sizeof line - n, "; waits:");
    for (int i = 0; i < 40 && n < (int)sizeof line - 40; ++i) if (g_st_wait[i]) n += snprintf(line + n, sizeof line - n, " %s %u%%", reasons[i < 39 ? i : 39], g_st_wait[i] * 100 / g_st_samples);
    double dt = now_s() - g_st_t0; n += snprintf(line + n, sizeof line - n, "; context switches %.0f/s; the watcher's own Sleep(1): mean %.2f ms, worst %.1f ms", dt > 0 ? g_st_cs / dt : 0.0, g_samp_sleep_n ? g_samp_sleep_total / g_samp_sleep_n * 1000.0 : 0.0, g_samp_sleep_max * 1000.0);
    g_samp_sleep_total = g_samp_sleep_max = 0; g_samp_sleep_n = 0;
    LOG("%s", line);
    memset(g_st_hist, 0, sizeof g_st_hist); memset(g_st_wait, 0, sizeof g_st_wait); g_st_samples = 0; g_st_cs = 0; g_st_t0 = now_s();
}
static int g_sampler_stacks = 0;   /* 1: also suspend the thread and read its stack (the module/path report) */
static DWORD WINAPI sampler_main(void* arg) {
    (void)arg;
    while (!g_sampler_stop) {
        { double t = now_s(); Sleep(1); t = now_s() - t; g_samp_sleep_total += t; g_samp_sleep_n++; if (t > g_samp_sleep_max) g_samp_sleep_max = t; }
        sampler_state_sample();
        if (!g_sampler_stacks) continue;
        if (SuspendThread(g_sampler_target) == (DWORD)-1) continue;
        CONTEXT c; memset(&c, 0, sizeof c); c.ContextFlags = CONTEXT_CONTROL;
        uintptr_t eip = 0, esp = 0; uint32_t stack[512]; int nstack = 0;
        if (GetThreadContext(g_sampler_target, &c)) {
            eip = c.Eip; esp = c.Esp;
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery((void*)esp, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
                uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize; size_t n = (end - esp) / 4; if (n > 512) n = 512;
                memcpy(stack, (void*)esp, n * 4); nstack = (int)n;
            }
        }
        ResumeThread(g_sampler_target);
        if (!eip) continue;
        EnterCriticalSection(&g_sampler_lock);
        g_samp_total++;
        int m = samp_module_of(eip); if (m >= 0) g_samp_mod[m].hits++;
        if (samp_is_system(m)) {
            g_samp_system++;
            /* the wait it sits in, then the first return addresses up the stack outside the
               system modules, in order (innermost first): the path it was called by */
            char key[160]; int n = snprintf(key, sizeof key, "%s", samp_syscall(eip)); int found = 0; uintptr_t last = 0;
            for (int i = 0; i < nstack && i < 160 && found < 5 && n < (int)sizeof key - 30; ++i) {
                uintptr_t a = stack[i]; if (a < 0x10000 || a == last) continue;
                int mm = (a >= g_samp_game_lo && a < g_samp_game_hi) ? -2 : samp_module_of(a);
                if (mm == -1 || (mm >= 0 && samp_is_system(mm))) continue;
                n += snprintf(key + n, sizeof key - n, " %s+%x", mm == -2 ? "game" : g_samp_mod[mm].name, (unsigned)(a - (mm == -2 ? g_samp_game_lo : g_samp_mod[mm].base))); found++; last = a;
            }
            samp_site(key);
        }
        LeaveCriticalSection(&g_sampler_lock);
    }
    return 0;
}
static void sampler_start(void) {
    if (g_sampler_thread) return;
    InitializeCriticalSection(&g_sampler_lock);
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_sampler_target, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    MODULEINFO mi; if (GetModuleInformation(GetCurrentProcess(), GetModuleHandleA(NULL), &mi, sizeof mi)) { g_samp_game_lo = (uintptr_t)mi.lpBaseOfDll; g_samp_game_hi = g_samp_game_lo + mi.SizeOfImage; }
    g_sampler_thread = CreateThread(NULL, 0, sampler_main, NULL, 0, NULL);
    g_st_t0 = now_s(); LOG("sampler: watching the main thread at 1 kHz");
}
static double now_s(void);
static void sampler_report(void) {
    if (!g_sampler_thread) return;
    sampler_state_report();
    if (!g_sampler_stacks) return;
    EnterCriticalSection(&g_sampler_lock);
    char line[1200]; int n = snprintf(line, sizeof line, "samples: %u, in the system %u;", g_samp_total, g_samp_system);
    for (int i = 0; i < g_samp_nmod && n < (int)sizeof line - 60; ++i) if (g_samp_mod[i].hits * 100 >= g_samp_total) n += snprintf(line + n, sizeof line - n, " %s %u%%", g_samp_mod[i].name, g_samp_total ? g_samp_mod[i].hits * 100 / g_samp_total : 0);
    n += snprintf(line + n, sizeof line - n, "; blocked in:");
    for (int k = 0; k < 4; ++k) {
        int best = -1; for (int i = 0; i < SAMP_SITES; ++i) if (g_samp_site[i].hits && (best < 0 || g_samp_site[i].hits > g_samp_site[best].hits)) best = i;
        if (best < 0 || g_samp_site[best].hits * 50 < g_samp_system || n >= (int)sizeof line - 200) break;
        n += snprintf(line + n, sizeof line - n, " [%s](%u)", g_samp_site[best].key, g_samp_site[best].hits);
        g_samp_site[best].hits = 0;
    }
    memset(g_samp_site, 0, sizeof g_samp_site); for (int i = 0; i < g_samp_nmod; ++i) g_samp_mod[i].hits = 0; g_samp_total = g_samp_system = 0;
    LeaveCriticalSection(&g_sampler_lock);
    LOG("%s", line);
}
