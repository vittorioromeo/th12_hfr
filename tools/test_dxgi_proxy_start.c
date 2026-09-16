/* Build once as a stand-in touhou_hfr64.dll (-DPROXY_TEST_RUNTIME -shared),
   and once as an EXE. Run in an isolated directory containing that DLL and
   the actual dxgi.dll proxy. No game or real HFR runtime is involved. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef PROXY_TEST_RUNTIME
static LONG starts;
__declspec(dllexport) DWORD WINAPI hfr_start(void* unused) {
    (void)unused;
    return (DWORD)InterlockedIncrement(&starts);
}
__declspec(dllexport) DWORD WINAPI test_start_count(void) { return (DWORD)starts; }
BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved) {
    (void)self; (void)reason; (void)reserved;
    return TRUE;
}
#else
#include <unknwn.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *Factory)(REFIID, void**);
typedef HRESULT (WINAPI *Factory2)(UINT, REFIID, void**);
typedef DWORD (WINAPI *Count)(void);
typedef FARPROC (WINAPI *Lookup)(HMODULE, LPCSTR);
static const GUID iid_factory1 =
    {0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87}};

/* Always read the EXE's current IAT slot, even with optimization enabled. */
static __attribute__((noinline)) FARPROC lookup(HMODULE module, LPCSTR name) {
    return GetProcAddress(module, name);
}
extern Lookup __imp_GetProcAddress;
static Lookup current_lookup(void) { return *(Lookup volatile*)&__imp_GetProcAddress; }
static Lookup before_test_hook;
static unsigned test_hook_calls;
static FARPROC WINAPI test_lookup_hook(HMODULE module, LPCSTR name) {
    ++test_hook_calls;
    return before_test_hook(module, name);
}
/* Same factory name in another module must not trigger HFR. */
__declspec(dllexport) void CreateDXGIFactory2(void) {}
static int same_lookup(Lookup original, HMODULE module, LPCSTR name) {
    SetLastError(0x12345678);
    FARPROC expected = original(module, name);
    DWORD expected_error = GetLastError();
    SetLastError(0x12345678);
    FARPROC actual = lookup(module, name);
    DWORD actual_error = GetLastError();
    if (actual != expected || actual_error != expected_error) {
        printf("FAIL: lookup changed result/error: %p/%lu vs %p/%lu\n",
               (void*)actual, actual_error, (void*)expected, expected_error);
        return 0;
    }
    return 1;
}

int main(int argc, char** argv) {
    if ((argc != 2 && argc != 3) || (argc==3 && strcmp(argv[2], "--game-order") &&
                                   strcmp(argv[2], "--system-factory") && strcmp(argv[2], "--unload")) ||
                     (strcmp(argv[1], "CreateDXGIFactory") &&
                      strcmp(argv[1], "CreateDXGIFactory1") &&
                      strcmp(argv[1], "CreateDXGIFactory2"))) return 2;
    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, sizeof path)) return 2;
    char* slash = strrchr(path, '\\');
    if (!slash || (size_t)(slash-path) > MAX_PATH-12) return 2;
    strcpy(slash+1, "dxgi.dll");
    Lookup original_lookup = current_lookup();
    if (argc==3 && !strcmp(argv[2], "--system-factory")) {
        /* Model an earlier mod's GetProcAddress hook. The proxy must chain to it,
           preserve its return value/error and restore it after initialization. */
        DWORD old, ignored;
        before_test_hook = original_lookup;
        if (!VirtualProtect(&__imp_GetProcAddress, sizeof __imp_GetProcAddress, PAGE_READWRITE, &old)) return 2;
        __imp_GetProcAddress = test_lookup_hook;
        if (!VirtualProtect(&__imp_GetProcAddress, sizeof __imp_GetProcAddress, old, &ignored)) return 2;
        original_lookup = test_lookup_hook;
    }
    HMODULE proxy;
    if (argc==3 && !strcmp(argv[2], "--game-order")) {
        /* DxLib loads D3D11 before requesting DXGI by bare name. With DXVK,
           D3D11's imports can load the proxy first, and its DllMain loads the
           system DLL of the same basename. Preserve that order exactly. */
        HMODULE d3d11 = LoadLibraryA("d3d11.dll");
        proxy = LoadLibraryA("dxgi.dll");
        char actual[MAX_PATH] = "";
        if (proxy) GetModuleFileNameA(proxy, actual, sizeof actual);
        printf("Game order: D3D11=%p DXGI=%p (%s)\n", (void*)d3d11, (void*)proxy, actual);
        if (!d3d11) { puts("FAIL: D3D11 prerequisite not loaded"); return 1; }
    } else proxy = LoadLibraryA(path);
    if (!proxy) { printf("FAIL: proxy load error %lu\n", GetLastError()); return 1; }
    if (argc==3 && !strcmp(argv[2], "--unload")) {
        if (current_lookup()==original_lookup) { puts("FAIL: lookup import was never armed"); return 1; }
        if (!FreeLibrary(proxy) || current_lookup()!=original_lookup || GetModuleHandleA("touhou_hfr64.dll")) {
            puts("FAIL: unloading the unused proxy did not restore the lookup import"); return 1;
        }
        puts("PASS: unloading before a factory lookup restores the original import");
        return 0;
    }
    HMODULE factory_module = proxy;
    if (argc==3 && !strcmp(argv[2], "--system-factory")) {
        /* Reproduce the reported bypass deterministically on Windows too: resolve
           the factory from System32, never from the game-folder proxy. */
        char system_path[MAX_PATH];
        UINT n = GetSystemDirectoryA(system_path, sizeof system_path);
        if (!n || n > MAX_PATH-12) return 2;
        strcat(system_path, "\\dxgi.dll");
        factory_module = LoadLibraryA(system_path);
        if (!factory_module || factory_module==proxy) return 2;
        /* Non-factories, failed names and ordinal lookups must stay transparent. */
        HMODULE kernel = GetModuleHandleA("kernel32.dll");
        if (!same_lookup(original_lookup, kernel, "GetCurrentProcessId") ||
            !same_lookup(original_lookup, kernel, "CreateDXGIFactory2") ||
            !same_lookup(original_lookup, GetModuleHandleA(NULL), "CreateDXGIFactory2") ||
            !same_lookup(original_lookup, factory_module, "hfr_deliberately_missing_export") ||
            !same_lookup(original_lookup, factory_module, (LPCSTR)(ULONG_PTR)65535)) return 1;
    }
    if (GetModuleHandleA("touhou_hfr64.dll")) {
        puts("FAIL: runtime loaded before a factory call"); return 1;
    }
    SetLastError(0x12345678);
    FARPROC expected_factory = original_lookup(factory_module, argv[1]);
    DWORD expected_error = GetLastError();
    test_hook_calls = 0;
    SetLastError(0x12345678);
    FARPROC first = lookup(factory_module, argv[1]);
    DWORD actual_error = GetLastError();
    if (!first) { puts("FAIL: factory export missing"); return 1; }
    if (first!=expected_factory || actual_error!=expected_error) {
        puts("FAIL: factory lookup changed the function pointer or LastError"); return 1;
    }
    if (factory_module!=proxy && test_hook_calls!=1) {
        puts("FAIL: the previous lookup hook was bypassed"); return 1;
    }
    if (factory_module!=proxy && !GetModuleHandleA("touhou_hfr64.dll")) {
        puts("FAIL: resolving a system DXGI factory bypassed runtime initialization"); return 1;
    }
    IUnknown* object = NULL;
    HRESULT hr = !strcmp(argv[1], "CreateDXGIFactory2")
        ? ((Factory2)(void*)first)(0, &iid_factory1, (void**)&object)
        : ((Factory)(void*)first)(&iid_factory1, (void**)&object);
    if (object) object->lpVtbl->Release(object);
    if (current_lookup()!=original_lookup) {
        puts("FAIL: startup did not restore the previous lookup import"); return 1;
    }
    printf("First factory %s returned 0x%08lx\n", argv[1], (unsigned long)hr);
    HMODULE runtime = GetModuleHandleA("touhou_hfr64.dll");
    Count count = runtime ? (Count)(void*)GetProcAddress(runtime, "test_start_count") : NULL;
    if (!count || count() != 1) {
        puts("FAIL: first factory did not load and initialize the runtime exactly once"); return 1;
    }
    const char* names[] = {"CreateDXGIFactory", "CreateDXGIFactory1", "CreateDXGIFactory2"};
    for (unsigned i=0; i<3; ++i) {
        FARPROC f = GetProcAddress(proxy, names[i]);
        if (!f) return 1;
        object = NULL;
        if (i==2) ((Factory2)(void*)f)(0, &iid_factory1, (void**)&object);
        else ((Factory)(void*)f)(&iid_factory1, (void**)&object);
        if (object) object->lpVtbl->Release(object);
    }
    if (count() != 1) { puts("FAIL: later factories started the runtime again"); return 1; }
    puts("PASS: runtime initializes once, with transparent lookup and import restoration");
    return 0;
}
#endif
