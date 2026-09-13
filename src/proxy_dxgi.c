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
    if (!n || n > MAX_PATH - 12) return;
    lstrcatA(path, "\\dxgi.dll");
    HMODULE found = LoadLibraryA(path);
    if (!found) return;
    if (found == (HMODULE)self) {
        OutputDebugStringA("Touhou HFR: this loader cannot tell the proxy from the system "
                           "dxgi.dll; no export was forwarded.");
        FreeLibrary(found);
        return;
    }
    real_dxgi = found;
#define RESOLVE(name) { FARPROC p = GetProcAddress(real_dxgi, #name); if (p) real_##name = (void*)p; }
    DXGI_EXPORTS(RESOLVE)
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
                            (LPCSTR)&start_runtime, &self)) return;
    DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    char* slash = path;
    for (char* p = path; *p; ++p) if (*p == '\\') slash = p;
    if (slash == path || (size_t)(slash - path) > MAX_PATH - 24) return;
    lstrcpyA(slash + 1, "touhou_hfr64.dll");
    HMODULE runtime = LoadLibraryA(path);
    if (!runtime) return;
    StartFn start = (StartFn)(void*)GetProcAddress(runtime, "hfr_start");
    if (start) start(NULL);
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
    start_runtime(); return ((Factory)orig_factory)(riid, out);
}
static HRESULT WINAPI hfr_dxgi_factory1(REFIID riid, void** out) {
    start_runtime(); return ((Factory)orig_factory1)(riid, out);
}
static HRESULT WINAPI hfr_dxgi_factory2(UINT flags, REFIID riid, void** out) {
    start_runtime(); return ((Factory2)orig_factory2)(flags, riid, out);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        load_real(instance);
        /* Point the three factory stubs at the wrappers above rather than at the real
           library, so the generated stub still does the jumping and nothing else changes. */
        orig_factory  = real_CreateDXGIFactory;
        orig_factory1 = real_CreateDXGIFactory1;
        orig_factory2 = real_CreateDXGIFactory2;
        real_CreateDXGIFactory  = (void*)hfr_dxgi_factory;
        real_CreateDXGIFactory1 = (void*)hfr_dxgi_factory1;
        real_CreateDXGIFactory2 = (void*)hfr_dxgi_factory2;
    }
    return TRUE;
}
