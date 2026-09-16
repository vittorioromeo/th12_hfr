/* dxgi.dll proxy: how the patch reaches New Classic when Steam is the launcher.
 *
 * The x86 games load `dinput8.dll`, so the patch ships a proxy for it and installs itself
 * whichever way the game is started. New Classic imports almost nothing -- its only
 * non-system import is `steam_api64.dll` -- so there was no equivalent, and the x64 patch
 * could only be installed by its own launcher, which creates the game process suspended
 * and injects.
 *
 * On the Steam build that cannot work. th06nc.exe's first act is
 *
 *     mov ecx, 0x48afc6                 ; the app id
 *     call SteamAPI_RestartAppIfNecessary
 *     test al, al ; jne -> mov rax,-1 ; ret
 *
 * so a process Steam did not launch asks Steam to launch it again and exits. The launcher's
 * injection is thrown away with the process, and the relaunch Steam attempts is the
 * "Game configuration unavailable" dialog owners were seeing.
 *
 * DxLib does load one thing by bare name, though:
 *
 *     LoadLibraryW(L"dxgi.dll")  ->  GetProcAddress("CreateDXGIFactory2")
 *
 * and Windows searches the executable's own directory before System32 (dxgi is not a
 * KnownDLL -- this is the same door ReShade and Special K use). So a dxgi.dll beside the
 * game is loaded by the game itself, on every launch, whoever started it. This file is that
 * proxy: it forwards all 57 of the real library's exports, and on the first factory call --
 * safely outside the loader lock, and long before the frame loop it is about to patch --
 * it loads touhou_hfr64.dll and starts the runtime.
 *
 * It is deliberately incurious. It does not know which game it is in, does not read the INI,
 * and does nothing at all if touhou_hfr64.dll is absent; the runtime checks the executable's
 * fingerprint itself and declines to patch anything it does not recognise.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "dxgi_exports.h"

/* Opt-in diagnostic build for failures before the runtime can create its own log.
   Preserve LastError, and report to both a separate file and Proton's +debugstr log.
   No tracing or extra file I/O is compiled into the ordinary release proxy. */
#ifdef HFR_PROXY_DIAGNOSTICS
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
static char proxy_log_path[MAX_PATH];
static void proxy_trace_init(HINSTANCE self) {
    DWORD saved = GetLastError();
    DWORD n = GetModuleFileNameA(self, proxy_log_path, sizeof proxy_log_path);
    char* slash = n && n < sizeof proxy_log_path ? strrchr(proxy_log_path, '\\') : NULL;
    if (slash && (size_t)(slash-proxy_log_path)+sizeof "\\touhou_hfr_proxy.log" <= sizeof proxy_log_path)
        strcpy(slash+1, "touhou_hfr_proxy.log");
    else proxy_log_path[0] = 0;
    SetLastError(saved);
}
static void proxy_trace(const char* fmt, ...) {
    DWORD saved = GetLastError();
    char line[1200];
    int used = snprintf(line, sizeof line, "Touhou HFR proxy [pid=%lu tid=%lu]: ",
                        GetCurrentProcessId(), GetCurrentThreadId());
    va_list ap; va_start(ap, fmt);
    vsnprintf(line+used, sizeof line-used-2, fmt, ap);
    va_end(ap);
    size_t n = strlen(line); line[n++] = '\n'; line[n] = 0;
    OutputDebugStringA(line);
    if (proxy_log_path[0]) {
        HANDLE file = CreateFileA(proxy_log_path, FILE_APPEND_DATA, FILE_SHARE_READ|FILE_SHARE_WRITE,
                                  NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(file, line, (DWORD)n, &written, NULL);
            CloseHandle(file);
        }
    }
    SetLastError(saved);
}
#else
#define proxy_trace_init(self) ((void)0)
#define proxy_trace(...) ((void)0)
#endif

static HMODULE real_dxgi;
/* Called instead of an export this system's dxgi.dll does not have. Returning E_NOTIMPL
   rather than jumping through a null pointer keeps a missing name a failed call. */
static HRESULT WINAPI proxy_absent(void) { return 0x80004001L; }

/* One slot per export, and one stub per export whose only instruction jumps through it.
   The stub preserves every register and the stack exactly as it found them, so it is
   transparent to any calling convention: the real function returns to the real caller. */
#define DECLARE_SLOT(name) static void* __attribute__((used)) real_##name = (void*)proxy_absent;
DXGI_EXPORTS(DECLARE_SLOT)

#define DECLARE_STUB(name) \
    __asm__(".globl " #name "\n" #name ": jmp *real_" #name "(%rip)\n");
DXGI_EXPORTS(DECLARE_STUB)

/* The three factory entry points are written out rather than generated: they are where the
   runtime is started, and they must forward afterwards. */
#define DECLARE_EXPORT(name) __declspec(dllexport) void name(void);
DXGI_EXPORTS(DECLARE_EXPORT)

/* The real library is asked for by its full path in System32, never by name: the loader
   answers a bare name with whatever module of that base name is already loaded, which here
   would be this file. Windows distinguishes the two by path -- the patch's own x86
   dinput8.dll proxy has resolved the system dinput8 this way since the first release -- but
   a loader that does not (Wine's, today) would hand us ourselves, and every forwarded call
   would then jump straight back into its own stub. So the answer is checked. If it is this
   module, nothing is resolved: every export keeps the stub that fails, which is a game that
   reports it cannot create a device rather than one that hangs in a loop. */
static void load_real(HINSTANCE self) {
    char path[MAX_PATH];
    UINT n = GetSystemDirectoryA(path, MAX_PATH);
    if (!n || n > MAX_PATH - 12) { proxy_trace("GetSystemDirectory failed/too long: n=%u error=%lu", n, GetLastError()); return; }
    lstrcatA(path, "\\dxgi.dll");
    proxy_trace("loading system DXGI: %s", path);
    HMODULE found = LoadLibraryA(path);
    if (!found) { proxy_trace("system DXGI load failed: error=%lu", GetLastError()); return; }
    proxy_trace("system DXGI=%p proxy=%p", (void*)found, (void*)self);
    if (found == (HMODULE)self) {
        OutputDebugStringA("Touhou HFR: this loader cannot tell the proxy from the system "
                           "dxgi.dll; no export was forwarded.");
        FreeLibrary(found);
        return;
    }
    real_dxgi = found;
#define RESOLVE(name) { FARPROC p = GetProcAddress(real_dxgi, #name); if (p) real_##name = (void*)p; }
    DXGI_EXPORTS(RESOLVE)
    proxy_trace("system factories=%p/%p/%p", real_CreateDXGIFactory,
                real_CreateDXGIFactory1, real_CreateDXGIFactory2);
}

/* Starting the runtime waits for the game's first call into us. Doing it from DllMain would
   run MinHook's thread enumeration under the loader lock; doing it from a thread spawned
   there would race the game to the frame loop. The factory call is neither: the game makes
   it while initialising Direct3D, on its own main thread, with nothing else running yet. */
typedef DWORD (WINAPI *StartFn)(void*);
static void start_runtime(void) {
    static LONG once;
    if (InterlockedCompareExchange(&once, 1, 0)) return;
    char path[MAX_PATH];
    HMODULE self;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&start_runtime, &self)) {
        proxy_trace("runtime start: GetModuleHandleEx failed: error=%lu", GetLastError()); return;
    }
    DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
    if (!n || n >= MAX_PATH) { proxy_trace("runtime start: GetModuleFileName failed/too long: n=%lu error=%lu", n, GetLastError()); return; }
    proxy_trace("runtime start: proxy module=%p path=%s", (void*)self, path);
    char* slash = path;
    for (char* p = path; *p; ++p) if (*p == '\\') slash = p;
    if (slash == path || (size_t)(slash - path) > MAX_PATH - 24) { proxy_trace("runtime start: invalid/too long parent path"); return; }
    lstrcpyA(slash + 1, "touhou_hfr64.dll");
    proxy_trace("loading runtime: %s", path);
    HMODULE runtime = LoadLibraryA(path);
    if (!runtime) { proxy_trace("runtime LoadLibrary failed: error=%lu", GetLastError()); return; }
    proxy_trace("runtime loaded: module=%p", (void*)runtime);
    StartFn start = (StartFn)(void*)GetProcAddress(runtime, "hfr_start");
    if (start) {
        DWORD result = start(NULL);
        proxy_trace("hfr_start returned %lu (1=installed; otherwise see touhou_hfr.log)", result);
        (void)result;
    } else proxy_trace("runtime has no hfr_start export: error=%lu", GetLastError());
}

/* The names DxLib asks for. Each starts the runtime once, then calls the real factory --
   and if the real library is missing an entry point, the same E_NOTIMPL a direct call
   would have produced. */
typedef HRESULT (WINAPI *Factory)(REFIID, void**);
typedef HRESULT (WINAPI *Factory2)(UINT, REFIID, void**);
/* Kept apart from the slots the stubs jump through, which are redirected to the wrappers. */
static void* orig_factory  = (void*)proxy_absent;
static void* orig_factory1 = (void*)proxy_absent;
static void* orig_factory2 = (void*)proxy_absent;

static HRESULT WINAPI hfr_dxgi_factory(REFIID riid, void** out) {
    proxy_trace("CreateDXGIFactory intercepted");
    start_runtime(); return ((Factory)orig_factory)(riid, out);
}
static HRESULT WINAPI hfr_dxgi_factory1(REFIID riid, void** out) {
    proxy_trace("CreateDXGIFactory1 intercepted");
    start_runtime(); return ((Factory)orig_factory1)(riid, out);
}
static HRESULT WINAPI hfr_dxgi_factory2(UINT flags, REFIID riid, void** out) {
    proxy_trace("CreateDXGIFactory2 intercepted");
    start_runtime(); return ((Factory2)orig_factory2)(flags, riid, out);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        proxy_trace_init(instance);
        proxy_trace("diagnostic build 2026-09-16 attached: module=%p", (void*)instance);
        load_real(instance);
        /* Point the three factory stubs at the wrappers above rather than at the real
           library, so the generated stub still does the jumping and nothing else changes. */
        orig_factory  = real_CreateDXGIFactory;
        orig_factory1 = real_CreateDXGIFactory1;
        orig_factory2 = real_CreateDXGIFactory2;
        real_CreateDXGIFactory  = (void*)hfr_dxgi_factory;
        real_CreateDXGIFactory1 = (void*)hfr_dxgi_factory1;
        real_CreateDXGIFactory2 = (void*)hfr_dxgi_factory2;
        proxy_trace("factory hooks ready");
    }
    return TRUE;
}
