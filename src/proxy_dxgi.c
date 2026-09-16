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
 * On Proton, a temporary observer on the EXE's GetProcAddress import also catches
 * DxLib resolving a factory directly from the system DXGI module (see below).
 *
 * It is deliberately incurious. It does not know which game it is in, does not read the INI,
 * and does nothing at all if touhou_hfr64.dll is absent; the runtime checks the executable's
 * fingerprint itself and declines to patch anything it does not recognise.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "dxgi_exports.h"

/* Opt-in diagnostic build for failures before the runtime can create its own log.
   Preserve LastError, and report to both a separate file and Proton's +debugstr log.
   No tracing or extra file I/O is compiled into the ordinary release proxy. */
#ifdef HFR_PROXY_DIAGNOSTICS
#include <stdio.h>
#include <stdarg.h>
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
   a loader that does not would hand us ourselves, and every forwarded call would then
   jump straight back into its own stub. Wine 11 with native DXVK and the reported Proton
   Experimental build do load two distinct modules; do not assume this guard explains a
   Proton startup failure. The answer is still checked. If it is this
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
static void disarm_factory_lookup(void);
static void start_runtime(void) {
    disarm_factory_lookup();
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

/* Proton can load this proxy through D3D11's imports, then give DxLib the SYSTEM
   DXGI handle for its later bare-name load. None of our exports is called in that
   case. Observe only the EXE's GetProcAddress import until it resolves a real DXGI
   factory. The resolved address and LastError are returned unchanged; other modules'
   imports and DXGI's export table are untouched. Initialization still happens on
   the game's startup thread, after LoadLibrary/GetProcAddress have returned from
   the loader, before the game calls its factory. No worker thread or DllMain start.
   Chain to the previous IAT value, including another mod's hook, and remove our
   observation as soon as either startup path fires. */
typedef FARPROC (WINAPI *LookupFn)(HMODULE, LPCSTR);
static LookupFn previous_lookup;
static void** lookup_slot;
static FARPROC WINAPI hfr_factory_lookup(HMODULE module, LPCSTR name) {
    FARPROC result = previous_lookup(module, name);
    DWORD error = GetLastError();
    if (result && real_dxgi && module == real_dxgi && (ULONG_PTR)name > 0xffff &&
        (!strcmp(name, "CreateDXGIFactory") || !strcmp(name, "CreateDXGIFactory1") ||
         !strcmp(name, "CreateDXGIFactory2"))) {
        proxy_trace("system factory lookup intercepted: %s module=%p result=%p",
                    name, (void*)module, (void*)result);
        start_runtime();
    }
    SetLastError(error);
    return result;
}

/* Find by import name, not by a fixed RVA or resolved pointer: the EXE can be
   rebased, and a previous mod may already own the slot. This walks a loaded x64
   image, with the import arrays and names bounded by its SizeOfImage. */
static void** find_lookup_slot(void) {
    BYTE* base = (BYTE*)GetModuleHandleW(NULL);
    if (!base) return NULL;
    const IMAGE_DOS_HEADER* dos = (const void*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return NULL;
    const IMAGE_NT_HEADERS64* nt = (const void*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return NULL;
    DWORD size = nt->OptionalHeader.SizeOfImage;
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || dir.VirtualAddress >= size || dir.Size > size-dir.VirtualAddress) return NULL;
    for (size_t offset=0; offset+sizeof(IMAGE_IMPORT_DESCRIPTOR)<=dir.Size; offset+=sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const IMAGE_IMPORT_DESCRIPTOR* imp = (const void*)(base+dir.VirtualAddress+offset);
        if (!imp->Name) break;
        if (imp->Name > size-sizeof "kernel32.dll" ||
            _strnicmp((const char*)base+imp->Name, "kernel32.dll", sizeof "kernel32.dll")) continue;
        if (!imp->OriginalFirstThunk || !imp->FirstThunk ||
            imp->OriginalFirstThunk >= size || imp->FirstThunk >= size) continue;
        const IMAGE_THUNK_DATA64* names = (const void*)(base+imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA64* slots = (void*)(base+imp->FirstThunk);
        for (size_t i=0; i<(size-imp->OriginalFirstThunk)/sizeof *names &&
                         i<(size-imp->FirstThunk)/sizeof *slots; ++i) {
            ULONGLONG rva = names[i].u1.AddressOfData;
            if (!rva) break;
            if (IMAGE_SNAP_BY_ORDINAL64(rva) || rva > size-sizeof(WORD)-sizeof "GetProcAddress") continue;
            const IMAGE_IMPORT_BY_NAME* name = (const void*)(base+rva);
            if (!memcmp(name->Name, "GetProcAddress", sizeof "GetProcAddress"))
                return (void**)&slots[i].u1.Function;
        }
    }
    return NULL;
}
static int exchange_lookup(void* expected, void* replacement) {
    DWORD old, ignored;
    if (!VirtualProtect(lookup_slot, sizeof *lookup_slot, PAGE_READWRITE, &old)) return 0;
    void* found = InterlockedCompareExchangePointer(lookup_slot, replacement, expected);
    if (!VirtualProtect(lookup_slot, sizeof *lookup_slot, old, &ignored))
        proxy_trace("factory lookup: could not restore IAT page protection: error=%lu", GetLastError());
    return found == expected;
}
static void disarm_factory_lookup(void) {
    if (lookup_slot && *lookup_slot == (void*)hfr_factory_lookup &&
        exchange_lookup((void*)hfr_factory_lookup, (void*)previous_lookup))
        proxy_trace("factory lookup import restored");
}
static void arm_factory_lookup(void) {
    if (!real_dxgi) return;
    lookup_slot = find_lookup_slot();
    if (!lookup_slot || !*lookup_slot) { proxy_trace("factory lookup import not found"); return; }
    previous_lookup = (LookupFn)*lookup_slot;
    if (exchange_lookup((void*)previous_lookup, (void*)hfr_factory_lookup))
        proxy_trace("factory lookup import armed: slot=%p previous=%p", (void*)lookup_slot, (void*)previous_lookup);
    else { proxy_trace("factory lookup import could not be armed: error=%lu", GetLastError()); lookup_slot = NULL; }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        proxy_trace_init(instance);
        proxy_trace("factory-fallback build 2026-09-16 attached: module=%p", (void*)instance);
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
        arm_factory_lookup();
    }
    else if (reason == DLL_PROCESS_DETACH && !reserved) disarm_factory_lookup();
    return TRUE;
}
