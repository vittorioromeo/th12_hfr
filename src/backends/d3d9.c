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
static void tex_register(IDirect3DTexture9* t);
static void tex_hook_class(IDirect3DTexture9* t);
static HRESULT __stdcall hook_CreateTexture(IDirect3DDevice9* dev, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture9** out, HANDLE* sh) {
    if (g_using_ex) unmanage(&pool, &usage);
    HRESULT hr = orig_CreateTexture(dev, w, h, levels, usage, fmt, pool, out, sh);
    if (SUCCEEDED(hr) && out && *out && cfg.texture_scale > 1) { tex_hook_class(*out); tex_register(*out); }
    if (SUCCEEDED(hr) && out && *out && cfg.debug) LOG("texture %p %ux%u fmt %d usage 0x%lx", (void*)*out, w, h, (int)fmt, (unsigned long)usage);
    return hr;
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
static char g_wrapper_path[MAX_PATH];   /* the non-system d3d9.dll presenting this game, if any */

/* A d3d9 wrapper -- PivotDX9 and friends -- is not a conflict the way vpatch is: the patch
   installs, the game runs, the filters work. It only takes the last step, placing the image
   in the window, which is why the scaling modes and borderless fullscreen stop having any
   effect there. So this warns rather than refuses, and only when it is actually costing
   something: someone who stretches to fill and never uses borderless loses nothing and should
   not be told off about a file they installed on purpose. video.warn_wrapper=0 silences it. */
static void warn_if_wrapper_presents(void) {
    if (!cfg.warn_wrapper || g_own_present) return;
    if (cfg.scaling == SCALE_STRETCH && !cfg.fullscreen_mode) {
        LOG("presentation is not ours, but no setting in use depends on it");
        return;
    }
    char text[900];
    if (g_wrapper_path[0])
        snprintf(text, sizeof text,
            "A Direct3D 9 wrapper is presenting this game:\n\n%s\n\n"
            "It, not Touhou HFR, decides how the image ends up in the window, so the scaling "
            "mode (fit, pixel perfect) and borderless fullscreen cannot take effect -- the game "
            "will simply fill the window. The upscaling filters, the resizable window and the "
            "in-game menu all still work.\n\n"
            "Touhou HFR replaces what that wrapper did for these games. Rename it to d3d9.dllx, "
            "or move it out of the game's folder, to get those two settings back.\n\n"
            "Set warn_wrapper=0 under [video] in the INI to stop showing this.", g_wrapper_path);
    else
        snprintf(text, sizeof text,
            "Touhou HFR could not create its own presentation chain, so the game is presenting "
            "itself.\n\nThe scaling mode (fit, pixel perfect) and borderless fullscreen cannot "
            "take effect -- the game will fill the window. Filters, resizing and the menu still "
            "work. touhou_hfr.log says what failed.\n\n"
            "Set warn_wrapper=0 under [video] in the INI to stop showing this.");
    LOG("warning shown: presentation is not ours, so scaling and borderless fullscreen are inert");
    show_notice(text);
}

static void texscale_init(IDirect3DDevice9* dev); static void dim_init(IDirect3DDevice9* dev);
static void after_device(IDirect3DDevice9* dev) {
    dim_init(dev);
    warn_if_wrapper_presents();    /* g_own_present is settled by now, however it turned out */
    texscale_init(dev);
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

/* ---- Internal resolution (video.internal_scale = N). The game's render target is N times its
   own 640x480, and everything it submits to that target is scaled by N: viewports, clear rects
   and every pre-transformed (XYZRHW) vertex, with the D3D9 half-texel rule kept
   (x' = (x + 0.5) N - 0.5). Draws to any other target -- the game's own render-to-texture
   surfaces, our presentation chain -- are left alone. Anything drawn through a real projection
   (the 3D stage) needs nothing: the rasteriser simply has N times the pixels.
   On its own this only sharpens; the sprite builder rounds every corner to a whole pixel before
   the half-texel offset, so the profile's sprite_round_sites are NOPed as well (install.c),
   which is what lets a bullet at x = 100.3 land on a different screen pixel than one at 100.0. */
static int g_iscale_active;                  /* the current render target is the game's own (dimming.c uses it too) */
typedef HRESULT (__stdcall *SetRenderTargetFn)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
typedef HRESULT (__stdcall *SetViewportFn)(IDirect3DDevice9*, const D3DVIEWPORT9*);
typedef HRESULT (__stdcall *ClearFn)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
typedef HRESULT (__stdcall *DrawPrimitiveUPFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
static SetRenderTargetFn orig_SetRenderTarget; static SetViewportFn orig_SetViewport; static ClearFn orig_Clear; static DrawPrimitiveUPFn orig_DrawPrimitiveUP;
static HRESULT __stdcall hook_SetRenderTarget(IDirect3DDevice9* dev, DWORD i, IDirect3DSurface9* s) {
    if (i == 0) g_iscale_active = (s != NULL && s == g_src_surf);
    if (g_dim_trace_frames > 0 && cfg.debug) LOG("draw      SetRenderTarget %lu %p (prio %d)%s", (unsigned long)i, (void*)s, g_draw_prio, s == g_src_surf ? " (game RT)" : s == g_shot_rt ? " (shot RT)" : "");
    return orig_SetRenderTarget(dev, i, s);
}
static HRESULT __stdcall hook_SetViewport(IDirect3DDevice9* dev, const D3DVIEWPORT9* vp) {
    if (g_dim_trace_frames > 0 && cfg.debug && vp) LOG("draw      SetViewport %lu,%lu %lux%lu%s", (unsigned long)vp->X, (unsigned long)vp->Y, (unsigned long)vp->Width, (unsigned long)vp->Height, g_iscale_active ? " (scaled)" : "");
    if (g_iscale_active && vp && !g_dim_drawing) {
        D3DVIEWPORT9 v = *vp; v.X *= g_iscale; v.Y *= g_iscale; v.Width *= g_iscale; v.Height *= g_iscale;
        return orig_SetViewport(dev, &v);
    }
    return orig_SetViewport(dev, vp);
}
static HRESULT __stdcall hook_Clear(IDirect3DDevice9* dev, DWORD n, const D3DRECT* r, DWORD flags, D3DCOLOR c, float z, DWORD st) {
    if (g_iscale_active && n && r && n <= 16) {
        D3DRECT rr[16];
        for (DWORD i = 0; i < n; ++i) { rr[i].x1 = r[i].x1 * g_iscale; rr[i].y1 = r[i].y1 * g_iscale; rr[i].x2 = r[i].x2 * g_iscale; rr[i].y2 = r[i].y2 * g_iscale; }
        return orig_Clear(dev, n, rr, flags, c, z, st);
    }
    return orig_Clear(dev, n, r, flags, c, z, st);
}
static HRESULT __stdcall hook_DrawPrimitiveUP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT prims, const void* data, UINT stride) {
    DWORD fvf = 0;
    if (g_dim_trace_frames > 0 && !g_dim_drawing) {
        DWORD f = 0; dev->lpVtbl->GetFVF(dev, &f); dim_trace(dev, "UP", prims, f, g_iscale_active, __builtin_return_address(0));
        if (cfg.debug && prims == 2 && stride >= 24 && (f & D3DFVF_XYZRHW) && data)
            for (UINT i = 0; i < 4; ++i) { const float* q = (const float*)((const uint8_t*)data + i * stride); LOG("draw        v%u %.1f,%.1f uv %.3f,%.3f", i, q[0], q[1], q[(stride / 4) - 2], q[(stride / 4) - 1]); }
    }
    int scale = g_iscale_active && g_iscale > 1 && !g_dim_drawing, fade = dim_item_fade();
    if ((scale || fade != 256) && data && stride >= 16 && SUCCEEDED(dev->lpVtbl->GetFVF(dev, &fvf)) && (fvf & D3DFVF_XYZRHW)) {
        UINT verts = type == D3DPT_TRIANGLELIST ? prims * 3 : type == D3DPT_TRIANGLESTRIP || type == D3DPT_TRIANGLEFAN ? prims + 2 :
                     type == D3DPT_LINELIST ? prims * 2 : type == D3DPT_LINESTRIP ? prims + 1 : prims;
        static uint8_t* buf; static UINT cap;
        UINT need = verts * stride;
        if (need > cap) { uint8_t* nb = (uint8_t*)realloc(buf, need); if (!nb) return orig_DrawPrimitiveUP(dev, type, prims, data, stride); buf = nb; cap = need; }
        memcpy(buf, data, need);
        if (scale) {
            float n = (float)g_iscale;
            for (UINT i = 0; i < verts; ++i) { float* v = (float*)(buf + i * stride); v[0] = (v[0] + 0.5f) * n - 0.5f; v[1] = (v[1] + 0.5f) * n - 0.5f; }
        }
        if (fade != 256) {
            DWORD dst = D3DBLEND_INVSRCALPHA; dev->lpVtbl->GetRenderState(dev, D3DRS_DESTBLEND, &dst);
            dim_fade_vertices(buf, verts, stride, fvf, fade, dst == D3DBLEND_ONE);
        }
        return orig_DrawPrimitiveUP(dev, type, prims, buf, stride);
    }
    return orig_DrawPrimitiveUP(dev, type, prims, data, stride);
}
typedef HRESULT (__stdcall *DrawPrimitiveFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
static DrawPrimitiveFn orig_DrawPrimitive;
static HRESULT __stdcall hook_DrawPrimitive(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT start, UINT prims) {
    if (g_dim_trace_frames > 0) { DWORD f = 0; dev->lpVtbl->GetFVF(dev, &f); dim_trace(dev, "VB", prims, f, g_iscale_active, __builtin_return_address(0)); }
    return orig_DrawPrimitive(dev, type, start, prims);
}
typedef HRESULT (__stdcall *DrawIndexedPrimitiveFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT (__stdcall *DrawIndexedPrimitiveUPFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT);
static DrawIndexedPrimitiveFn orig_DrawIndexedPrimitive; static DrawIndexedPrimitiveUPFn orig_DrawIndexedPrimitiveUP;
static HRESULT __stdcall hook_DrawIndexedPrimitive(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT base, UINT minv, UINT nverts, UINT start, UINT prims) {
    if (g_dim_trace_frames > 0) { DWORD f = 0; dev->lpVtbl->GetFVF(dev, &f); dim_trace(dev, "IDX", prims, f, g_iscale_active, __builtin_return_address(0)); }
    return orig_DrawIndexedPrimitive(dev, type, base, minv, nverts, start, prims);
}
static HRESULT __stdcall hook_DrawIndexedPrimitiveUP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT minv, UINT nverts, UINT prims, const void* idx, D3DFORMAT ifmt, const void* data, UINT stride) {
    if (g_dim_trace_frames > 0) { DWORD f = 0; dev->lpVtbl->GetFVF(dev, &f); dim_trace(dev, "IDXUP", prims, f, g_iscale_active, __builtin_return_address(0)); }
    return orig_DrawIndexedPrimitiveUP(dev, type, minv, nverts, prims, idx, ifmt, data, stride);
}
/* Texture upscaling (texscale.c): the bind is where the copy is substituted, and the
   texture class's Release/LockRect and the D3DX surface loaders are where a copy is dropped
   or marked stale. The class vtable is shared by every texture of the device, so it is
   patched once, from the first texture the game creates. */
typedef HRESULT (__stdcall *SetTextureFn)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
static SetTextureFn orig_SetTexture;
static IDirect3DBaseTexture9* tex_substitute(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* t);
static HRESULT __stdcall hook_SetTexture(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* t) {
    return orig_SetTexture(dev, stage, tex_substitute(dev, stage, t));
}
typedef ULONG (__stdcall *TexReleaseFn)(IDirect3DTexture9*);
typedef HRESULT (__stdcall *TexLockRectFn)(IDirect3DTexture9*, UINT, D3DLOCKED_RECT*, const RECT*, DWORD);
static TexReleaseFn orig_TexRelease; static TexLockRectFn orig_TexLockRect;
static void tex_released(IDirect3DTexture9* t); static void tex_dirty_texture(IDirect3DTexture9* t); static void tex_dirty_surface(IDirect3DSurface9* s);
static ULONG __stdcall hook_TexRelease(IDirect3DTexture9* t) {
    ULONG n = orig_TexRelease(t);
    if (n == 0) tex_released(t);
    return n;
}
static HRESULT __stdcall hook_TexLockRect(IDirect3DTexture9* t, UINT level, D3DLOCKED_RECT* lr, const RECT* r, DWORD flags) {
    if (!(flags & D3DLOCK_READONLY)) tex_dirty_texture(t);
    return orig_TexLockRect(t, level, lr, r, flags);
}
static void tex_hook_class(IDirect3DTexture9* t) {
    void** vt = *(void***)t;
    patch_vtable(vt, 2, (void*)hook_TexRelease, (void**)&orig_TexRelease);
    patch_vtable(vt, 19, (void*)hook_TexLockRect, (void**)&orig_TexLockRect);
}
typedef HRESULT (__stdcall *D3DXLoadSurfaceFromMemoryFn)(IDirect3DSurface9*, const PALETTEENTRY*, const RECT*, LPCVOID, D3DFORMAT, UINT, const PALETTEENTRY*, const RECT*, DWORD, D3DCOLOR);
typedef HRESULT (__stdcall *D3DXLoadSurfaceFromFileInMemoryFn)(IDirect3DSurface9*, const PALETTEENTRY*, const RECT*, LPCVOID, UINT, const RECT*, DWORD, D3DCOLOR, void*);
static D3DXLoadSurfaceFromMemoryFn orig_D3DXLoadSurfaceFromMemory; static D3DXLoadSurfaceFromFileInMemoryFn orig_D3DXLoadSurfaceFromFileInMemory;
static HRESULT __stdcall hook_D3DXLoadSurfaceFromMemory(IDirect3DSurface9* dst, const PALETTEENTRY* dp, const RECT* dr, LPCVOID mem, D3DFORMAT fmt, UINT pitch, const PALETTEENTRY* sp, const RECT* sr, DWORD filter, D3DCOLOR key) {
    tex_dirty_surface(dst);
    return orig_D3DXLoadSurfaceFromMemory(dst, dp, dr, mem, fmt, pitch, sp, sr, filter, key);
}
static HRESULT __stdcall hook_D3DXLoadSurfaceFromFileInMemory(IDirect3DSurface9* dst, const PALETTEENTRY* dp, const RECT* dr, LPCVOID mem, UINT size, const RECT* sr, DWORD filter, D3DCOLOR key, void* info) {
    tex_dirty_surface(dst);
    return orig_D3DXLoadSurfaceFromFileInMemory(dst, dp, dr, mem, size, sr, filter, key, info);
}
/* The engine's screen capture (pause backdrop, spell backgrounds) copies a 640x480-coordinate
   rectangle of the back buffer into one of its textures with D3DX. The back buffer is ours and
   N times larger, so the source rectangle is scaled; D3DX then filters it down to the
   destination the game asked for. */
typedef HRESULT (__stdcall *D3DXLoadSurfaceFromSurfaceFn)(IDirect3DSurface9*, const PALETTEENTRY*, const RECT*, IDirect3DSurface9*, const PALETTEENTRY*, const RECT*, DWORD, D3DCOLOR);
static D3DXLoadSurfaceFromSurfaceFn orig_D3DXLoadSurfaceFromSurface;
static HRESULT __stdcall hook_D3DXLoadSurfaceFromSurface(IDirect3DSurface9* dst, const PALETTEENTRY* dp, const RECT* dr, IDirect3DSurface9* src, const PALETTEENTRY* sp, const RECT* sr, DWORD filter, D3DCOLOR key) {
    tex_dirty_surface(dst);
    if (g_iscale > 1 && sr && src == g_src_surf) {
        RECT r = { sr->left * g_iscale, sr->top * g_iscale, sr->right * g_iscale, sr->bottom * g_iscale };
        return orig_D3DXLoadSurfaceFromSurface(dst, dp, dr, src, sp, &r, filter, key);
    }
    return orig_D3DXLoadSurfaceFromSurface(dst, dp, dr, src, sp, sr, filter, key);
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
        if (g_using_ex || cfg.texture_scale > 1)
            patch_vtable(vt, 23, (void*)hook_CreateTexture, (void**)&orig_CreateTexture);
        if (cfg.texture_scale > 1) patch_vtable(vt, 65, (void*)hook_SetTexture, (void**)&orig_SetTexture);
        if (g_using_ex) {
            patch_vtable(vt, 26, (void*)hook_CreateVertexBuffer, (void**)&orig_CreateVertexBuffer);
            patch_vtable(vt, 27, (void*)hook_CreateIndexBuffer, (void**)&orig_CreateIndexBuffer);
        }
        if (g_iscale > 1 || g_dim_available) {
            patch_vtable(vt, 37, (void*)hook_SetRenderTarget, (void**)&orig_SetRenderTarget);
            patch_vtable(vt, 83, (void*)hook_DrawPrimitiveUP, (void**)&orig_DrawPrimitiveUP);
            patch_vtable(vt, 81, (void*)hook_DrawPrimitive, (void**)&orig_DrawPrimitive);
            if (cfg.debug) {
                patch_vtable(vt, 82, (void*)hook_DrawIndexedPrimitive, (void**)&orig_DrawIndexedPrimitive);
                patch_vtable(vt, 84, (void*)hook_DrawIndexedPrimitiveUP, (void**)&orig_DrawIndexedPrimitiveUP);
            }
        }
        if (g_iscale > 1) {
            patch_vtable(vt, 43, (void*)hook_Clear, (void**)&orig_Clear);
            patch_vtable(vt, 47, (void*)hook_SetViewport, (void**)&orig_SetViewport);
            LOG("internal resolution x%d: the game draws at %dx%d", g_iscale, g_native_w, g_native_h);
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
    snprintf(g_wrapper_path, sizeof g_wrapper_path, "%s", module);
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
