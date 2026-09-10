/* ------------------------------------------------------------------ Direct3D hooks */
typedef HRESULT (__stdcall *CreateDeviceFn)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT (__stdcall *ResetFn)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef IDirect3D9* (__stdcall *Direct3DCreate9Fn)(UINT);
static CreateDeviceFn orig_CreateDevice; static ResetFn orig_Reset; static Direct3DCreate9Fn orig_Direct3DCreate9;

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
static HRESULT __stdcall hook_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    apply_pp(pp);
    HRESULT hr;
    if (g_using_ex) {
        IDirect3DDevice9Ex* ex = (IDirect3DDevice9Ex*)dev; D3DDISPLAYMODEEX m; fill_mode_ex(pp, &m);
        hr = ex->lpVtbl->ResetEx(ex, pp, pp->Windowed ? NULL : &m);
        LOG("ResetEx -> 0x%08lx", (long)hr);
    } else {
        hr = orig_Reset(dev, pp);
        LOG("Reset -> 0x%08lx", (long)hr);
    }
    if (SUCCEEDED(hr)) after_device(dev);
    return hr;
}
static HRESULT __stdcall hook_CreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND hwnd, DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    apply_pp(pp);
    HRESULT hr;
    if (g_using_ex) {
        IDirect3D9Ex* ex = (IDirect3D9Ex*)d3d; D3DDISPLAYMODEEX m; fill_mode_ex(pp, &m);
        hr = ex->lpVtbl->CreateDeviceEx(ex, adapter, type, hwnd, flags, pp, pp->Windowed ? NULL : &m, (IDirect3DDevice9Ex**)out);
        LOG("CreateDeviceEx -> 0x%08lx", (long)hr);
        if (FAILED(hr)) { hr = orig_CreateDevice(d3d, adapter, type, hwnd, flags, pp, out); LOG("fallback CreateDevice on the 9Ex object -> 0x%08lx", (long)hr); }
        if (SUCCEEDED(hr) && out && *out) {
            static const GUID iid_dev9ex = { 0xb18b10ce, 0x2649, 0x405a, { 0x87, 0x0f, 0x95, 0xf7, 0x77, 0xd4, 0x31, 0x3a } };
            void* q = NULL;
            if (SUCCEEDED((*out)->lpVtbl->QueryInterface(*out, &iid_dev9ex, &q)) && q) { ((IUnknown*)q)->lpVtbl->Release((IUnknown*)q); }
            else { LOG("device does not expose IDirect3DDevice9Ex; 9Ex features disabled"); g_using_ex = 0; }
            if (hwnd) SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);   /* 9Ex resets the window style */
        }
    } else {
        hr = orig_CreateDevice(d3d, adapter, type, hwnd, flags, pp, out);
        LOG("CreateDevice -> 0x%08lx", (long)hr);
    }
    if (SUCCEEDED(hr) && out && *out) {
        IDirect3DDevice9* dev = *out;
        void** vt = *(void***)dev;
        patch_vtable(vt, 16, (void*)hook_Reset, (void**)&orig_Reset);
        if (g_using_ex) {
            patch_vtable(vt, 23, (void*)hook_CreateTexture, (void**)&orig_CreateTexture);
            patch_vtable(vt, 26, (void*)hook_CreateVertexBuffer, (void**)&orig_CreateVertexBuffer);
            patch_vtable(vt, 27, (void*)hook_CreateIndexBuffer, (void**)&orig_CreateIndexBuffer);
        }
        after_device(dev);
    }
    return hr;
}
static IDirect3D9* __stdcall hook_Direct3DCreate9(UINT sdk) {
    IDirect3D9* d3d = NULL;
    if (cfg.d3d9ex) {
        /* use the d3d9.dll the game resolved its import from (a wrapper in the game folder stays in the chain) */
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
