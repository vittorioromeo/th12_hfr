/* ------------------------------------------------------------------ filter shaders
 * Filters are Direct3D 9 pixel shaders compiled at run time by the d3dx9 the game already
 * ships, so no shader bytecode has to be built or shipped. Two are built into the DLL;
 * any .hlsl file dropped in the game's shaders/ folder is offered as well, and a file
 * there overrides a built-in of the same name.
 *
 * Every filter is compiled against this prologue and must define
 *     float4 main(float2 uv : TEXCOORD0) : COLOR0
 * The quad is drawn with the Direct3D 9 half-pixel offset, so uv arrives at output-pixel
 * centres: for a target that is an exact multiple of the source, frac(uv * SourceSize.xy)
 * identifies the sub-pixel. Fixed-scale filters such as MMPX depend on that.
 */
#include "shader_sources.h"
#include "shader_parse.h"

enum { MAX_FILTERS = 32, MAX_POSTS = 16, FILTER_NAME_MAX = 32, MAX_PASSES = HFR_MAX_PASSES };

/* A filter is one or more pixel shader passes. Everything before the first "//! pass" is a
   shared header compiled into every pass, which is what lets the passes of an algorithm
   share their helper functions instead of repeating them. A file with no "//! pass" at all
   is a single pass, so every shader written before this existed still works unchanged. */
struct FilterPass {
    struct ShaderPass s;            /* body, length, scale and float flag, from shader_parse.h */
    IDirect3DPixelShader9* ps;
};
struct Filter {
    char  name[FILTER_NAME_MAX];
    int   scale;                    /* total magnification; 0 = drawn straight to the destination */
    int   pass_count;
    struct FilterPass pass[MAX_PASSES];
    const char* embedded;           /* built-in source, or NULL for a file */
    char  path[MAX_PATH];           /* file source, when not embedded */
    char* text;                     /* the source, kept while the passes point into it */
    const char* header;             /* shared header, into text */
    size_t header_len;
    int   state;                    /* 0 not tried, 1 ready, -1 failed */
    int   post;                     /* a post-process (sharpening) rather than a filter */
};
/* Direct3D 9 does not allow a ps_3_0 pixel shader with the fixed-function vertex pipeline,
   so a shader filter is drawn through this pass-through vertex shader with clip-space
   vertices instead of the pre-transformed ones the built-in filters use. */
static const char PASSTHROUGH_VS[] =
    "void main(float4 p : POSITION, float2 t : TEXCOORD0,\n"
    "          out float4 op : POSITION, out float2 ot : TEXCOORD0) { op = p; ot = t; }\n";
static IDirect3DVertexShader9* g_quad_vs;
static int g_quad_vs_state;

/* A fixed-scale filter magnifies by a whole number, which is usually more than the window
   asks for: ScaleFX produces a 3x image, and a 1.5x window then has to throw half of it
   away. Taking one bilinear sample per destination pixel does that by picking two source
   pixels out of every three, which turns every filtered edge into a dotted line -- the
   filter looks broken when the resample is what is wrong. Four bilinear taps at the quarter
   points of the destination pixel's footprint average that footprint instead. */
static const char DOWNSAMPLE_PS[] =
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    "    float2 o = 0.25 * (SourceSize.xy * TargetSize.zw) * SourceSize.zw;\n"
    "    float4 c  = tex2D(Source, uv + float2(-o.x, -o.y));\n"
    "    c += tex2D(Source, uv + float2( o.x, -o.y));\n"
    "    c += tex2D(Source, uv + float2(-o.x,  o.y));\n"
    "    c += tex2D(Source, uv + float2( o.x,  o.y));\n"
    "    return c * 0.25;\n"
    "}\n";
static IDirect3DPixelShader9* g_downsample_ps;
static int g_downsample_state;

static struct Filter g_filters[MAX_FILTERS];
static int g_filter_count;
/* Post-processes: single passes run over the finished image at the window's size, after
   the filter and the resample, with a strength. Kept apart from the filters because they
   are chosen separately -- a filter decides how the game's pixels are magnified, a
   post-process what is done to the result -- and the two lists never mix in the menu. */
static struct Filter g_posts[MAX_POSTS];
static int g_post_count;
static int g_filters_scanned;
static const char* g_ps_profile;

typedef HRESULT (WINAPI *D3DXCompileShaderFn)(LPCSTR, UINT, const void*, void*, LPCSTR, LPCSTR, DWORD, void**, void**, void**);
static D3DXCompileShaderFn d3dx_compile;

static void* buffer_ptr(void* b) { return ((void* (WINAPI*)(void*))(*(void***)b)[3])(b); }
static DWORD buffer_len(void* b) { return ((DWORD (WINAPI*)(void*))(*(void***)b)[4])(b); }
static void  buffer_free(void* b) { if (b) ((ULONG (WINAPI*)(void*))(*(void***)b)[2])(b); }

/* Which d3dx9 compiles the filters is not a matter of taste. The one the game imports can be
   very old -- TH10 ships d3dx9_31, from 2006, whose HLSL compiler rejects an early return
   inside an if ("error X3500: asymetric returns from if statements not yet implemented") and
   so cannot build MMPX or Super-xBR. Newer is not automatically better either: from
   d3dx9_42 onward D3DXCompileShader forwards to a separate D3DCompiler_NN.dll that may not be
   installed, and then it fails too.
   So do not reason about versions at all -- ask each candidate to compile something that uses
   the features the bundled filters use, and take the first that can. */
static const char COMPILER_PROBE[] =
    "sampler2D S : register(s0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    "    if (uv.x > 0.5) return tex2D(S, uv);\n"   /* the early return old compilers reject */
    "    float4 c = 0;\n"
    "    for (int i = 0; i < 3; ++i) c += tex2D(S, uv + i * 0.01);\n"
    "    return c / 3;\n"
    "}\n";
static int compiler_works(D3DXCompileShaderFn fn) {
    void *code = NULL, *errors = NULL;
    HRESULT hr = fn(COMPILER_PROBE, (UINT)(sizeof COMPILER_PROBE - 1), NULL, NULL, "main",
                    g_ps_profile ? g_ps_profile : "ps_3_0", 0, &code, &errors, NULL);
    int ok = SUCCEEDED(hr) && code != NULL;
    buffer_free(code); buffer_free(errors);
    return ok;
}
static int shaders_load_compiler(void) {
    if (d3dx_compile) return 1;
    const char* first = g_game ? g_game->d3dx : NULL;
    char name[32];
    D3DXCompileShaderFn fallback = NULL;
    const char* fallback_name = NULL;

    /* Newest first, then the game's own, so a capable compiler is preferred but the one that
       is certainly present is still used if it is the only one that works. */
    for (int i = 43; i >= 23; --i) {
        const char* dll;
        if (i == 23) { if (!first) continue; dll = first; }
        else { snprintf(name, sizeof name, "d3dx9_%d.dll", i); dll = name; }
        HMODULE m = GetModuleHandleA(dll);
        if (!m) m = LoadLibraryA(dll);
        if (!m) continue;
        D3DXCompileShaderFn fn = (D3DXCompileShaderFn)GetProcAddress(m, "D3DXCompileShader");
        if (!fn) continue;
        if (compiler_works(fn)) {
            d3dx_compile = fn;
            LOG("shaders: compiling with %s", dll);
            return 1;
        }
        if (!fallback) { fallback = fn; fallback_name = dll; }
        LOG("shaders: %s has a compiler but it cannot build the bundled filters; looking further", dll);
    }
    if (fallback) {
        d3dx_compile = fallback;
        LOG("shaders: falling back to %s; some filters will not compile", fallback_name);
        return 1;
    }
    LOG("shaders: no d3dx9 with D3DXCompileShader; shader filters unavailable");
    return 0;
}
/* Pick the best profile the device actually supports; ps_2_a is enough for some filters. */
static void shaders_pick_profile(IDirect3DDevice9* dev) {
    D3DCAPS9 caps;
    g_ps_profile = "ps_3_0";
    if (dev && SUCCEEDED(dev->lpVtbl->GetDeviceCaps(dev, &caps))) {
        DWORD v = caps.PixelShaderVersion;
        unsigned major = (v >> 8) & 0xff, minor = v & 0xff;
        if (major < 2) { g_ps_profile = NULL; LOG("shaders: device reports pixel shader %u.%u; shader filters unavailable", major, minor); return; }
        if (major == 2) g_ps_profile = (caps.PS20Caps.NumInstructionSlots >= 512) ? "ps_2_a" : "ps_2_0";
    }
    LOG("shaders: target profile %s", g_ps_profile ? g_ps_profile : "none");
}

static int filter_add_to(struct Filter* list, int* count, int max, int first, const char* name, int scale, const char* embedded, const char* path, int post) {
    if (*count >= max) return 0;
    for (int i = first; i < *count; ++i)
        if (!_stricmp(list[i].name, name)) {          /* a file replaces a built-in */
            list[i].embedded = embedded;
            if (path) { snprintf(list[i].path, sizeof list[i].path, "%s", path); list[i].embedded = NULL; }
            if (scale >= 0) list[i].scale = scale;
            return 1;
        }
    struct Filter* f = &list[(*count)++];
    memset(f, 0, sizeof *f);
    snprintf(f->name, sizeof f->name, "%s", name);
    f->scale = scale < 0 ? 0 : scale;
    f->embedded = embedded;
    f->post = post;
    if (path) snprintf(f->path, sizeof f->path, "%s", path);
    return 1;
}
/* A file that says "//! post" goes to the post-process list; anything else is a filter. */
static int filter_add(const char* name, int scale, const char* embedded, const char* path, const char* text) {
    if (text && shader_is_post(text)) return filter_add_to(g_posts, &g_post_count, MAX_POSTS, 0, name, 0, embedded, path, 1);
    return filter_add_to(g_filters, &g_filter_count, MAX_FILTERS, FILTER_BUILTIN_COUNT, name, scale, embedded, path, 0);
}
/* Fills in the filter's header and passes. Returns 0 when the shader is malformed. */
static int parse_passes(struct Filter* f, const char* text) {
    struct ShaderPass passes[MAX_PASSES];
    const char* err = NULL;
    int n = shader_split(text, &f->header, &f->header_len, passes, &f->scale, &err);
    if (!n) { LOG("shaders: %s %s", f->name, err ? err : "could not be parsed"); return 0; }
    f->pass_count = n;
    for (int i = 0; i < n; ++i) { f->pass[i].s = passes[i]; f->pass[i].ps = NULL; }
    return 1;
}
/* The scan only needs to know how much a filter magnifies, so it parses and throws away. */
static int parse_total_scale(const char* name, const char* text) {
    struct Filter tmp;
    memset(&tmp, 0, sizeof tmp);
    snprintf(tmp.name, sizeof tmp.name, "%s", name);
    return parse_passes(&tmp, text) ? tmp.scale : 0;
}
static char* read_text_file(const char* path, size_t* len) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(h, NULL), rd = 0;
    char* d = (sz > 0 && sz < 1024 * 1024) ? (char*)malloc(sz + 1) : NULL;
    if (!d || !ReadFile(h, d, sz, &rd, NULL) || rd != sz) { free(d); CloseHandle(h); return NULL; }
    d[sz] = 0; CloseHandle(h);
    if (len) *len = sz;
    return d;
}
static void shaders_scan(void) {
    if (g_filters_scanned) return;
    g_filters_scanned = 1;
    g_filter_count = FILTER_BUILTIN_COUNT;
    memset(g_filters, 0, sizeof g_filters);
    g_post_count = 0;
    memset(g_posts, 0, sizeof g_posts);
    snprintf(g_filters[FILTER_NEAREST].name, FILTER_NAME_MAX, "%s", "nearest");
    snprintf(g_filters[FILTER_BILINEAR].name, FILTER_NAME_MAX, "%s", "bilinear");
    snprintf(g_filters[FILTER_SHARP].name, FILTER_NAME_MAX, "%s", "sharp-bilinear");
    for (size_t i = 0; i < sizeof embedded_shaders / sizeof *embedded_shaders; ++i)
        filter_add(embedded_shaders[i].name, embedded_shaders[i].scale, embedded_shaders[i].source, NULL, embedded_shaders[i].source);
    char dir[MAX_PATH], pattern[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char* p = strrchr(dir, '\\');
    if (p) *p = 0; else dir[0] = 0;
    snprintf(pattern, sizeof pattern, "%s\\shaders\\*.hlsl", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { LOG("shaders: %d built-in filter(s) and %d post-process(es), no shaders folder", g_filter_count - FILTER_BUILTIN_COUNT, g_post_count); return; }
    int added = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char full[MAX_PATH], name[FILTER_NAME_MAX];
        snprintf(full, sizeof full, "%s\\shaders\\%s", dir, fd.cFileName);
        snprintf(name, sizeof name, "%s", fd.cFileName);
        char* dot = strrchr(name, '.'); if (dot) *dot = 0;
        size_t len = 0; char* text = read_text_file(full, &len);
        if (!text) { LOG("shaders: cannot read %s", fd.cFileName); continue; }
        filter_add(name, parse_total_scale(name, text), NULL, full, text);
        free(text);
        ++added;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    LOG("shaders: %d filter(s) and %d post-process(es) available (%d files in the shaders folder)", g_filter_count, g_post_count, added);
}
/* Compiled once alongside the first filter shader; a failure disables shader filters. */
static IDirect3DVertexShader9* quad_vertex_shader(IDirect3DDevice9* dev) {
    if (g_quad_vs_state) return g_quad_vs;
    g_quad_vs_state = -1;
    if (!dev || !g_ps_profile || !shaders_load_compiler()) return NULL;
    const char* profile = g_ps_profile[3] == '3' ? "vs_3_0" : "vs_2_0";
    void *code = NULL, *errors = NULL;
    HRESULT hr = d3dx_compile(PASSTHROUGH_VS, (UINT)(sizeof PASSTHROUGH_VS - 1), NULL, NULL, "main", profile, 0, &code, &errors, NULL);
    if (SUCCEEDED(hr) && code) {
        hr = dev->lpVtbl->CreateVertexShader(dev, (const DWORD*)buffer_ptr(code), &g_quad_vs);
        if (SUCCEEDED(hr)) { g_quad_vs_state = 1; LOG("shaders: %s pass-through vertex shader ready", profile); }
    }
    if (g_quad_vs_state != 1)
        LOG("shaders: %s pass-through vertex shader failed (0x%08lx): %.*s", profile, (long)hr,
            errors ? (int)buffer_len(errors) : 0, errors ? (char*)buffer_ptr(errors) : "");
    buffer_free(code); buffer_free(errors);
    return g_quad_vs;
}
/* Compiled on demand, and only used when a chain overshoots the window. */
static IDirect3DPixelShader9* downsample_shader(IDirect3DDevice9* dev) {
    if (g_downsample_state) return g_downsample_ps;
    g_downsample_state = -1;
    if (!dev || !g_ps_profile || !shaders_load_compiler()) return NULL;
    size_t plen = sizeof SHADER_PROLOGUE - 1, blen = sizeof DOWNSAMPLE_PS - 1;
    char* src = (char*)malloc(plen + blen + 1);
    if (!src) return NULL;
    memcpy(src, SHADER_PROLOGUE, plen);
    memcpy(src + plen, DOWNSAMPLE_PS, blen + 1);
    void *code = NULL, *errors = NULL;
    HRESULT hr = d3dx_compile(src, (UINT)(plen + blen), NULL, NULL, "main", g_ps_profile, 0, &code, &errors, NULL);
    if (SUCCEEDED(hr) && code &&
        SUCCEEDED(dev->lpVtbl->CreatePixelShader(dev, (const DWORD*)buffer_ptr(code), &g_downsample_ps))) {
        g_downsample_state = 1;
        LOG("shaders: box downsample ready (%s)", g_ps_profile);
    } else {
        LOG("shaders: box downsample unavailable (0x%08lx): %.*s", (long)hr,
            errors ? (int)buffer_len(errors) : 0, errors ? (char*)buffer_ptr(errors) : "");
    }
    buffer_free(code); buffer_free(errors); free(src);
    return g_downsample_state == 1 ? g_downsample_ps : NULL;
}
static void filters_release(void) {
    if (g_quad_vs) { g_quad_vs->lpVtbl->Release(g_quad_vs); g_quad_vs = NULL; }
    g_quad_vs_state = 0;
    if (g_downsample_ps) { g_downsample_ps->lpVtbl->Release(g_downsample_ps); g_downsample_ps = NULL; }
    g_downsample_state = 0;
    for (int i = 0; i < g_filter_count + g_post_count; ++i) {
        struct Filter* f = i < g_filter_count ? &g_filters[i] : &g_posts[i - g_filter_count];
        for (int j = 0; j < f->pass_count; ++j)
            if (f->pass[j].ps) { f->pass[j].ps->lpVtbl->Release(f->pass[j].ps); f->pass[j].ps = NULL; }
        free(f->text); f->text = NULL;
        f->pass_count = 0; f->state = 0;
    }
}
/* Compile on first use, and remember failure so a broken shader is reported once. Every
   pass sees the same shared header, so an algorithm's passes share their helper functions. */
static int compile_pass(IDirect3DDevice9* dev, struct Filter* f, int index) {
    struct FilterPass* pass = &f->pass[index];
    size_t plen = sizeof SHADER_PROLOGUE - 1;
    size_t total = plen + f->header_len + pass->s.len + 1;
    char* src = (char*)malloc(total);
    if (!src) return 0;
    memcpy(src, SHADER_PROLOGUE, plen);
    memcpy(src + plen, f->header, f->header_len);
    memcpy(src + plen + f->header_len, pass->s.body, pass->s.len);
    src[plen + f->header_len + pass->s.len] = 0;

    void *code = NULL, *errors = NULL;
    HRESULT hr = d3dx_compile(src, (UINT)(total - 1), NULL, NULL, "main", g_ps_profile, 0, &code, &errors, NULL);
    int ok = 0;
    if (FAILED(hr) || !code) {
        LOG("shaders: %s pass %d failed to compile as %s (0x%08lx): %.*s", f->name, index, g_ps_profile, (long)hr,
            errors ? (int)buffer_len(errors) : 0, errors ? (char*)buffer_ptr(errors) : "");
    } else {
        hr = dev->lpVtbl->CreatePixelShader(dev, (const DWORD*)buffer_ptr(code), &pass->ps);
        if (FAILED(hr)) LOG("shaders: %s pass %d compiled but the device refused it (0x%08lx)", f->name, index, (long)hr);
        else { ok = 1; LOG("shaders: %s pass %d ready (%s, %lu bytes, scale %d%s)", f->name, index, g_ps_profile,
                           (unsigned long)buffer_len(code), pass->s.scale, pass->s.want_float ? ", float target" : ""); }
    }
    buffer_free(code); buffer_free(errors); free(src);
    return ok;
}
static int filter_prepare(IDirect3DDevice9* dev, struct Filter* f) {
    if (f->state) return f->state == 1;
    f->state = -1;
    if (!g_ps_profile || !shaders_load_compiler() || !dev) return 0;

    /* The passes point into this text, so the filter owns it for as long as it is compiled. */
    f->text = f->embedded ? _strdup(f->embedded) : read_text_file(f->path, NULL);
    if (!f->text) { LOG("shaders: %s unreadable", f->name); return 0; }
    if (!parse_passes(f, f->text)) { free(f->text); f->text = NULL; return 0; }

    for (int i = 0; i < f->pass_count; ++i)
        if (!compile_pass(dev, f, i)) {
            for (int j = 0; j < i; ++j) { f->pass[j].ps->lpVtbl->Release(f->pass[j].ps); f->pass[j].ps = NULL; }
            free(f->text); f->text = NULL; f->pass_count = 0;
            return 0;
        }
    if (f->post && (f->pass_count != 1 || f->pass[0].s.scale > 1 || f->pass[0].s.want_float)) {
        LOG("shaders: %s is a post-process, which must be a single pass at the window's size", f->name);
        f->pass[0].ps->lpVtbl->Release(f->pass[0].ps); f->pass[0].ps = NULL;
        free(f->text); f->text = NULL; f->pass_count = 0;
        return 0;
    }
    f->state = 1;
    if (f->pass_count > 1) LOG("shaders: %s ready (%d passes, %dx overall)", f->name, f->pass_count, f->scale);
    return 1;
}
static struct Filter* filter_at(int index) {
    shaders_scan();
    if (index < 0 || index >= g_filter_count) return NULL;
    return &g_filters[index];
}
static int filter_count(void) { shaders_scan(); return g_filter_count; }
static struct Filter* post_at(int index) {
    shaders_scan();
    if (index < 0 || index >= g_post_count) return NULL;
    return &g_posts[index];
}
static int post_count(void) { shaders_scan(); return g_post_count; }
static int post_index_by_name(const char* name) {
    shaders_scan();
    if (!name || !*name) return -1;
    for (int i = 0; i < g_post_count; ++i) if (!_stricmp(g_posts[i].name, name)) return i;
    return -1;
}
/* Resolve a name from the INI to an index, so filters keep their identity when the
   shaders folder changes. Falls back to the numeric form for compatibility. */
static int filter_index_by_name(const char* name) {
    shaders_scan();
    if (!name || !*name) return -1;
    for (int i = 0; i < g_filter_count; ++i) if (!_stricmp(g_filters[i].name, name)) return i;
    char* end = NULL;
    long n = strtol(name, &end, 10);
    if (end && end != name && !*end && n >= 0 && n < g_filter_count) return (int)n;
    return -1;
}
/* Turn the INI's filter name into an index once the registry is known. */
static void resolve_filter(void) {
    int n = filter_count();
    int idx = filter_index_by_name(cfg.filter_name);
    if (idx < 0 && cfg.filter_name[0])
        LOG("video: no filter named '%s'; using %s", cfg.filter_name, g_filters[FILTER_SHARP].name);
    if (idx < 0) idx = (cfg.filter >= 0 && cfg.filter < n) ? cfg.filter : FILTER_SHARP;
    cfg.filter = idx;
    snprintf(cfg.filter_name, sizeof cfg.filter_name, "%s", g_filters[idx].name);
    for (int i = 0; i < n; ++i)
        LOG("video: filter %d = %s%s%s", i, g_filters[i].name,
            g_filters[i].scale ? " (fixed scale)" : "", i == idx ? "  <- selected" : "");
    /* The post-process is optional: "none" (or an unknown name) means off. */
    int pn = post_count();
    cfg.sharpen = post_index_by_name(cfg.sharpen_name);
    if (cfg.sharpen < 0 && cfg.sharpen_name[0] && _stricmp(cfg.sharpen_name, "none"))
        LOG("video: no post-process named '%s'; sharpening off", cfg.sharpen_name);
    if (cfg.sharpen < 0) snprintf(cfg.sharpen_name, sizeof cfg.sharpen_name, "%s", "none");
    for (int i = 0; i < pn; ++i)
        LOG("video: post-process %d = %s%s", i, g_posts[i].name, i == cfg.sharpen ? "  <- selected" : "");
    if (cfg.sharpen >= 0) LOG("video: sharpening with %s at %d%%", cfg.sharpen_name, cfg.sharpen_strength);
}
