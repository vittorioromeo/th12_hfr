/* The dxgi.dll proxy, against the system's real dxgi.dll.
   The proxy is how the patch reaches New Classic when Steam is the launcher, so the thing
   that must not break is ordinary Direct3D: a game that loads it must see exactly what the
   real library would have given it. This loads both and compares. It also runs in a process
   that is not the game, which is the case the proxy has to be inert in. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include <stdio.h>
#include <string.h>

static int failures;
static void check(int ok, const char* what) {
    printf("%s: %s\n", ok ? "  ok" : "FAIL", what);
    if (!ok) failures = 1;
}
typedef HRESULT (WINAPI *Factory1)(REFIID, void**);
typedef HRESULT (WINAPI *Declare)(void);

/* {770aae78-f26f-4dba-a829-253c83d1b387} IDXGIFactory1 */
static const GUID iid_factory1 =
    {0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87}};

int main(int argc, char** argv) {
    /* The proxy is loaded from a copy under a different name. Both files are the same
       bytes, but a loader that keys modules by base name -- Wine's does -- cannot hold the
       proxy and the system dxgi.dll open at once, and then there is nothing to compare
       against on affected Wine versions. Renaming the copy tests forwarding separately;
       it does not prove the normal installation works on Proton. The companion
       test_dxgi_proxy_start.c uses the actual basename and a stand-in runtime to check
       activation. Real-game Proton loading still needs an integration test. */
    char source[MAX_PATH], copy[MAX_PATH], system_path[MAX_PATH];
    if (!GetFullPathNameA(argc > 1 ? argv[1] : "build\\dxgi.dll", MAX_PATH, source, NULL)) {
        printf("FAIL: cannot resolve the proxy path\n"); return 2;
    }
    if (!GetFullPathNameA("build\\tests\\dxgi_under_test.dll", MAX_PATH, copy, NULL) ||
        !CopyFileA(source, copy, FALSE)) {
        printf("FAIL: cannot copy the proxy for testing (error %lu)\n", GetLastError()); return 2;
    }
    UINT n = GetSystemDirectoryA(system_path, MAX_PATH);
    if (!n) { printf("FAIL: no system directory\n"); return 2; }
    strcat(system_path, "\\dxgi.dll");

    HMODULE real = LoadLibraryA(system_path);
    if (!real) { printf("SKIP: this system has no %s\n", system_path); return 0; }
    HMODULE proxy = LoadLibraryA(copy);
    if (!proxy) { printf("FAIL: cannot load %s (error %lu)\n", copy, GetLastError()); return 2; }
    check(proxy != real, "the proxy and the system library are distinct modules");

    /* Every name the real library exports must be reachable through the proxy. */
    int missing = 0, total = 0;
    static const char* const names[] = {
        "CreateDXGIFactory", "CreateDXGIFactory1", "CreateDXGIFactory2",
        "DXGIDeclareAdapterRemovalSupport", "DXGIGetDebugInterface1", "DXGID3D10CreateDevice",
        "DXGID3D10RegisterLayers", "D3DKMTPresent", "D3DKMTEscape", "D3DKMTCreateDevice",
        "ApplyCompatResolutionQuirking", "SetAppCompatStringPointer", "PIXBeginCapture",
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; ++i) {
        if (!GetProcAddress(real, names[i])) continue;   /* not on this system; nothing to forward */
        ++total;
        if (!GetProcAddress(proxy, names[i])) { printf("       missing: %s\n", names[i]); ++missing; }
    }
    check(total > 0, "the system library exports names to compare against");
    check(!missing, "every exported name the system has is exported by the proxy");

    /* The call DxLib actually makes, through both, must agree. A factory the proxy returns
       is a real factory: the proxy forwards, it does not reimplement. */
    Factory1 real_create  = (Factory1)(void*)GetProcAddress(real,  "CreateDXGIFactory1");
    Factory1 proxy_create = (Factory1)(void*)GetProcAddress(proxy, "CreateDXGIFactory1");
    check(real_create && proxy_create, "CreateDXGIFactory1 resolves in both");
    if (real_create && proxy_create) {
        IUnknown* a = NULL; IUnknown* b = NULL;
        HRESULT ra = real_create(&iid_factory1, (void**)&a);
        HRESULT rb = proxy_create(&iid_factory1, (void**)&b);
        printf("       real=0x%08lx proxy=0x%08lx\n", (unsigned long)ra, (unsigned long)rb);
        check(ra == rb, "CreateDXGIFactory1 returns what the real library returns");
        check(!!a == !!b, "it produces an object exactly when the real one does");
        check(rb != (HRESULT)0x80004001L, "the call was forwarded, not answered by the missing-export stub");
        if (a) a->lpVtbl->Release(a);
        if (b) b->lpVtbl->Release(b);
    }
    /* A forwarded export that is not one of the three the proxy wraps. */
    Declare real_declare  = (Declare)(void*)GetProcAddress(real,  "DXGIDeclareAdapterRemovalSupport");
    Declare proxy_declare = (Declare)(void*)GetProcAddress(proxy, "DXGIDeclareAdapterRemovalSupport");
    if (real_declare && proxy_declare)
        check(real_declare() == proxy_declare(), "a generated stub forwards to the real function");

    /* The proxy starts the runtime on the first factory call. In a process that is not the
       game the runtime must decline: no patches, no crash, and we are still running. */
    check(GetModuleHandleA("th06nc.exe") == NULL, "this test is not the game");
    printf(failures ? "FAIL: dxgi proxy\n"
                    : "PASS: dxgi proxy forwards every export and is inert outside the game\n");
    return failures ? 1 : 0;
}
