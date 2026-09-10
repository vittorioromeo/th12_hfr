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
static IDirect3DSurface9*    g_lockable;     /* lockable copy handed to the game as its back buffer */
static int                   g_lockable_sysmem;
static unsigned              g_stat_backbuffer;
static IDirect3DSwapChain9*  g_swap;         /* our presentation chain, sized to the window */
static IDirect3DSurface9*    g_real_bb;      /* its back buffer */
static int                   g_own_present;  /* we present; the device's own chain is unused */
static int                   g_want_own_present = 1;  /* cleared when a d3d9 wrapper is in the way */
static IDirect3DTexture9*    g_pass_tex;     /* intermediate for a prepass (sharp or shader) */
static IDirect3DSurface9*    g_pass_surf;
static int                   g_pass_w, g_pass_h;
static IDirect3DStateBlock9* g_state;
static int g_native_w, g_native_h;           /* the size the game believes it renders at */
static int g_out_w, g_out_h;                 /* the real swap chain size */
static int g_scaler_ok;                      /* redirection is live */
static int g_scaler_enabled = 1;             /* may be turned off when unsupported */
static D3DFORMAT g_bb_format, g_ds_format;
static unsigned g_stat_blits;

#define SAFE_RELEASE(p) do { if (p) { IUnknown* u_ = (IUnknown*)(p); u_->lpVtbl->Release(u_); (p) = NULL; } } while (0)

static void scaler_release_pass(void) { SAFE_RELEASE(g_pass_surf); SAFE_RELEASE(g_pass_tex); g_pass_w = g_pass_h = 0; }
/* One intermediate serves whichever prepass is in use; only one runs per frame. */
static int ensure_pass_target(IDirect3DDevice9* dev, int w, int h) {
    if (w < 1 || h < 1 || w > 8192 || h > 8192) return 0;
    if (g_pass_tex && g_pass_w == w && g_pass_h == h) return 1;
    scaler_release_pass();
    if (FAILED(dev->lpVtbl->CreateTexture(dev, (UINT)w, (UINT)h, 1, D3DUSAGE_RENDERTARGET, g_bb_format, D3DPOOL_DEFAULT, &g_pass_tex, NULL)) ||
        FAILED(g_pass_tex->lpVtbl->GetSurfaceLevel(g_pass_tex, 0, &g_pass_surf))) {
        scaler_release_pass();
        LOG("scaler: intermediate %dx%d unavailable", w, h);
        return 0;
    }
    g_pass_w = w; g_pass_h = h;
    return 1;
}
static void client_size(HWND h, int* w, int* t) {
    RECT c;
    if (h && GetClientRect(h, &c)) { *w = c.right - c.left; *t = c.bottom - c.top; }
    else { *w = 0; *t = 0; }
}
static void scaler_release_output(void) { SAFE_RELEASE(g_real_bb); SAFE_RELEASE(g_swap); g_out_w = g_out_h = 0; g_own_present = 0; }
static void scaler_release(void) {
    g_scaler_ok = 0;
    g_pass_w = g_pass_h = 0;
    scaler_release_output();
    SAFE_RELEASE(g_state); scaler_release_pass();
    SAFE_RELEASE(g_lockable); g_lockable_sysmem = 0;
    SAFE_RELEASE(g_src_ds); SAFE_RELEASE(g_src_surf); SAFE_RELEASE(g_src_tex);
}

/* Presentation goes through a swap chain of our own rather than the device's.
 *
 * The alternative is to resize the device's own chain, which means Reset. The game cannot
 * survive that: with Direct3D 9Ex every texture it owns has been moved to the default pool
 * (9Ex has no managed pool), default-pool contents are undefined after a reset, and the
 * game has no code to reload them -- its pre-reset "release" routine only touches three
 * pointers that are never assigned in this build. Its own Alt+Enter reset has the same
 * problem. Creating an additional swap chain sidesteps all of it: the device is never
 * reset, so nothing it owns is ever lost, and resizing is just a new chain.
 */
static int scaler_set_output(IDirect3DDevice9* dev, HWND hwnd, int w, int h) {
    if (!dev || !hwnd || w < 1 || h < 1) return 0;
    scaler_release_output();
    if (!g_want_own_present) return 0;
    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof pp);
    pp.BackBufferWidth = (UINT)w;
    pp.BackBufferHeight = (UINT)h;
    pp.BackBufferFormat = g_bb_format;
    pp.BackBufferCount = 1;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = FALSE;
    pp.PresentationInterval = cfg.vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    HRESULT hr = dev->lpVtbl->CreateAdditionalSwapChain(dev, &pp, &g_swap);
    if (SUCCEEDED(hr)) hr = g_swap->lpVtbl->GetBackBuffer(g_swap, 0, D3DBACKBUFFER_TYPE_MONO, &g_real_bb);
    if (FAILED(hr)) {
        scaler_release_output();
        LOG("scaler: presentation chain %dx%d failed (0x%08lx)", w, h, (long)hr);
        return 0;
    }
    g_out_w = w; g_out_h = h; g_own_present = 1;
    LOG("scaler: presenting through a %dx%d chain of our own", w, h);
    return 1;
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
    window_override_pp(out, hwnd);
    if (!out->Windowed) {
        LOG("scaler: keeping the device windowed instead of taking an exclusive %ux%u mode",
            out->BackBufferWidth, out->BackBufferHeight);
        out->Windowed = TRUE;
        out->FullScreen_RefreshRateInHz = 0;
    }
    if (g_want_own_present) {
        /* We present through a chain of our own, so the device's stays at the game's size. */
        out->BackBufferWidth = (UINT)g_native_w;
        out->BackBufferHeight = (UINT)g_native_h;
    } else {
        /* The device's own chain is what reaches the screen, so it follows the window. */
        if (want_w <= 0 || want_h <= 0) {
            int cw, ch; client_size(hwnd, &cw, &ch);
            want_w = cw > 0 ? cw : g_native_w;
            want_h = ch > 0 ? ch : g_native_h;
        }
    }
    if (want_w > 0 && want_h > 0) { out->BackBufferWidth = (UINT)want_w; out->BackBufferHeight = (UINT)want_h; }
    out->MultiSampleType = D3DMULTISAMPLE_NONE;
    out->MultiSampleQuality = 0;
}

/* Create the render target the game draws into, plus its depth stencil, and take hold of the
   real back buffer. Any failure leaves the patch in stock behaviour rather than half applied. */
static int scaler_create(IDirect3DDevice9* dev, const D3DPRESENT_PARAMETERS* used, HWND hwnd) {
    scaler_release();
    if (g_dev != dev) filters_release();   /* shaders belong to the device, not the chain */
    g_dev = dev;
    shaders_pick_profile(dev);
    resolve_filter();
    g_bb_format = used->BackBufferFormat;
    g_ds_format = used->EnableAutoDepthStencil ? used->AutoDepthStencilFormat : D3DFMT_D24S8;
    if (!g_scaler_enabled || g_native_w <= 0 || g_native_h <= 0) return 0;
    int cw = 0, ch = 0;
    RECT c;
    if (hwnd && GetClientRect(hwnd, &c)) { cw = c.right; ch = c.bottom; }
    if (cw < 1 || ch < 1) { cw = g_native_w; ch = g_native_h; }
    if (!scaler_set_output(dev, hwnd, cw, ch)) {
        /* Fall back to the device's own chain: it was sized to the window at creation, and a
           resize goes through Reset. */
        HRESULT bb = orig_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &g_real_bb);
        if (FAILED(bb)) {
            LOG("scaler: no output surface (0x%08lx); leaving the game to present as it always did", (long)bb);
            g_scaler_enabled = 0;
            return 0;
        }
        g_out_w = (int)used->BackBufferWidth; g_out_h = (int)used->BackBufferHeight;
        LOG("scaler: presenting through the game's own chain at %dx%d", g_out_w, g_out_h);
    }
    HRESULT hr = dev->lpVtbl->CreateTexture(dev, (UINT)g_native_w, (UINT)g_native_h, 1,
                                            D3DUSAGE_RENDERTARGET, g_bb_format, D3DPOOL_DEFAULT, &g_src_tex, NULL);
    if (FAILED(hr)) { LOG("scaler: render target %dx%d failed (0x%08lx); scaling disabled", g_native_w, g_native_h, (long)hr); goto fail; }
    hr = g_src_tex->lpVtbl->GetSurfaceLevel(g_src_tex, 0, &g_src_surf);
    if (FAILED(hr)) { LOG("scaler: GetSurfaceLevel failed (0x%08lx)", (long)hr); goto fail; }
    hr = dev->lpVtbl->CreateDepthStencilSurface(dev, (UINT)g_native_w, (UINT)g_native_h, g_ds_format,
                                                D3DMULTISAMPLE_NONE, 0, TRUE, &g_src_ds, NULL);
    if (FAILED(hr)) { LOG("scaler: depth stencil %dx%d fmt=%d failed (0x%08lx)", g_native_w, g_native_h, (int)g_ds_format, (long)hr); goto fail; }
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
struct QuadVtxVS { float x, y, z, u, v; };
#define QUAD_FVF_VS (D3DFVF_XYZ | D3DFVF_TEX1)

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
    dev->lpVtbl->SetFVF(dev, QUAD_FVF);
    dev->lpVtbl->DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof v[0]);
}
/* Same rectangle, but in clip space for the pass-through vertex shader. The half-pixel
   shift is applied before the conversion, so both paths rasterise identically. */
static void draw_quad_vs(IDirect3DDevice9* dev, IDirect3DTexture9* tex, const struct ScaleRect* r, int tw, int th) {
    if (tw < 1 || th < 1) return;
    float x0 = ((float)r->x - 0.5f) * 2.0f / (float)tw - 1.0f;
    float x1 = ((float)r->x - 0.5f + (float)r->w) * 2.0f / (float)tw - 1.0f;
    float y0 = 1.0f - ((float)r->y - 0.5f) * 2.0f / (float)th;
    float y1 = 1.0f - ((float)r->y - 0.5f + (float)r->h) * 2.0f / (float)th;
    struct QuadVtxVS v[4] = {
        { x0, y0, 0.0f, 0.0f, 0.0f },
        { x1, y0, 0.0f, 1.0f, 0.0f },
        { x0, y1, 0.0f, 0.0f, 1.0f },
        { x1, y1, 0.0f, 1.0f, 1.0f },
    };
    dev->lpVtbl->SetTexture(dev, 0, (IDirect3DBaseTexture9*)tex);
    dev->lpVtbl->SetFVF(dev, QUAD_FVF_VS);
    dev->lpVtbl->DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof v[0]);
}
/* Render one prepass into the intermediate target, optionally through a filter shader. */
static void shader_constants(IDirect3DDevice9* dev, int sw, int sh, int tw, int th) {
    float c0[4] = { (float)sw, (float)sh, sw ? 1.0f / sw : 0.0f, sh ? 1.0f / sh : 0.0f };
    float c1[4] = { (float)tw, (float)th, tw ? 1.0f / tw : 0.0f, th ? 1.0f / th : 0.0f };
    dev->lpVtbl->SetPixelShaderConstantF(dev, 0, c0, 1);
    dev->lpVtbl->SetPixelShaderConstantF(dev, 1, c1, 1);
}
static int run_prepass(IDirect3DDevice9* dev, int w, int h, int sampler, IDirect3DPixelShader9* ps) {
    if (!ensure_pass_target(dev, w, h)) return 0;
    dev->lpVtbl->SetRenderTarget(dev, 0, g_pass_surf);
    dev->lpVtbl->SetDepthStencilSurface(dev, NULL);
    D3DVIEWPORT9 vp = { 0, 0, (DWORD)w, (DWORD)h, 0.0f, 1.0f };
    dev->lpVtbl->SetViewport(dev, &vp);
    quad_states(dev, sampler);
    struct ScaleRect full = { 0, 0, w, h };
    if (ps) {
        IDirect3DVertexShader9* vs = quad_vertex_shader(dev);
        if (!vs) return 0;
        dev->lpVtbl->SetVertexShader(dev, vs);
        dev->lpVtbl->SetPixelShader(dev, ps);
        shader_constants(dev, g_native_w, g_native_h, w, h);
        draw_quad_vs(dev, g_src_tex, &full, w, h);
        dev->lpVtbl->SetPixelShader(dev, NULL);
        dev->lpVtbl->SetVertexShader(dev, NULL);
    } else draw_quad(dev, g_src_tex, &full);
    return 1;
}
/* Decide what the final draw reads and how. A filter may run as a prepass into the
   intermediate target (fixed magnification), or as the shader of the final draw itself
   (free scale), or be a plain sampler state. */
static void select_filter(IDirect3DDevice9* dev, const struct ScaleRect* dst,
                          IDirect3DTexture9** src, int* sampler, IDirect3DPixelShader9** final_ps) {
    *src = g_src_tex; *sampler = D3DTEXF_POINT; *final_ps = NULL;
    struct Filter* f = filter_at(cfg.filter);
    int kind = f ? cfg.filter : FILTER_SHARP;
    if (kind >= FILTER_BUILTIN_COUNT) {
        IDirect3DPixelShader9* ps = filter_shader(dev, f);
        if (!ps) kind = FILTER_SHARP;                       /* unavailable: quietly degrade */
        else if (f->scale > 0) {
            int w = g_native_w * f->scale, h = g_native_h * f->scale;
            if (run_prepass(dev, w, h, D3DTEXF_POINT, ps)) {
                *src = g_pass_tex;
                /* an exact multiple can stay point-sampled and keep every edge crisp */
                *sampler = (dst->w % w == 0 && dst->h % h == 0) ? D3DTEXF_POINT : D3DTEXF_LINEAR;
                return;
            }
            kind = FILTER_SHARP;
        } else if (quad_vertex_shader(dev)) { *final_ps = ps; *sampler = D3DTEXF_POINT; return; }
        else kind = FILTER_SHARP;
    }
    if (kind == FILTER_BILINEAR) { *sampler = D3DTEXF_LINEAR; return; }
    if (kind != FILTER_SHARP) return;                        /* nearest */
    /* Sharp bilinear: point-magnify to the smallest integer multiple that covers the
       destination, so bilinear only has to soften the sub-pixel remainder. */
    int factor = sharp_factor(g_native_w, g_native_h, dst->w, dst->h);
    if (factor <= 1) { *sampler = (dst->w == g_native_w && dst->h == g_native_h) ? D3DTEXF_POINT : D3DTEXF_LINEAR; return; }
    if (run_prepass(dev, g_native_w * factor, g_native_h * factor, D3DTEXF_POINT, NULL)) {
        *src = g_pass_tex;
        *sampler = (dst->w == g_pass_w && dst->h == g_pass_h) ? D3DTEXF_POINT : D3DTEXF_LINEAR;
    } else *sampler = D3DTEXF_LINEAR;
}

static void ui_render_frame(IDirect3DDevice9* dev, const struct ScaleRect* content);

/* Called from the Present hook, after the game's EndScene and before the real Present. */
static void scaler_blit(IDirect3DDevice9* dev) {
    if (!g_scaler_ok || !g_real_bb || g_out_w <= 0 || g_out_h <= 0) return;
    if (g_state) g_state->lpVtbl->Capture(g_state);
    struct ScaleRect dst = scale_rect(g_native_w, g_native_h, g_out_w, g_out_h, cfg.scaling);
    if (FAILED(dev->lpVtbl->BeginScene(dev))) { if (g_state) g_state->lpVtbl->Apply(g_state); return; }
    IDirect3DTexture9* src; int filter; IDirect3DPixelShader9* final_ps;
    select_filter(dev, &dst, &src, &filter, &final_ps);
    dev->lpVtbl->SetRenderTarget(dev, 0, g_real_bb);
    dev->lpVtbl->SetDepthStencilSurface(dev, NULL);
    D3DVIEWPORT9 vp = { 0, 0, (DWORD)g_out_w, (DWORD)g_out_h, 0.0f, 1.0f };
    dev->lpVtbl->SetViewport(dev, &vp);
    if (dst.w < g_out_w || dst.h < g_out_h)
        dev->lpVtbl->Clear(dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    quad_states(dev, filter);
    if (final_ps) {
        dev->lpVtbl->SetVertexShader(dev, quad_vertex_shader(dev));
        dev->lpVtbl->SetPixelShader(dev, final_ps);
        shader_constants(dev, g_native_w, g_native_h, dst.w, dst.h);
        draw_quad_vs(dev, src, &dst, g_out_w, g_out_h);
        dev->lpVtbl->SetPixelShader(dev, NULL);
        dev->lpVtbl->SetVertexShader(dev, NULL);
    } else draw_quad(dev, src, &dst);
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
    /* Our chain carries the picture; the device's own chain is never shown. */
    if (g_own_present && g_swap) return g_swap->lpVtbl->Present(g_swap, NULL, NULL, wnd, NULL, 0);
    return orig_Present(dev, src, dst, wnd, dirty);
}
/* The game asks for the back buffer to save screenshots and to capture the screen into a
   sprite; both must see what it actually drew on, at the size it expects.
 *
 * It must also be able to *lock* what it gets: the screenshot path locks the surface and
 * writes a 640x480x3 BMP straight out of it, without checking whether the lock succeeded.
 * A render target in the default pool cannot be locked, so handing over our render target
 * made that path write through an uninitialised pointer. Hand over a system-memory copy
 * instead, which is lockable and is equally valid as a D3DX blit source. The copy costs a
 * readback, but only on the rare frames where the game asks for the back buffer at all. */
/* A lockable render target: a graphics-side copy, so refreshing it is a blit rather than a
   readback. The engine asks for the back buffer every frame it has a pending screen capture,
   so this must not stall the pipeline; only the screenshot path actually locks it, and that
   one is welcome to be slow. Falls back to system memory, then to handing over the render
   target itself, which is correct for the capture path even though it cannot be locked. */
static IDirect3DSurface9* lockable_copy(IDirect3DDevice9* dev) {
    if (!g_lockable) {
        if (SUCCEEDED(dev->lpVtbl->CreateRenderTarget(dev, (UINT)g_native_w, (UINT)g_native_h, g_bb_format,
                                                      D3DMULTISAMPLE_NONE, 0, TRUE, &g_lockable, NULL)))
            LOG("scaler: lockable render target for the game's back buffer");
        else if (SUCCEEDED(dev->lpVtbl->CreateOffscreenPlainSurface(dev, (UINT)g_native_w, (UINT)g_native_h,
                                                                    g_bb_format, D3DPOOL_SYSTEMMEM, &g_lockable, NULL))) {
            g_lockable_sysmem = 1;
            LOG("scaler: no lockable render target; using a system-memory copy (slower)");
        } else {
            LOG("scaler: no lockable copy of the game surface available");
            return NULL;
        }
    }
    HRESULT hr = g_lockable_sysmem ? dev->lpVtbl->GetRenderTargetData(dev, g_src_surf, g_lockable)
                                   : dev->lpVtbl->StretchRect(dev, g_src_surf, NULL, g_lockable, NULL, D3DTEXF_NONE);
    return SUCCEEDED(hr) ? g_lockable : NULL;
}
static HRESULT __stdcall hook_GetBackBuffer(IDirect3DDevice9* dev, UINT chain, UINT index, D3DBACKBUFFER_TYPE type, IDirect3DSurface9** out) {
    if (g_scaler_ok && g_src_surf && chain == 0 && index == 0 && type == D3DBACKBUFFER_TYPE_MONO && out) {
        IDirect3DSurface9* give = lockable_copy(dev);
        if (!give) give = g_src_surf;
        /* Logged sparsely: the engine asks once per frame while a capture is pending, so the
           running count is what shows whether this path is hot. */
        ++g_stat_backbuffer;
        if (g_stat_backbuffer <= 3 || g_stat_backbuffer % 600 == 0)
            LOG("scaler: back buffer handed to the game (%u so far; %s)", g_stat_backbuffer,
                give == g_src_surf ? "render target" : (g_lockable_sysmem ? "system memory" : "lockable target"));
        give->lpVtbl->AddRef(give);
        *out = give;
        return D3D_OK;
    }
    return orig_GetBackBuffer(dev, chain, index, type, out);
}
#endif /* HFR_SCALER_TEST_ONLY */
