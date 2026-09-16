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
static const GUID iid_factory1 =
    {0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87}};

int main(int argc, char** argv) {
    if (argc != 2 || (strcmp(argv[1], "CreateDXGIFactory") &&
                      strcmp(argv[1], "CreateDXGIFactory1") &&
                      strcmp(argv[1], "CreateDXGIFactory2"))) return 2;
    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, sizeof path)) return 2;
    char* slash = strrchr(path, '\\');
    if (!slash || (size_t)(slash-path) > MAX_PATH-12) return 2;
    strcpy(slash+1, "dxgi.dll");
    HMODULE proxy = LoadLibraryA(path);
    if (!proxy) { printf("FAIL: proxy load error %lu\n", GetLastError()); return 1; }
    if (GetModuleHandleA("touhou_hfr64.dll")) {
        puts("FAIL: runtime loaded before a factory call"); return 1;
    }
    FARPROC first = GetProcAddress(proxy, argv[1]);
    if (!first) { puts("FAIL: factory export missing"); return 1; }
    IUnknown* object = NULL;
    HRESULT hr = !strcmp(argv[1], "CreateDXGIFactory2")
        ? ((Factory2)(void*)first)(0, &iid_factory1, (void**)&object)
        : ((Factory)(void*)first)(&iid_factory1, (void**)&object);
    if (object) object->lpVtbl->Release(object);
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
    puts("PASS: real-name proxy initializes the runtime only on the first factory call");
    return 0;
}
#endif
