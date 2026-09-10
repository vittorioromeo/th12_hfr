/* ------------------------------------------------------------------ Direct3D hooks */
typedef HRESULT (__stdcall *CreateDeviceFn)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT (__stdcall *ResetFn)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef IDirect3D9* (__stdcall *Direct3DCreate9Fn)(UINT);
static CreateDeviceFn orig_CreateDevice; static ResetFn orig_Reset; static Direct3DCreate9Fn orig_Direct3DCreate9;

static HWND g_device_window;
static HWND device_window(const D3DPRESENT_PARAMETERS* pp) {
    if (pp && pp->hDeviceWindow) return pp->hDeviceWindow;
    return g_device_window;
}
static int detect_refresh(IDirect3DDevice9* dev) {
    int hz = 0;
    if (dev) { D3DDISPLAYMODE m; if (SUCCEEDED(dev->lpVtbl->GetDisplayMode(dev, 0, &m)) && m.RefreshRate > 0) hz = m.RefreshRate; }
    if (hz <= 1) { DEVMODEA dm; memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
        if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1) hz = dm.dmDisplayFrequency; }
    if (hz <= 1) hz = 60;
    return hz;
}
/* Direct3D 9Ex: the device is created through IDirect3D9Ex::CreateDeviceEx so that the present queue depth can be
   limited (SetMaximumFrameLatency), which removes up to two frames of display latency. 9Ex has no managed pool,
   so managed resources are turned into default-pool dynamic ones (device CreateTexture/CreateVertexBuffer/
   CreateIndexBuffer and the D3DX texture loaders the game imports). */
static int g_using_ex = 0;
typedef HRESULT (WINAPI *Direct3DCreate9ExFn)(UINT, IDirect3D9Ex**);
static void patch_vtable(void** vt, int idx, void* hook, void** orig) {
    if (*orig) return;
    DWORD old;
    if (!VirtualProtect(&vt[idx],4,PAGE_EXECUTE_READWRITE,&old)) {LOG("Cannot patch D3D vtable slot %d",idx);return;}
    *orig=vt[idx];vt[idx]=hook;VirtualProtect(&vt[idx],4,old,&old);
}
typedef HRESULT (__stdcall *CreateTextureFn)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
typedef HRESULT (__stdcall *CreateVertexBufferFn)(IDirect3DDevice9*, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9**, HANDLE*);
typedef HRESULT (__stdcall *CreateIndexBufferFn)(IDirect3DDevice9*, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer9**, HANDLE*);
static CreateTextureFn orig_CreateTexture; static CreateVertexBufferFn orig_CreateVertexBuffer; static CreateIndexBufferFn orig_CreateIndexBuffer;
static unsigned g_stat_managed_conv;
static inline void unmanage(D3DPOOL* pool, DWORD* usage) { if (*pool == D3DPOOL_MANAGED) { *pool = D3DPOOL_DEFAULT; *usage |= D3DUSAGE_DYNAMIC; g_stat_managed_conv++; } }
static HRESULT __stdcall hook_CreateTexture(IDirect3DDevice9* dev, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture9** out, HANDLE* sh) {
    unmanage(&pool, &usage); return orig_CreateTexture(dev, w, h, levels, usage, fmt, pool, out, sh);
}
static HRESULT __stdcall hook_CreateVertexBuffer(IDirect3DDevice9* dev, UINT len, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9** out, HANDLE* sh) {
    unmanage(&pool, &usage); return orig_CreateVertexBuffer(dev, len, usage, fvf, pool, out, sh);
}
static HRESULT __stdcall hook_CreateIndexBuffer(IDirect3DDevice9* dev, UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer9** out, HANDLE* sh) {
    unmanage(&pool, &usage); return orig_CreateIndexBuffer(dev, len, usage, fmt, pool, out, sh);
}
typedef HRESULT (__stdcall *D3DXCreateTextureFn)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**);
typedef HRESULT (__stdcall *D3DXCreateTextureFromFileInMemoryExFn)(IDirect3DDevice9*, LPCVOID, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, DWORD, DWORD, D3DCOLOR, void*, PALETTEENTRY*, IDirect3DTexture9**);
static D3DXCreateTextureFn orig_D3DXCreateTexture; static D3DXCreateTextureFromFileInMemoryExFn orig_D3DXCreateTextureFromFileInMemoryEx;
static HRESULT __stdcall hook_D3DXCreateTexture(IDirect3DDevice9* dev, UINT w, UINT h, UINT mip, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture9** out) {
    if (g_using_ex) unmanage(&pool, &usage);
    return orig_D3DXCreateTexture(dev, w, h, mip, usage, fmt, pool, out);
}
static HRESULT __stdcall hook_D3DXCreateTextureFromFileInMemoryEx(IDirect3DDevice9* dev, LPCVOID src, UINT srcsize, UINT w, UINT h, UINT mip, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, DWORD filter, DWORD mipfilter, D3DCOLOR key, void* info, PALETTEENTRY* pal, IDirect3DTexture9** out) {
    if (g_using_ex) unmanage(&pool, &usage);
    return orig_D3DXCreateTextureFromFileInMemoryEx(dev, src, srcsize, w, h, mip, usage, fmt, pool, filter, mipfilter, key, info, pal, out);
}
static int hook_iat(const char* dll, const char* func, void* hook, void** orig);

static void apply_pp(D3DPRESENT_PARAMETERS* pp) {
    pp->PresentationInterval = cfg.vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    if (!pp->Windowed) {
        int hz = cfg.fullscreen_refresh ? cfg.fullscreen_refresh : (cfg.fps > 0 ? cfg.fps : detect_refresh(NULL));
        pp->FullScreen_RefreshRateInHz = hz > 0 ? hz : D3DPRESENT_RATE_DEFAULT;
    }
    if (g_using_ex) {
        if (pp->Windowed && cfg.flipex) { pp->SwapEffect = D3DSWAPEFFECT_FLIPEX; if (pp->BackBufferCount < 2) pp->BackBufferCount = 2; }
        else if (pp->SwapEffect == D3DSWAPEFFECT_FLIPEX) { pp->SwapEffect = D3DSWAPEFFECT_DISCARD; }
    }
    LOG("present params: windowed=%d %ux%u refresh=%u interval=0x%x backbuffers=%u swap=%d", pp->Windowed, pp->BackBufferWidth,
        pp->BackBufferHeight, pp->FullScreen_RefreshRateInHz, pp->PresentationInterval, pp->BackBufferCount, pp->SwapEffect);
}
static void fill_mode_ex(D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* m) {
    memset(m, 0, sizeof *m); m->Size = sizeof *m; m->Width = pp->BackBufferWidth; m->Height = pp->BackBufferHeight;
    m->RefreshRate = pp->FullScreen_RefreshRateInHz; m->Format = pp->BackBufferFormat; m->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
}
static void after_device(IDirect3DDevice9* dev) {
    int hz = detect_refresh(dev);
    g_display_hz = hz;
    LOG("display refresh detected: %d Hz", hz);
    recompute_rate(cfg.fps > 0 ? cfg.fps : hz);
    g_next = 0;
    if (g_using_ex && cfg.max_frame_latency > 0) {
        IDirect3DDevice9Ex* ex = (IDirect3DDevice9Ex*)dev;
        HRESULT hr = ex->lpVtbl->SetMaximumFrameLatency(ex, (UINT)cfg.max_frame_latency);
        UINT got = 0; ex->lpVtbl->GetMaximumFrameLatency(ex, &got);
        LOG("SetMaximumFrameLatency(%d) -> 0x%08lx (now %u)", cfg.max_frame_latency, (long)hr, got);
    }
}
/* The game resets the device to change mode or window size. Both are ours now, and an
   actual reset would destroy every texture it owns (see scaler_set_output), so when we are
   presenting through our own chain the reset is answered without touching the device: the
   window work the game does around it still happens, and the new client size arrives as a
   WM_SIZE, which rebuilds our chain. */
static HRESULT __stdcall hook_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    apply_pp(pp);
    if (g_own_present) {
        HWND hwnd = device_window(pp);
        int cw, ch; client_size(hwnd, &cw, &ch);
        LOG("Reset answered without resetting the device (client %dx%d)", cw, ch);
        if (cw > 0 && ch > 0 && (cw != g_out_w || ch != g_out_h)) scaler_set_output(dev, hwnd, cw, ch);
        return D3D_OK;
    }
    D3DPRESENT_PARAMETERS use; scaler_adjust_pp(&use, pp, device_window(pp), 0, 0);
    hfr_menu_invalidate();
    scaler_release();
    HRESULT hr;
    if (g_using_ex) {
        IDirect3DDevice9Ex* ex = (IDirect3DDevice9Ex*)dev; D3DDISPLAYMODEEX m; fill_mode_ex(&use, &m);
        hr = ex->lpVtbl->ResetEx(ex, &use, use.Windowed ? NULL : &m);
        LOG("ResetEx %ux%u -> 0x%08lx", use.BackBufferWidth, use.BackBufferHeight, (long)hr);
    } else {
        hr = orig_Reset(dev, &use);
        LOG("Reset %ux%u -> 0x%08lx", use.BackBufferWidth, use.BackBufferHeight, (long)hr);
    }
    if (SUCCEEDED(hr)) { scaler_create(dev, &use, device_window(pp)); after_device(dev); }
    else {
        /* The chain is untouched, but our render target is gone and the game is about to
           draw into it. Rebuild against the size the chain actually still has. */
        D3DPRESENT_PARAMETERS keep = use;
        IDirect3DSurface9* bb = NULL;
        if (SUCCEEDED(orig_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
            D3DSURFACE_DESC d;
            if (SUCCEEDED(bb->lpVtbl->GetDesc(bb, &d))) { keep.BackBufferWidth = d.Width; keep.BackBufferHeight = d.Height; }
            bb->lpVtbl->Release(bb);
        }
        scaler_create(dev, &keep, device_window(pp));
    }
    return hr;
}
static HRESULT __stdcall hook_CreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND hwnd, DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    apply_pp(pp);
    g_device_window = hwnd ? hwnd : (pp ? pp->hDeviceWindow : NULL);
    D3DPRESENT_PARAMETERS use; scaler_adjust_pp(&use, pp, g_device_window, 0, 0);
    HRESULT hr;
    if (g_using_ex) {
        IDirect3D9Ex* ex = (IDirect3D9Ex*)d3d; D3DDISPLAYMODEEX m; fill_mode_ex(&use, &m);
        hr = ex->lpVtbl->CreateDeviceEx(ex, adapter, type, hwnd, flags, &use, use.Windowed ? NULL : &m, (IDirect3DDevice9Ex**)out);
        LOG("CreateDeviceEx %ux%u -> 0x%08lx", use.BackBufferWidth, use.BackBufferHeight, (long)hr);
        if (FAILED(hr)) { hr = orig_CreateDevice(d3d, adapter, type, hwnd, flags, &use, out); LOG("fallback CreateDevice on the 9Ex object -> 0x%08lx", (long)hr); }
        if (SUCCEEDED(hr) && out && *out) {
            static const GUID iid_dev9ex = { 0xb18b10ce, 0x2649, 0x405a, { 0x87, 0x0f, 0x95, 0xf7, 0x77, 0xd4, 0x31, 0x3a } };
            void* q = NULL;
            if (SUCCEEDED((*out)->lpVtbl->QueryInterface(*out, &iid_dev9ex, &q)) && q) { ((IUnknown*)q)->lpVtbl->Release((IUnknown*)q); }
            else { LOG("device does not expose IDirect3DDevice9Ex; 9Ex features disabled"); g_using_ex = 0; }
            if (hwnd) SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);   /* 9Ex resets the window style */
        }
    } else {
        hr = orig_CreateDevice(d3d, adapter, type, hwnd, flags, &use, out);
        LOG("CreateDevice %ux%u -> 0x%08lx", use.BackBufferWidth, use.BackBufferHeight, (long)hr);
    }
    if (SUCCEEDED(hr) && out && *out) {
        IDirect3DDevice9* dev = *out;
        void** vt = *(void***)dev;
        patch_vtable(vt, 16, (void*)hook_Reset, (void**)&orig_Reset);
        patch_vtable(vt, 17, (void*)hook_Present, (void**)&orig_Present);
        patch_vtable(vt, 18, (void*)hook_GetBackBuffer, (void**)&orig_GetBackBuffer);
        if (g_using_ex) {
            patch_vtable(vt, 23, (void*)hook_CreateTexture, (void**)&orig_CreateTexture);
            patch_vtable(vt, 26, (void*)hook_CreateVertexBuffer, (void**)&orig_CreateVertexBuffer);
            patch_vtable(vt, 27, (void*)hook_CreateIndexBuffer, (void**)&orig_CreateIndexBuffer);
        }
        window_attach(g_device_window);
        scaler_create(dev, &use, g_device_window);
        if (!hfr_menu_init(dev, g_device_window)) LOG("menu: unavailable");
        else LOG("menu: ready (open with virtual key 0x%02x)", cfg.menu_key);
        after_device(dev);
    }
    return hr;
}
/* A d3d9.dll sitting in the game folder (a rotation or compatibility wrapper) replaces the
   real one for the whole process. Such a wrapper only ever expects the single implicit swap
   chain: PivotDX9 accepts CreateAdditionalSwapChain and then never returns from Present on
   it. So when the loaded d3d9 is not the system one we present through the device's own
   chain and resize it with Reset -- which in turn means Direct3D 9Ex has to go, because 9Ex
   forces the game's textures into the default pool where a reset destroys them and the game
   has no code to reload them. */
static void detect_d3d9_wrapper(void) {
    if (cfg.own_present >= 0) {                     /* forced by the INI */
        g_want_own_present = cfg.own_present;
        LOG("presentation chain forced to %s by own_present", g_want_own_present ? "ours" : "the game's");
        if (!g_want_own_present) cfg.d3d9ex = 0;
        return;
    }
    char module[MAX_PATH] = "", system[MAX_PATH] = "";
    HMODULE m = GetModuleHandleA("d3d9.dll");
    if (!m || !GetModuleFileNameA(m, module, MAX_PATH) || !GetSystemDirectoryA(system, MAX_PATH)) return;
    size_t n = strlen(system);
    if (n && _strnicmp(module, system, n) == 0) return;      /* the real one */
    g_want_own_present = 0;
    LOG("d3d9 wrapper in use (%s): presenting through the game's own chain", module);
    LOG("  the wrapper decides how the image reaches the window, so the scaling modes and");
    LOG("  borderless fullscreen cannot take effect. Filters and the menu still work.");
    LOG("  Rename that d3d9.dll to use them -- this patch replaces what it does.");
    if (cfg.d3d9ex) {
        cfg.d3d9ex = 0;
        LOG("Direct3D 9Ex disabled: resizing needs Reset, and a reset with 9Ex would lose every game texture");
    }
}
static IDirect3D9* __stdcall hook_Direct3DCreate9(UINT sdk) {
    IDirect3D9* d3d = NULL;
    detect_d3d9_wrapper();
    if (cfg.d3d9ex) {
        HMODULE m = GetModuleHandleA("d3d9.dll");
        Direct3DCreate9ExFn createEx = m ? (Direct3DCreate9ExFn)GetProcAddress(m, "Direct3DCreate9Ex") : NULL;
        if (createEx) {
            IDirect3D9Ex* ex = NULL; HRESULT hr = createEx(sdk, &ex);
            if (SUCCEEDED(hr) && ex) { d3d = (IDirect3D9*)ex; g_using_ex = 1; LOG("Direct3DCreate9Ex ok (%s)", "hooked"); }
            else LOG("Direct3DCreate9Ex failed (0x%08lx), using Direct3DCreate9", (long)hr);
        } else LOG("Direct3DCreate9Ex not available, using Direct3DCreate9");
    }
    if (!d3d) d3d = orig_Direct3DCreate9(sdk);
    if (d3d) {
        void** vt = *(void***)d3d;
        patch_vtable(vt, 16, (void*)hook_CreateDevice, (void**)&orig_CreateDevice);
        LOG("Direct3DCreate9 hooked (9Ex=%d)", g_using_ex);
    }
    return d3d;
}
static int hook_iat(const char* dll, const char* func, void* hook, void** orig) {
    uint8_t* base = (uint8_t*)0x400000; /* required by the selected executable profile */
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base; IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress);
    for (; imp->Name; imp++) {
        if (_stricmp((char*)(base + imp->Name), dll) != 0) continue;
        if (!imp->OriginalFirstThunk) return 0;
        IMAGE_THUNK_DATA* thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        IMAGE_THUNK_DATA* oth = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
        for (; oth->u1.AddressOfData; thunk++, oth++) {
            if (oth->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            IMAGE_IMPORT_BY_NAME* ibn = (IMAGE_IMPORT_BY_NAME*)(base + oth->u1.AddressOfData);
            if (strcmp((char*)ibn->Name, func) != 0) continue;
            *orig=(void*)thunk->u1.Function;
            return patch_memory((uintptr_t)&thunk->u1.Function,&hook,4,NULL);
        }
    }
    return 0;
}
