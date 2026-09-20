/* ------------------------------------------------------------------ load-time texture upscaling
 * video.texture_scale = N: every texture the game loads is magnified N times with a pixel-art
 * filter, once, on the GPU, and the magnified copy is what the game samples from then on. With
 * internal_scale the rasteriser has N times the pixels and the sprites sit on sub-pixel
 * positions; this is the half that gives those pixels detail instead of a bilinear blur.
 *
 * How: the game's textures are registered when created (device and D3DX creation hooks). The
 * first time one is bound with SetTexture, a render-target texture N times its size is made
 * and the filter chain runs into it; SetTexture then binds the copy instead. UVs are relative,
 * so the game notices nothing. A texture the game rewrites afterwards -- dialogue text is
 * rendered with GDI and loaded with D3DXLoadSurfaceFromMemory -- is marked dirty by the
 * surface-load hooks and by LockRect, and is redone on its next bind.
 *
 * Alpha: the bundled filters work on colour and write alpha = 1, and a sprite sheet is mostly
 * alpha. So two chains per texture: one over the colour premultiplied by alpha (transparent
 * texels then read as black instead of whatever the sheet left there, which is what the
 * edge detection should see), one over the alpha channel spread to grey; a final pass
 * divides the first by the second and writes the second as alpha. Every filter thereby treats
 * the sprite's silhouette with the same rules as its colours.
 *
 * Excluded: render targets (the game's screen captures), mipmapped textures, anything over
 * 1024 on a side, and everything once a VRAM budget is spent. A texture that cannot be done is
 * simply bound as it was. */

enum { TEX_MAX = 512, TEX_SIDE_MAX = 1024 };
static const size_t TEX_BUDGET = 512u * 1024u * 1024u;
struct TexEntry {
    IDirect3DTexture9* tex;      /* the game's texture (no reference held) */
    IDirect3DSurface9* surf;     /* its level 0, for the surface-load hooks (no reference held) */
    IDirect3DTexture9* up;       /* our magnified copy (owned) */
    int w, h;
    unsigned char eligible, dirty, failed;
};
static struct TexEntry g_tex[TEX_MAX];
static int g_tex_count;
static struct PassTarget g_tpass[MAX_PASSES];
static IDirect3DTexture9* g_tstage[3];      /* premultiplied colour, alpha as grey, filtered colour */
static IDirect3DSurface9* g_tstage_surf[3];
static int g_tstage_w[3], g_tstage_h[3];
static IDirect3DStateBlock9* g_tstate;
static int g_tscale;                         /* effective factor, 0 = off */
static int g_tfilter = -1;                   /* filter index */
static IDirect3DPixelShader9 *g_ps_premul, *g_ps_alpha, *g_ps_combine;
static int g_tps_state;                      /* 0 not tried, 1 ready, -1 failed */

static const char TEX_PREMUL_PS[] =
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 { float4 c = tex2D(Source, uv); return float4(c.rgb * c.a, 1.0); }\n";
static const char TEX_ALPHA_PS[] =
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 { float4 c = tex2D(Source, uv); return float4(c.aaa, 1.0); }\n";
static const char TEX_COMBINE_PS[] =
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 { float3 p = tex2D(Source, uv).rgb; float a = tex2D(Original, uv).r;\n"
    "    return float4(p / max(a, 1.0 / 255.0), a); }\n";

static IDirect3DPixelShader9* tex_compile(IDirect3DDevice9* dev, const char* body) {
    size_t plen = sizeof SHADER_PROLOGUE - 1, blen = strlen(body);
    char* src = (char*)malloc(plen + blen + 1);
    if (!src) return NULL;
    memcpy(src, SHADER_PROLOGUE, plen); memcpy(src + plen, body, blen + 1);
    void *code = NULL, *errors = NULL; IDirect3DPixelShader9* ps = NULL;
    HRESULT hr = d3dx_compile(src, (UINT)(plen + blen), NULL, NULL, "main", g_ps_profile, 0, &code, &errors, NULL);
    if (FAILED(hr) || !code || FAILED(dev->lpVtbl->CreatePixelShader(dev, (const DWORD*)buffer_ptr(code), &ps))) {
        LOG("textures: helper shader failed (0x%08lx): %.*s", (long)hr, errors ? (int)buffer_len(errors) : 0, errors ? (char*)buffer_ptr(errors) : "");
        ps = NULL;
    }
    buffer_free(code); buffer_free(errors); free(src);
    return ps;
}
static int tex_shaders_ready(IDirect3DDevice9* dev) {
    if (g_tps_state) return g_tps_state == 1;
    g_tps_state = -1;
    if (!g_ps_profile || !shaders_load_compiler() || !quad_vertex_shader(dev)) return 0;
    g_ps_premul = tex_compile(dev, TEX_PREMUL_PS);
    g_ps_alpha = tex_compile(dev, TEX_ALPHA_PS);
    g_ps_combine = tex_compile(dev, TEX_COMBINE_PS);
    if (g_ps_premul && g_ps_alpha && g_ps_combine) g_tps_state = 1;
    return g_tps_state == 1;
}
static void tex_release_target(struct PassTarget* t) { SAFE_RELEASE(t->surf); SAFE_RELEASE(t->tex); t->w = t->h = 0; t->fmt = D3DFMT_UNKNOWN; }
static int tex_ensure_target(IDirect3DDevice9* dev, struct PassTarget* t, int w, int h, D3DFORMAT fmt) {
    if (w < 1 || h < 1 || w > 8192 || h > 8192) return 0;
    if (t->tex && t->w == w && t->h == h && t->fmt == fmt) return 1;
    tex_release_target(t);
    if (FAILED(dev->lpVtbl->CreateTexture(dev, (UINT)w, (UINT)h, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &t->tex, NULL)) ||
        FAILED(t->tex->lpVtbl->GetSurfaceLevel(t->tex, 0, &t->surf))) { tex_release_target(t); return 0; }
    t->w = w; t->h = h; t->fmt = fmt;
    return 1;
}
static int tex_ensure_stage(IDirect3DDevice9* dev, int i, int w, int h) {
    if (g_tstage[i] && g_tstage_w[i] == w && g_tstage_h[i] == h) return 1;
    SAFE_RELEASE(g_tstage_surf[i]); SAFE_RELEASE(g_tstage[i]);
    if (FAILED(dev->lpVtbl->CreateTexture(dev, (UINT)w, (UINT)h, 1, D3DUSAGE_RENDERTARGET, PASS_FORMAT, D3DPOOL_DEFAULT, &g_tstage[i], NULL)) ||
        FAILED(g_tstage[i]->lpVtbl->GetSurfaceLevel(g_tstage[i], 0, &g_tstage_surf[i]))) { SAFE_RELEASE(g_tstage_surf[i]); SAFE_RELEASE(g_tstage[i]); return 0; }
    g_tstage_w[i] = w; g_tstage_h[i] = h;
    return 1;
}
/* One full-surface pass: draw src through ps into target. */
static void tex_pass(IDirect3DDevice9* dev, IDirect3DPixelShader9* ps, IDirect3DTexture9* src, int sw, int sh,
                     IDirect3DSurface9* dst, int dw, int dh, IDirect3DTexture9* original) {
    dev->lpVtbl->SetRenderTarget(dev, 0, dst);
    dev->lpVtbl->SetDepthStencilSurface(dev, NULL);
    D3DVIEWPORT9 vp = { 0, 0, (DWORD)dw, (DWORD)dh, 0.0f, 1.0f };
    dev->lpVtbl->SetViewport(dev, &vp);
    quad_states(dev, D3DTEXF_POINT);
    dev->lpVtbl->SetVertexShader(dev, quad_vertex_shader(dev));
    dev->lpVtbl->SetPixelShader(dev, ps);
    shader_constants(dev, sw, sh, dw, dh);
    dev->lpVtbl->SetTexture(dev, 1, (IDirect3DBaseTexture9*)original);
    struct ScaleRect full = { 0, 0, dw, dh };
    draw_quad_vs(dev, src, &full, dw, dh);
}
/* The filter chain over one texture, as run_chain does over the frame, into g_tpass. Returns
   the last pass's texture and its size, or NULL. */
static IDirect3DTexture9* tex_chain(IDirect3DDevice9* dev, struct Filter* f, IDirect3DTexture9* src, int sw, int sh, int* ow, int* oh) {
    IDirect3DTexture9* cur = src; int cw = sw, ch = sh;
    if (f->scale == 0) {   /* a free-scale filter: one pass straight to N times */
        if (!tex_ensure_target(dev, &g_tpass[0], sw * g_tscale, sh * g_tscale, PASS_FORMAT)) return NULL;
        tex_pass(dev, f->pass[0].ps, src, sw, sh, g_tpass[0].surf, g_tpass[0].w, g_tpass[0].h, src);
        *ow = g_tpass[0].w; *oh = g_tpass[0].h;
        return g_tpass[0].tex;
    }
    for (int i = 0; i < f->pass_count; ++i) {
        struct FilterPass* pass = &f->pass[i];
        int w = cw * pass->s.scale, h = ch * pass->s.scale;
        D3DFORMAT fmt = pass->s.want_float ? D3DFMT_A16B16G16R16F : PASS_FORMAT;
        if (!tex_ensure_target(dev, &g_tpass[i], w, h, fmt)) return NULL;
        for (int j = 0; j < i && j < CHAIN_SAMPLERS - 2; ++j)
            dev->lpVtbl->SetTexture(dev, (DWORD)(2 + j), (IDirect3DBaseTexture9*)g_tpass[j].tex);
        tex_pass(dev, pass->ps, cur, cw, ch, g_tpass[i].surf, w, h, src);
        cur = g_tpass[i].tex; cw = w; ch = h;
    }
    *ow = cw; *oh = ch;
    return cur;
}
static int tex_generate(IDirect3DDevice9* dev, struct TexEntry* e) {
    struct Filter* f = filter_at(g_tfilter);
    if (!f || !filter_prepare(dev, f) || !tex_shaders_ready(dev)) return 0;
    int W = e->w * g_tscale, H = e->h * g_tscale;
    int first = !e->up;
    if (!e->up) {
        size_t bytes = (size_t)W * H * 4;
        if (g_tex_bytes + bytes > TEX_BUDGET) { if (!g_stat_tex_done || (g_stat_tex_done % 64) == 0) LOG("textures: budget spent; further textures stay as they are"); return 0; }
        if (FAILED(dev->lpVtbl->CreateTexture(dev, (UINT)W, (UINT)H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &e->up, NULL))) return 0;
        g_tex_bytes += bytes;
    }
    IDirect3DSurface9* dst = NULL;
    if (FAILED(e->up->lpVtbl->GetSurfaceLevel(e->up, 0, &dst))) return 0;
    if (!tex_ensure_stage(dev, 0, e->w, e->h) || !tex_ensure_stage(dev, 1, e->w, e->h) || !tex_ensure_stage(dev, 2, W, H)) { dst->lpVtbl->Release(dst); return 0; }

    /* Save what the game had bound; our SetRenderTarget hook tracks the target, so binding
       the game's surface back at the end restores the internal-resolution state too. */
    IDirect3DSurface9 *rt = NULL, *ds = NULL; D3DVIEWPORT9 vp; int have_vp;
    dev->lpVtbl->GetRenderTarget(dev, 0, &rt); dev->lpVtbl->GetDepthStencilSurface(dev, &ds);
    have_vp = SUCCEEDED(dev->lpVtbl->GetViewport(dev, &vp));
    if (g_tstate) g_tstate->lpVtbl->Capture(g_tstate);
    int ok = 0;
    g_tex_generating++;
    /* 1. premultiplied colour and alpha-as-grey, at the source size */
    tex_pass(dev, g_ps_premul, e->tex, e->w, e->h, g_tstage_surf[0], e->w, e->h, e->tex);
    tex_pass(dev, g_ps_alpha,  e->tex, e->w, e->h, g_tstage_surf[1], e->w, e->h, e->tex);
    /* 2. the filter over each */
    int cw, ch;
    IDirect3DTexture9* col = tex_chain(dev, f, g_tstage[0], e->w, e->h, &cw, &ch);
    if (col) {
        /* keep the filtered colour: the second chain reuses the intermediates */
        if (cw == W && ch == H) dev->lpVtbl->StretchRect(dev, g_tpass[f->scale == 0 ? 0 : f->pass_count - 1].surf, NULL, g_tstage_surf[2], NULL, D3DTEXF_POINT);
        else dev->lpVtbl->StretchRect(dev, g_tpass[f->scale == 0 ? 0 : f->pass_count - 1].surf, NULL, g_tstage_surf[2], NULL, D3DTEXF_LINEAR);
        IDirect3DTexture9* alp = tex_chain(dev, f, g_tstage[1], e->w, e->h, &cw, &ch);
        if (alp) {
            /* 3. combine: colour / alpha, alpha */
            if (cw != W || ch != H) {   /* a fixed-scale filter that does not match N: resample the alpha too */
                if (!tex_ensure_target(dev, &g_tpass[MAX_PASSES - 1], W, H, PASS_FORMAT)) alp = NULL;
                else { dev->lpVtbl->StretchRect(dev, g_tpass[f->scale == 0 ? 0 : f->pass_count - 1].surf, NULL, g_tpass[MAX_PASSES - 1].surf, NULL, D3DTEXF_LINEAR); alp = g_tpass[MAX_PASSES - 1].tex; }
            }
            if (alp) { tex_pass(dev, g_ps_combine, g_tstage[2], W, H, dst, W, H, alp); ok = 1; }
        }
    }
    dev->lpVtbl->SetPixelShader(dev, NULL); dev->lpVtbl->SetVertexShader(dev, NULL);
    for (int i = 0; i < CHAIN_SAMPLERS; ++i) dev->lpVtbl->SetTexture(dev, (DWORD)i, NULL);
    g_tex_generating--;
    dev->lpVtbl->SetRenderTarget(dev, 0, rt); dev->lpVtbl->SetDepthStencilSurface(dev, ds);
    if (have_vp) dev->lpVtbl->SetViewport(dev, &vp);
    if (g_tstate) g_tstate->lpVtbl->Apply(g_tstate);
    SAFE_RELEASE(rt); SAFE_RELEASE(ds); dst->lpVtbl->Release(dst);
    if (ok) { if (first) g_stat_tex_done++; else g_stat_tex_redone++; }
    return ok;
}

static struct TexEntry* tex_find(IDirect3DTexture9* t) {
    for (int i = 0; i < g_tex_count; ++i) if (g_tex[i].tex == t) return &g_tex[i];
    return NULL;
}
static struct TexEntry* tex_find_surface(IDirect3DSurface9* s) {
    for (int i = 0; i < g_tex_count; ++i) if (g_tex[i].surf == s) return &g_tex[i];
    return NULL;
}
static void tex_forget(struct TexEntry* e) {
    if (e->up) { size_t bytes = (size_t)e->w * g_tscale * e->h * g_tscale * 4; g_tex_bytes -= bytes < g_tex_bytes ? bytes : g_tex_bytes; }
    SAFE_RELEASE(e->up);
    *e = g_tex[--g_tex_count];
}
/* Called by the device's CreateTexture hook, which every path -- D3DX included -- goes through. */
static void tex_register(IDirect3DTexture9* t) {
    if (!g_tscale || !t) return;
    struct TexEntry* e = tex_find(t);
    if (e) tex_forget(e);              /* the pointer came back after a free */
    if (g_tex_count >= TEX_MAX) return;
    D3DSURFACE_DESC d;
    if (FAILED(t->lpVtbl->GetLevelDesc(t, 0, &d))) return;
    e = &g_tex[g_tex_count++];
    memset(e, 0, sizeof *e);
    e->tex = t; e->w = (int)d.Width; e->h = (int)d.Height;
    IDirect3DSurface9* s = NULL;
    if (SUCCEEDED(t->lpVtbl->GetSurfaceLevel(t, 0, &s)) && s) { e->surf = s; s->lpVtbl->Release(s); }
    e->eligible = !(d.Usage & D3DUSAGE_RENDERTARGET) && !(d.Usage & D3DUSAGE_DEPTHSTENCIL) &&
                  d.Width <= TEX_SIDE_MAX && d.Height <= TEX_SIDE_MAX &&
                  /* The D3D8-era games load every sheet through a statically linked D3DX8 with its
                     default mip chain, and then draw sprites at 1:1, where only level 0 is ever
                     sampled. Excluding mipmapped textures there would exclude all of them. */
                  (t->lpVtbl->GetLevelCount(t) <= 1 || (g_game && g_game->d3d8));
}
static void tex_dirty_surface(IDirect3DSurface9* s) { struct TexEntry* e = s ? tex_find_surface(s) : NULL; if (e) e->dirty = 1; }
static void tex_dirty_texture(IDirect3DTexture9* t) { struct TexEntry* e = t ? tex_find(t) : NULL; if (e) e->dirty = 1; }
static void tex_released(IDirect3DTexture9* t) { struct TexEntry* e = tex_find(t); if (e) tex_forget(e); }

/* The bind: hand over the copy, making or remaking it first when needed. */
static IDirect3DBaseTexture9* tex_substitute(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* t) {
    if (!g_tscale || g_tex_generating || !t || stage != 0) return t;
    struct TexEntry* e = tex_find((IDirect3DTexture9*)t);
    if (!e || !e->eligible || e->failed) return t;
    if (!e->up || e->dirty) {
        if (!tex_generate(dev, e)) { if (!e->up) e->failed = 1; return t; }
        e->dirty = 0;
    }
    return (IDirect3DBaseTexture9*)e->up;
}
static void texscale_release(void) {
    for (int i = 0; i < g_tex_count; ++i) SAFE_RELEASE(g_tex[i].up);
    g_tex_count = 0; g_tex_bytes = 0;
    for (int i = 0; i < MAX_PASSES; ++i) tex_release_target(&g_tpass[i]);
    for (int i = 0; i < 3; ++i) { SAFE_RELEASE(g_tstage_surf[i]); SAFE_RELEASE(g_tstage[i]); g_tstage_w[i] = g_tstage_h[i] = 0; }
    SAFE_RELEASE(g_tstate);
    SAFE_RELEASE(g_ps_premul); SAFE_RELEASE(g_ps_alpha); SAFE_RELEASE(g_ps_combine); g_tps_state = 0;
}
static void texscale_init(IDirect3DDevice9* dev) {
    g_tscale = 0;
    if (cfg.texture_scale < 2) return;
    if (cfg.texture_scale > 4) cfg.texture_scale = 4;
    g_tfilter = filter_index_by_name(cfg.texture_filter_name);
    struct Filter* f = filter_at(g_tfilter);
    if (!f || g_tfilter < FILTER_BUILTIN_COUNT) {
        LOG("textures: filter \"%s\" is not a shader filter; texture upscaling off", cfg.texture_filter_name);
        return;
    }
    g_tscale = cfg.texture_scale;
    if (FAILED(dev->lpVtbl->CreateStateBlock(dev, D3DSBT_ALL, &g_tstate))) g_tstate = NULL;
    LOG("textures: upscaling x%d with %s at load time%s", g_tscale, f->name,
        f->scale && f->scale != g_tscale ? " (resampled from the filter's own scale)" : "");
}
