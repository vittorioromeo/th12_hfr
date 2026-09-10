/* ------------------------------------------------------------------ in-game overlay
 * Drawn by the scaler inside its own BeginScene, after the game's image has been placed in
 * the back buffer, so the menu renders at the display's resolution instead of being
 * magnified with the game. This file is the runtime's half of the interface in ui_api.h;
 * the menu itself is C++ and knows nothing about the runtime's globals.
 */

static char g_ini_path[MAX_PATH];

int hfr_ui_get(int id) {
    switch (id) {
    case UI_SCALING:            return cfg.scaling;
    case UI_FILTER:             return cfg.filter;
    case UI_RESIZABLE:          return cfg.resizable;
    case UI_SNAP_ASPECT:        return cfg.snap_aspect;
    case UI_FULLSCREEN_MODE:    return cfg.fullscreen_mode;
    case UI_VSYNC:              return cfg.vsync;
    case UI_MAX_FRAME_LATENCY:  return cfg.max_frame_latency;
    default:                    return 0;
    }
}
void hfr_ui_set(int id, int value) {
    switch (id) {
    case UI_SCALING:         cfg.scaling = value < 0 ? 0 : (value > SCALE_INTEGER ? SCALE_INTEGER : value); break;
    case UI_FILTER:
        if (value >= 0 && value < filter_count()) {
            cfg.filter = value;
            snprintf(cfg.filter_name, sizeof cfg.filter_name, "%s", filter_at(value)->name);
        }
        break;
    case UI_RESIZABLE:       cfg.resizable = !!value; break;
    case UI_SNAP_ASPECT:     cfg.snap_aspect = !!value; break;
    case UI_FULLSCREEN_MODE: cfg.fullscreen_mode = !!value; break;
    case UI_VSYNC:           cfg.vsync = !!value; break;
    case UI_MAX_FRAME_LATENCY:
        cfg.max_frame_latency = value < 0 ? 0 : (value > 16 ? 16 : value);
        if (g_using_ex && g_dev && cfg.max_frame_latency > 0) {
            IDirect3DDevice9Ex* ex = (IDirect3DDevice9Ex*)g_dev;
            ex->lpVtbl->SetMaximumFrameLatency(ex, (UINT)cfg.max_frame_latency);
        }
        break;
    default: break;
    }
}
int         hfr_ui_filter_count(void) { return filter_count(); }
const char* hfr_ui_filter_name(int index) { struct Filter* f = filter_at(index); return f ? f->name : ""; }
int         hfr_ui_filter_is_fixed_scale(int index) { struct Filter* f = filter_at(index); return f && f->scale > 0; }
int         hfr_ui_menu_key(void) { return cfg.menu_key; }
void hfr_ui_report(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

static void ini_put_int(const char* section, const char* key, int value) {
    char buf[32]; snprintf(buf, sizeof buf, "%d", value);
    WritePrivateProfileStringA(section, key, buf, g_ini_path);
}
void hfr_ui_save(void) {
    if (!g_ini_path[0]) { LOG("menu: no INI path; nothing saved"); return; }
    ini_put_int("video", "scaling", cfg.scaling);
    WritePrivateProfileStringA("video", "filter", cfg.filter_name, g_ini_path);
    ini_put_int("video", "resizable", cfg.resizable);
    ini_put_int("video", "snap_aspect", cfg.snap_aspect);
    ini_put_int("video", "fullscreen_mode", cfg.fullscreen_mode);
    ini_put_int("hfr", "max_frame_latency", cfg.max_frame_latency);
    LOG("menu: settings written to %s", g_ini_path);
}
void hfr_ui_status(char* buf, int len) {
    snprintf(buf, (size_t)len, "%s   %d x %d window   %d Hz logic / %d Hz present",
             g_game ? g_game->identity->name : "no game", g_out_w, g_out_h, g_logic_rate, g_refresh);
}
/* What the current settings actually produce, which is more use than the settings alone. */
void hfr_ui_scale_info(char* buf, int len) {
    struct ScaleRect r = scale_rect(g_native_w, g_native_h, g_out_w, g_out_h, cfg.scaling);
    double sx = g_native_w > 0 ? (double)r.w / g_native_w : 0.0;
    int bars_x = g_out_w - r.w, bars_y = g_out_h - r.h;
    if (bars_x || bars_y)
        snprintf(buf, (size_t)len, "%dx%d game image at %.3gx, %d x %d of black bars", g_native_w, g_native_h, sx, bars_x, bars_y);
    else
        snprintf(buf, (size_t)len, "%dx%d game image at %.3gx, filling the window", g_native_w, g_native_h, sx);
}

static void ui_render_frame(IDirect3DDevice9* dev, const struct ScaleRect* content) {
    (void)content;
    hfr_menu_render(dev, g_out_w, g_out_h);
}
