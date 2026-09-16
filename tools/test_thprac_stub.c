/* A stand-in for thprac, for proving in a running game that the replacement update runner
 * still delivers the instruction thprac's menu hook sits on.
 *
 * It reproduces thprac's hook mechanism exactly (thprac_hook.cpp): one 0xCC over the first
 * byte of the target instruction, a vectored exception handler at the front of the chain
 * that runs a callback and resumes on a codecave holding the original instruction followed
 * by a jump back. And it reproduces the part that makes the bug visible -- thprac's
 * GameGuiProgress state machine, where the update callback opens an ImGui frame (0 -> 1 ->
 * 2) and the draw callback only renders when one is open (2 -> 0). With the update hook
 * bypassed, "rendered" stays at zero however often the draw hook fires, which is exactly
 * what a player sees: no menu, no error, nothing in any log.
 *
 * Loaded in the game's place of dinput8.dll; it loads the patch itself from hfr_real.dll and
 * forwards the one export the game asks for. Addresses come from the environment, so the
 * same build serves all four games:
 *
 *   HFRSTUB_UPDATE=4625fb HFRSTUB_RENDER=462722 HFRSTUB_DELAY=6
 *   HFRSTUB_UPDATE_LEN / HFRSTUB_RENDER_LEN  (thprac's instr_size; 1 unless stated)
 *
 * HFRSTUB_DELAY is how long to wait before installing, in seconds: thprac enables its main
 * hooks well after the game has started, so the default puts this after the patch installs,
 * and 0 puts it before. Both orders must work. Never shipped; tools only.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static FILE* g_log;
static void logf_(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

struct Hook {
    uintptr_t addr;
    uint8_t orig;
    uint8_t* cave;
    unsigned hits;
};
static struct Hook g_update, g_render;
static volatile long g_progress;        /* thprac's GameGuiProgress */
static volatile long g_opened, g_rendered, g_render_without_frame;

static uint8_t* make_cave(uintptr_t addr, size_t len) {
    uint8_t* cave = VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!cave) return NULL;
    memcpy(cave, (void*)addr, len);
    cave[len] = 0xe9;
    *(uint32_t*)(cave + len + 1) = (uint32_t)(addr + len - (uintptr_t)(cave + len + 5));
    return cave;
}
static int hook_install(struct Hook* h, uintptr_t addr, size_t len) {
    if (!addr) return 0;
    h->addr = addr;
    h->orig = *(uint8_t*)addr;
    h->cave = make_cave(addr, len);
    if (!h->cave) return 0;
    DWORD prot;
    if (!VirtualProtect((void*)addr, 1, PAGE_EXECUTE_READWRITE, &prot)) return 0;
    *(uint8_t*)addr = 0xcc;
    VirtualProtect((void*)addr, 1, prot, &prot);
    return 1;
}

static LONG NTAPI veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    DWORD eip = ep->ContextRecord->Eip;
    struct Hook* h = eip == g_update.addr ? &g_update : eip == g_render.addr ? &g_render : NULL;
    if (!h) return EXCEPTION_CONTINUE_SEARCH;
    ++h->hits;
    if (h == &g_update) {                       /* GameGuiBegin ... GameGuiEnd */
        g_progress = 1;
        InterlockedIncrement(&g_opened);
        g_progress = 2;
    } else {                                    /* GameGuiRender */
        if (g_progress == 2) { InterlockedIncrement(&g_rendered); g_progress = 0; }
        else InterlockedIncrement(&g_render_without_frame);
    }
    ep->ContextRecord->Eip = (DWORD)(uintptr_t)h->cave;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static uintptr_t env_addr(const char* name) {
    char buf[32];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof buf);
    if (!n || n >= sizeof buf) return 0;
    return (uintptr_t)strtoul(buf, NULL, 16);
}

static unsigned env_num(const char* name, unsigned fallback) {
    char buf[32];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof buf);
    return n && n < sizeof buf ? (unsigned)strtoul(buf, NULL, 10) : fallback;
}

static DWORD WINAPI worker(LPVOID unused) {
    (void)unused;
    unsigned delay = env_num("HFRSTUB_DELAY", 6);
    Sleep(delay * 1000);
    uintptr_t upd = env_addr("HFRSTUB_UPDATE"), rnd = env_addr("HFRSTUB_RENDER");
    /* thprac's instr_size: how many bytes of the hooked instruction the codecave carries. */
    size_t ulen = env_num("HFRSTUB_UPDATE_LEN", 1), rlen = env_num("HFRSTUB_RENDER_LEN", 1);
    logf_("installing after %us: update 0x%06x+%u (byte %02x), render 0x%06x+%u (byte %02x)",
          delay, (unsigned)upd, (unsigned)ulen, upd ? *(uint8_t*)upd : 0,
          (unsigned)rnd, (unsigned)rlen, rnd ? *(uint8_t*)rnd : 0);
    AddVectoredExceptionHandler(1, veh);
    logf_("update hook: %s", hook_install(&g_update, upd, ulen) ? "installed" : "FAILED");
    logf_("render hook: %s", hook_install(&g_render, rnd, rlen) ? "installed" : "FAILED");
    for (;;) {
        Sleep(2000);
        logf_("update hits %u, draw hits %u | frames opened %ld, rendered %ld, rendered with no frame open %ld",
              g_update.hits, g_render.hits, g_opened, g_rendered, g_render_without_frame);
    }
    return 0;
}

typedef HRESULT (WINAPI *DI8Fn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DI8Fn g_real_di8;
__declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD v, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    if (!g_real_di8) {
        HMODULE m = GetModuleHandleA("hfr_real.dll");
        if (m) g_real_di8 = (DI8Fn)(void*)GetProcAddress(m, "DirectInput8Create");
        if (!g_real_di8) {
            char path[MAX_PATH];
            UINT n = GetSystemDirectoryA(path, sizeof path);
            if (n && n < sizeof path) {
                snprintf(path + n, sizeof path - n, "\\dinput8.dll");
                HMODULE s = LoadLibraryA(path);
                if (s) g_real_di8 = (DI8Fn)(void*)GetProcAddress(s, "DirectInput8Create");
            }
        }
    }
    return g_real_di8 ? g_real_di8(h, v, riid, out, outer) : E_FAIL;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res) {
    (void)h; (void)res;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    g_log = fopen("thprac_stub.log", "w");
    logf_("stand-in loaded");
    LoadLibraryA("hfr_real.dll");        /* the patch, under a name the game does not import */
    CreateThread(NULL, 0, worker, NULL, 0, NULL);
    return TRUE;
}
