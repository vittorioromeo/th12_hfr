/* Exercises the in-game menu on a real Direct3D 9 device, without the game.
   The runtime side of ui_api.h is stubbed, so this isolates the overlay itself:
   ImGui misuse shows up here as a reported failure instead of a silent crash in the game. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdarg.h>
#include "../src/ui/ui_api.h"

static int g_settings[UI_SETTING_COUNT] = { 1, 2, 1, 0, 1, 1, 1 };
static const char* g_names[] = { "nearest", "bilinear", "sharp-bilinear", "mmpx", "xbr-lv2" };
static int g_failed;

int         hfr_ui_get(int id) { return (id >= 0 && id < UI_SETTING_COUNT) ? g_settings[id] : 0; }
void        hfr_ui_set(int id, int v) { if (id >= 0 && id < UI_SETTING_COUNT) g_settings[id] = v; }
int         hfr_ui_filter_count(void) { return 5; }
const char* hfr_ui_filter_name(int i) { return (i >= 0 && i < 5) ? g_names[i] : ""; }
int         hfr_ui_filter_is_fixed_scale(int i) { return i == 3; }
void        hfr_ui_save(void) { printf("  (save called)\n"); }
void        hfr_ui_status(char* b, int n) { snprintf(b, (size_t)n, "TH12 v1.00b   1280 x 960 window   360 Hz logic / 360 Hz present"); }
void        hfr_ui_scale_info(char* b, int n) { snprintf(b, (size_t)n, "640x480 game image at 2x, 0 x 0 of black bars"); }
int         hfr_ui_menu_key(void) { return VK_F11; }
void        hfr_menu_requested(void) { hfr_menu_toggle(); }   /* the runtime acts on this per frame */
/* The runtime turns the key's level into a press; here the harness just mirrors that. */
static int  g_key_down;
void        hfr_menu_key_down(int down) {
    if (down && !g_key_down) hfr_menu_toggle();
    g_key_down = down ? 1 : 0;
}
int         hfr_ui_simulation_locked(void) { return 0; }
const char* hfr_ui_present_path(void) { return "our own swap chain"; }
static const char* g_systems[] = { "BulletManager", "Player", "Bomb", "LaserManager",
                                   "ItemManager", "Gui", "Stage", "AnmManagerWorld", "AnmManagerUI" };
static int g_system_on[9] = { 1, 1, 0, 1, 1, 0, 1, 1, 1 };
int         hfr_ui_system_count(void) { return 9; }
const char* hfr_ui_system_name(int i) { return (i >= 0 && i < 9) ? g_systems[i] : ""; }
int         hfr_ui_system_get(int i) { return (i >= 0 && i < 9) ? g_system_on[i] : 0; }
void        hfr_ui_system_set(int i, int v) { if (i >= 0 && i < 9) g_system_on[i] = v; }
void        hfr_ui_report(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); printf("REPORT: "); vprintf(fmt, ap); printf("\n"); va_end(ap); g_failed = 1;
}

static LRESULT CALLBACK wnd(HWND h, UINT m, WPARAM w, LPARAM l) {
    LRESULT r = 0;
    if (hfr_menu_wndproc(h, m, w, l, &r)) return r;
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}
int main(void) {
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wnd; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "hfrmenu";
    RegisterClassA(&wc);
    HWND h = CreateWindowExA(0, "hfrmenu", "menu test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             0, 0, 1280, 960, NULL, NULL, wc.hInstance, NULL);
    if (!h) { printf("FAIL: no window\n"); return 2; }

    /* Match how the patch actually sets the device up: Direct3D 9Ex, a swap chain of our
       own to present through, a render target standing in for the game's back buffer, and a
       D3DSBT_ALL block captured around the overlay -- the integration, not just the widgets. */
    IDirect3D9Ex* d3dex = NULL;
    if (FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION, &d3dex)) || !d3dex) { printf("FAIL: no Direct3D 9Ex\n"); return 2; }
    IDirect3D9* d3d = (IDirect3D9*)d3dex;
    D3DPRESENT_PARAMETERS pp; memset(&pp, 0, sizeof pp);
    pp.BackBufferWidth = 1280; pp.BackBufferHeight = 960; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = h;
    pp.Windowed = TRUE; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    pp.BackBufferWidth = 640; pp.BackBufferHeight = 480;   /* the game's own size */
    pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    IDirect3DDevice9Ex* devex = NULL;
    HRESULT hr = d3dex->lpVtbl->CreateDeviceEx(d3dex, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, h,
                                               D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, NULL, &devex);
    if (FAILED(hr) || !devex) { printf("FAIL: CreateDeviceEx 0x%08lx\n", (long)hr); return 2; }
    IDirect3DDevice9* dev = (IDirect3DDevice9*)devex;

    D3DPRESENT_PARAMETERS sp; memset(&sp, 0, sizeof sp);
    sp.BackBufferWidth = 1280; sp.BackBufferHeight = 960; sp.BackBufferFormat = D3DFMT_X8R8G8B8;
    sp.BackBufferCount = 1; sp.SwapEffect = D3DSWAPEFFECT_DISCARD; sp.hDeviceWindow = h;
    sp.Windowed = TRUE; sp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DSwapChain9* chain = NULL;
    hr = dev->lpVtbl->CreateAdditionalSwapChain(dev, &sp, &chain);
    if (FAILED(hr)) { printf("FAIL: CreateAdditionalSwapChain 0x%08lx\n", (long)hr); return 2; }
    IDirect3DSurface9* chain_bb = NULL;
    chain->lpVtbl->GetBackBuffer(chain, 0, D3DBACKBUFFER_TYPE_MONO, &chain_bb);

    IDirect3DTexture9* rt = NULL; IDirect3DSurface9* rt_surf = NULL; IDirect3DSurface9* rt_ds = NULL;
    dev->lpVtbl->CreateTexture(dev, 640, 480, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &rt, NULL);
    if (rt) rt->lpVtbl->GetSurfaceLevel(rt, 0, &rt_surf);
    dev->lpVtbl->CreateDepthStencilSurface(dev, 640, 480, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &rt_ds, NULL);
    if (rt_surf) { dev->lpVtbl->SetRenderTarget(dev, 0, rt_surf); dev->lpVtbl->SetDepthStencilSurface(dev, rt_ds); }
    IDirect3DStateBlock9* block = NULL;
    dev->lpVtbl->CreateStateBlock(dev, D3DSBT_ALL, &block);
    printf("9Ex device, additional swap chain, render target and state block ready\n");

    if (!hfr_menu_init(dev, h)) { printf("FAIL: hfr_menu_init\n"); return 2; }
    printf("menu initialised\n");

    /* a few frames with only the startup hint, then the menu itself */
    for (int frame = 0; frame < 240; ++frame) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (frame == 30) { hfr_menu_toggle(); printf("menu toggled -> %d\n", hfr_menu_visible()); }
        /* the "game" draws into its render target */
        dev->lpVtbl->Clear(dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(20, 20, 40), 1.0f, 0);
        /* then the patch presents it, with the overlay on top, exactly as scaler_blit does */
        if (block) block->lpVtbl->Capture(block);
        if (SUCCEEDED(dev->lpVtbl->BeginScene(dev))) {
            dev->lpVtbl->SetRenderTarget(dev, 0, chain_bb);
            dev->lpVtbl->SetDepthStencilSurface(dev, NULL);
            D3DVIEWPORT9 vp = { 0, 0, 1280, 960, 0.0f, 1.0f };
            dev->lpVtbl->SetViewport(dev, &vp);
            dev->lpVtbl->Clear(dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
            hfr_menu_render(dev, 1280, 960);
            dev->lpVtbl->EndScene(dev);
            if (rt_surf) { dev->lpVtbl->SetRenderTarget(dev, 0, rt_surf); dev->lpVtbl->SetDepthStencilSurface(dev, rt_ds); }
        }
        if (block) block->lpVtbl->Apply(block);
        chain->lpVtbl->Present(chain, NULL, NULL, NULL, NULL, 0);
    }
    printf(g_failed ? "FAIL: the overlay reported a problem\n" : "PASS: menu rendered 240 frames (hint and window)\n");
    hfr_menu_shutdown();
    return g_failed;
}
