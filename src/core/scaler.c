/* ------------------------------------------------------------------ output scaling
 * The game is written for a fixed 640x480 back buffer and lets Direct3D stretch that to
 * whatever the window happens to be, with a filter the driver picks.  Instead we make the
 * swap chain match the window and give the game a 640x480 render target to draw into, then
 * present that render target as a textured quad with a scaling rule and filter of our own.
 *
 * The game is never told: its own D3DPRESENT_PARAMETERS are left at 640x480 and
 * GetBackBuffer hands out the render target's surface, so the screenshot path (which sizes
 * its buffer from those parameters) and the engine's "capture the screen into a sprite"
 * path still see exactly the surface they expect.
 */

enum { SCALE_STRETCH = 0, SCALE_ASPECT = 1, SCALE_INTEGER = 2 };
enum { FILTER_NEAREST = 0, FILTER_BILINEAR = 1, FILTER_SHARP = 2, FILTER_BUILTIN_COUNT };

struct ScaleRect { int x, y, w, h; };

/* Destination rectangle for a sw x sh image inside a dw x dh target. Pure; unit tested. */
static struct ScaleRect scale_rect(int sw, int sh, int dw, int dh, int mode) {
    struct ScaleRect r;
    r.x = r.y = 0; r.w = dw > 0 ? dw : 0; r.h = dh > 0 ? dh : 0;
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return r;
    if (mode == SCALE_STRETCH) return r;
    if (mode == SCALE_INTEGER) {
        int n = dw / sw, m = dh / sh;
        if (m < n) n = m;
        if (n >= 1) { r.w = sw * n; r.h = sh * n; r.x = (dw - r.w) / 2; r.y = (dh - r.h) / 2; return r; }
        /* target too small for even 1:1 — fall through and fit the aspect instead */
    }
    if ((long long)sw * dh > (long long)dw * sh) {           /* width is the limit */
        r.w = dw; r.h = (int)(((long long)dw * sh + sw / 2) / sw);
    } else {                                                  /* height is the limit */
        r.h = dh; r.w = (int)(((long long)dh * sw + sh / 2) / sh);
    }
    if (r.w < 1) r.w = 1;
    if (r.h < 1) r.h = 1;
    if (r.w > dw) r.w = dw;
    if (r.h > dh) r.h = dh;
    r.x = (dw - r.w) / 2; r.y = (dh - r.h) / 2;
    return r;
}
/* Point-upscale factor used by the sharp-bilinear pass: the smallest integer multiple that
   covers the destination, so the bilinear pass only ever softens the sub-pixel remainder. */
static int sharp_factor(int sw, int sh, int dw, int dh) {
    int fx = (dw + sw - 1) / sw, fy = (dh + sh - 1) / sh;
    int f = fx > fy ? fx : fy;
    if (f < 1) f = 1;
    if (f > 8) f = 8;
    return f;
}

#ifndef HFR_SCALER_TEST_ONLY

typedef HRESULT (__stdcall *PresentFn)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT (__stdcall *GetBackBufferFn)(IDirect3DDevice9*, UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9**);
static PresentFn orig_Present; static GetBackBufferFn orig_GetBackBuffer;

static IDirect3DDevice9*     g_dev;
static IDirect3DTexture9*    g_src_tex;      /* the game's render target, native size */
static IDirect3DSurface9*    g_src_surf;
static IDirect3DSurface9*    g_src_ds;       /* our own depth stencil, native size */
static IDirect3DSurface9*    g_real_bb;      /* the actual swap chain back buffer */
static IDirect3DTexture9*    g_mid_tex;      /* sharp-bilinear intermediate */
static IDirect3DSurface9*    g_mid_surf;
static int                   g_mid_factor;
static IDirect3DStateBlock9* g_state;
static int g_native_w, g_native_h;           /* the size the game believes it renders at */
static int g_out_w, g_out_h;                 /* the real swap chain size */
static int g_scaler_ok;                      /* redirection is live */
static int g_scaler_enabled = 1;             /* may be turned off when unsupported */
static D3DFORMAT g_bb_format, g_ds_format;
static unsigned g_stat_blits;

#define SAFE_RELEASE(p) do { if (p) { IUnknown* u_ = (IUnknown*)(p); u_->lpVtbl->Release(u_); (p) = NULL; } } while (0)

static void scaler_release_mid(void) { SAFE_RELEASE(g_mid_surf); SAFE_RELEASE(g_mid_tex); g_mid_factor = 0; }
/* Release everything tied to the current swap chain. ResetEx keeps our textures alive, but the
   back buffer surface is recreated, so it is re-acquired after every reset regardless. */
static void scaler_release(void) {
    g_scaler_ok = 0;
    SAFE_RELEASE(g_state); SAFE_RELEASE(g_real_bb); scaler_release_mid();
    SAFE_RELEASE(g_src_ds); SAFE_RELEASE(g_src_surf); SAFE_RELEASE(g_src_tex);
}

/* Defined by the window module: may turn the game's exclusive fullscreen request into a
   borderless window covering the monitor, in which case it fixes the size itself. */
static int window_override_pp(D3DPRESENT_PARAMETERS* out, HWND hwnd);

/* Rewrite the parameters actually handed to Direct3D: the swap chain follows the output size
   while the caller's copy (the game's own globals) keeps describing the native size. */
static void scaler_adjust_pp(D3DPRESENT_PARAMETERS* out, const D3DPRESENT_PARAMETERS* game, HWND hwnd, int want_w, int want_h) {
    *out = *game;
    if (!g_scaler_enabled) return;
    if (g_native_w <= 0) { g_native_w = (int)game->BackBufferWidth; g_native_h = (int)game->BackBufferHeight; }
    if (window_override_pp(out, hwnd)) { want_w = (int)out->BackBufferWidth; want_h = (int)out->BackBufferHeight; }
    if (want_w <= 0 || want_h <= 0) {
        RECT c;
        if (out->Windowed && hwnd && GetClientRect(hwnd, &c) && c.right > 0 && c.bottom > 0) { want_w = c.right; want_h = c.bottom; }
        else { want_w = (int)game->BackBufferWidth; want_h = (int)game->BackBufferHeight; }
    }
    if (want_w < 1) want_w = 1;
    if (want_h < 1) want_h = 1;
    out->BackBufferWidth = (UINT)want_w;
    out->BackBufferHeight = (UINT)want_h;
    out->MultiSampleType = D3DMULTISAMPLE_NONE;
    out->MultiSampleQuality = 0;
}

/* Create the render target the game draws into, plus its depth stencil, and take hold of the
   real back buffer. Any failure leaves the patch in stock behaviour rather than half applied. */
static int scaler_create(IDirect3DDevice9* dev, const D3DPRESENT_PARAMETERS* used) {
    scaler_release();
    g_dev = dev;
    g_out_w = (int)used->BackBufferWidth; g_out_h = (int)used->BackBufferHeight;
    g_bb_format = used->BackBufferFormat;
    g_ds_format = used->EnableAutoDepthStencil ? used->AutoDepthStencilFormat : D3DFMT_D24S8;
    if (!g_scaler_enabled || g_native_w <= 0 || g_native_h <= 0) return 0;
    HRESULT hr = dev->lpVtbl->CreateTexture(dev, (UINT)g_native_w, (UINT)g_native_h, 1,
                                            D3DUSAGE_RENDERTARGET, g_bb_format, D3DPOOL_DEFAULT, &g_src_tex, NULL);
    if (FAILED(hr)) { LOG("scaler: render target %dx%d failed (0x%08lx); scaling disabled", g_native_w, g_native_h, (long)hr); goto fail; }
    hr = g_src_tex->lpVtbl->GetSurfaceLevel(g_src_tex, 0, &g_src_surf);
    if (FAILED(hr)) { LOG("scaler: GetSurfaceLevel failed (0x%08lx)", (long)hr); goto fail; }
    hr = dev->lpVtbl->CreateDepthStencilSurface(dev, (UINT)g_native_w, (UINT)g_native_h, g_ds_format,
                                                D3DMULTISAMPLE_NONE, 0, TRUE, &g_src_ds, NULL);
    if (FAILED(hr)) { LOG("scaler: depth stencil %dx%d fmt=%d failed (0x%08lx)", g_native_w, g_native_h, (int)g_ds_format, (long)hr); goto fail; }
    hr = orig_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &g_real_bb);
    if (FAILED(hr)) { LOG("scaler: GetBackBuffer failed (0x%08lx)", (long)hr); goto fail; }
    if (FAILED(dev->lpVtbl->CreateStateBlock(dev, D3DSBT_ALL, &g_state))) {
        g_state = NULL;   /* not fatal: the game sets the state it needs on every draw */
        LOG("scaler: no state block; relying on the game to set its own render state");
    }
    dev->lpVtbl->SetRenderTarget(dev, 0, g_src_surf);
    dev->lpVtbl->SetDepthStencilSurface(dev, g_src_ds);
    g_scaler_ok = 1;
    LOG("scaler: %dx%d game surface -> %dx%d swap chain (mode=%d filter=%d)", g_native_w, g_native_h, g_out_w, g_out_h, cfg.scaling, cfg.filter);
    return 1;
fail:
    scaler_release();
    g_scaler_enabled = 0;
    return 0;
}
/* After Present, and after every reset, the game must find its own surface bound again. */
static void scaler_rebind(IDirect3DDevice9* dev) {
    if (!g_scaler_ok) return;
    dev->lpVtbl->SetRenderTarget(dev, 0, g_src_surf);
    dev->lpVtbl->SetDepthStencilSurface(dev, g_src_ds);
}

struct QuadVtx { float x, y, z, rhw, u, v; };
#define QUAD_FVF (D3DFVF_XYZRHW | D3DFVF_TEX1)

static void quad_states(IDirect3DDevice9* dev, int filter) {
    dev->lpVtbl->SetVertexShader(dev, NULL);
    dev->lpVtbl->SetPixelShader(dev, NULL);
    dev->lpVtbl->SetRenderState(dev, D3DRS_ZENABLE, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_ALPHABLENDENABLE, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_ALPHATESTENABLE, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_FOGENABLE, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_SCISSORTESTENABLE, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_STENCILENABLE, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_COLORWRITEENABLE, 0xf);
    dev->lpVtbl->SetRenderState(dev, D3DRS_SRGBWRITEENABLE, FALSE);
    dev->lpVtbl->SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->lpVtbl->SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->lpVtbl->SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->lpVtbl->SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->lpVtbl->SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->lpVtbl->SetTextureStageState(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->lpVtbl->SetSamplerState(dev, 0, D3DSAMP_MINFILTER, (DWORD)filter);
    dev->lpVtbl->SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, (DWORD)filter);
    dev->lpVtbl->SetSamplerState(dev, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->lpVtbl->SetSamplerState(dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->lpVtbl->SetSamplerState(dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->lpVtbl->SetSamplerState(dev, 0, D3DSAMP_SRGBTEXTURE, FALSE);
    dev->lpVtbl->SetFVF(dev, QUAD_FVF);
}
/* One textured quad. The half-texel shift is what maps texel centres onto pixel centres. */
static void draw_quad(IDirect3DDevice9* dev, IDirect3DTexture9* tex, const struct ScaleRect* r) {
    float x0 = (float)r->x - 0.5f, y0 = (float)r->y - 0.5f;
    float x1 = x0 + (float)r->w,   y1 = y0 + (float)r->h;
    struct QuadVtx v[4] = {
        { x0, y0, 0.0f, 1.0f, 0.0f, 0.0f },
        { x1, y0, 0.0f, 1.0f, 1.0f, 0.0f },
        { x0, y1, 0.0f, 1.0f, 0.0f, 1.0f },
        { x1, y1, 0.0f, 1.0f, 1.0f, 1.0f },
    };
    dev->lpVtbl->SetTexture(dev, 0, (IDirect3DBaseTexture9*)tex);
    dev->lpVtbl->DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof v[0]);
}
/* Sharp bilinear: point-magnify to the smallest integer multiple that covers the destination,
   then let bilinear handle only the leftover fraction. Keeps pixel edges crisp and even. */
static IDirect3DTexture9* sharp_prepass(IDirect3DDevice9* dev, const struct ScaleRect* dst) {
    int f = sharp_factor(g_native_w, g_native_h, dst->w, dst->h);
    if (f <= 1) return NULL;
    int w = g_native_w * f, h = g_native_h * f;
    if (g_mid_factor != f) {
        scaler_release_mid();
        if (FAILED(dev->lpVtbl->CreateTexture(dev, (UINT)w, (UINT)h, 1, D3DUSAGE_RENDERTARGET, g_bb_format, D3DPOOL_DEFAULT, &g_mid_tex, NULL)) ||
            FAILED(g_mid_tex->lpVtbl->GetSurfaceLevel(g_mid_tex, 0, &g_mid_surf))) {
            scaler_release_mid();
            LOG("scaler: sharp-bilinear intermediate %dx%d unavailable; using bilinear", w, h);
            return NULL;
        }
        g_mid_factor = f;
    }
    dev->lpVtbl->SetRenderTarget(dev, 0, g_mid_surf);
    dev->lpVtbl->SetDepthStencilSurface(dev, NULL);
    D3DVIEWPORT9 vp = { 0, 0, (DWORD)w, (DWORD)h, 0.0f, 1.0f };
    dev->lpVtbl->SetViewport(dev, &vp);
    quad_states(dev, D3DTEXF_POINT);
    struct ScaleRect full = { 0, 0, w, h };
    draw_quad(dev, g_src_tex, &full);
    return g_mid_tex;
}

static void ui_render_frame(IDirect3DDevice9* dev, const struct ScaleRect* content);

/* Called from the Present hook, after the game's EndScene and before the real Present. */
static void scaler_blit(IDirect3DDevice9* dev) {
    if (!g_scaler_ok || !g_real_bb || g_out_w <= 0 || g_out_h <= 0) return;
    if (g_state) g_state->lpVtbl->Capture(g_state);
    struct ScaleRect dst = scale_rect(g_native_w, g_native_h, g_out_w, g_out_h, cfg.scaling);
    if (FAILED(dev->lpVtbl->BeginScene(dev))) { if (g_state) g_state->lpVtbl->Apply(g_state); return; }
    IDirect3DTexture9* src = g_src_tex;
    int filter = D3DTEXF_POINT;
    if (cfg.filter == FILTER_BILINEAR) filter = D3DTEXF_LINEAR;
    else if (cfg.filter == FILTER_SHARP) {
        IDirect3DTexture9* mid = sharp_prepass(dev, &dst);
        if (mid) { src = mid; filter = D3DTEXF_LINEAR; }
        else filter = D3DTEXF_LINEAR;
    }
    dev->lpVtbl->SetRenderTarget(dev, 0, g_real_bb);
    dev->lpVtbl->SetDepthStencilSurface(dev, NULL);
    D3DVIEWPORT9 vp = { 0, 0, (DWORD)g_out_w, (DWORD)g_out_h, 0.0f, 1.0f };
    dev->lpVtbl->SetViewport(dev, &vp);
    if (dst.w < g_out_w || dst.h < g_out_h)
        dev->lpVtbl->Clear(dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    quad_states(dev, filter);
    draw_quad(dev, src, &dst);
    ui_render_frame(dev, &dst);
    dev->lpVtbl->EndScene(dev);
    dev->lpVtbl->SetTexture(dev, 0, NULL);
    /* Rebind before restoring state: binding a render target resets the viewport, so the
       state block has to be applied afterwards for the game to find its own viewport. */
    scaler_rebind(dev);
    if (g_state) g_state->lpVtbl->Apply(g_state);
    g_stat_blits++;
}

static HRESULT __stdcall hook_Present(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty) {
    scaler_blit(dev);
    return orig_Present(dev, src, dst, wnd, dirty);
}
/* The game asks for the back buffer to save screenshots and to capture the screen into a
   sprite; both must see the surface it actually drew on, at the size it expects. */
static HRESULT __stdcall hook_GetBackBuffer(IDirect3DDevice9* dev, UINT chain, UINT index, D3DBACKBUFFER_TYPE type, IDirect3DSurface9** out) {
    if (g_scaler_ok && g_src_surf && chain == 0 && index == 0 && type == D3DBACKBUFFER_TYPE_MONO && out) {
        g_src_surf->lpVtbl->AddRef(g_src_surf);
        *out = g_src_surf;
        return D3D_OK;
    }
    return orig_GetBackBuffer(dev, chain, index, type, out);
}
#endif /* HFR_SCALER_TEST_ONLY */
