/* ------------------------------------------------------------------ in-game overlay
 * Drawn by the scaler inside its own BeginScene, after the game's image has been placed in
 * the back buffer, so the menu renders at the display's resolution instead of being
 * magnified with the game. This file is the runtime's half of the interface in ui_api.h;
 * the menu itself is C++ and knows nothing about the runtime's globals.
 */

static char g_ini_path[MAX_PATH];

/* Changes that touch Direct3D resources or the tick schedule are queued here and applied by
   the frame hook. The menu draws inside the scaler's scene, with the back buffer bound, so
   releasing a swap chain at the moment a checkbox is clicked would pull the ground out from
   under the frame being drawn. */
static int g_pending_chain, g_pending_rate, g_pending_window;

int hfr_ui_get(int id) {
    switch (id) {
    case UI_SCALING:            return cfg.scaling;
    case UI_FILTER:             return cfg.filter;
    case UI_RESIZABLE:          return cfg.resizable;
    case UI_WINDOW_SCALE:       return cfg.window_scale;
    case UI_SNAP_ASPECT:        return cfg.snap_aspect;
    case UI_FULLSCREEN_MODE:    return cfg.fullscreen_mode;
    case UI_VSYNC:              return cfg.vsync;
    case UI_MAX_FRAME_LATENCY:  return cfg.max_frame_latency;
    case UI_FPS:                return cfg.fps;
    case UI_SUBSTEP:            return cfg.substep;
    case UI_SUBTICK_INPUT:      return cfg.subtick_input;
    case UI_ENEMY_INTERP:       return cfg.enemy_interp;
    case UI_DEBUG:              return cfg.debug;
    case UI_D3D9EX:             return g_using_ex;
    case UI_OWN_PRESENT:        return g_own_present;
    case UI_BORDERLESS_ACTIVE:  return g_borderless_active;
    case UI_DIM_BACKGROUND:     return cfg.dim_background;
    case UI_DIM_ITEMS:          return cfg.dim_items;
    case UI_DIM_AVAILABLE:      return g_dim_available && g_game && g_game->draw.world_prio > 0;
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
    case UI_WINDOW_SCALE:
        cfg.window_scale = value < -1 ? -1 : (value > 800 ? 800 : value);
        if (cfg.window_scale) g_pending_window = 1;   /* 0 means "leave it": nothing to apply */
        break;
    case UI_SNAP_ASPECT:     cfg.snap_aspect = !!value; break;
    case UI_FULLSCREEN_MODE: cfg.fullscreen_mode = !!value; break;
    case UI_VSYNC:           cfg.vsync = !!value; g_pending_chain = 1; break;
    case UI_FPS:             cfg.fps = value < 0 ? 0 : (value > 1000 ? 1000 : value); g_pending_rate = 1; break;
    case UI_SUBSTEP:         cfg.substep = !!value; g_pending_rate = 1; break;
    case UI_SUBTICK_INPUT:   cfg.subtick_input = !!value; break;
    case UI_ENEMY_INTERP:    cfg.enemy_interp = !!value; break;
    case UI_DEBUG:           cfg.debug = !!value; break;
    case UI_DIM_BACKGROUND:  cfg.dim_background = value < 0 ? 0 : (value > 100 ? 100 : value); break;
    case UI_DIM_ITEMS:       cfg.dim_items = value < 0 ? 0 : (value > 100 ? 100 : value); break;
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
int         hfr_ui_system_count(void) { return g_game ? (int)g_class_count : 0; }
const char* hfr_ui_system_name(int i) { return (g_game && i >= 0 && i < (int)g_class_count) ? g_classes[i].name : ""; }
int         hfr_ui_system_get(int i) { return (i >= 0 && i < (int)g_class_count) ? g_sub_enabled[i] : 0; }
void        hfr_ui_system_set(int i, int v) { if (i >= 0 && i < (int)g_class_count) g_sub_enabled[i] = !!v; }
const char* hfr_ui_present_path(void) { return g_own_present ? "own swap chain" : "the game's swap chain"; }
/* A stage in progress is recording a replay, and the recording carries the simulation
   settings; changing them part way through would describe the run incorrectly. */
int hfr_ui_simulation_patched(void) {
    return g_game && g_game->addr.runner_fn && g_game->addr.frame_calls[0];
}
int hfr_ui_simulation_locked(void) {
    if (g_replay_playing) return 1;
    uint8_t* rm = g_game && g_game->addr.replay_manager ? G_REPLAY_MANAGER : NULL;
    return rm && *(int*)(rm + g_game->layout.replay_frame) >= 0;
}
/* Called from the frame hook, between frames, where touching Direct3D is safe. */
static void hfr_ui_apply_pending(IDirect3DDevice9* dev) {
    if (g_pending_rate) {
        g_pending_rate = 0;
        int want = cfg.fps > 0 ? cfg.fps : g_display_hz;
        LOG("menu: tick rate -> %d (substep=%d)", want, cfg.substep);
        recompute_rate(want);
    }
    if (g_pending_window) {
        g_pending_window = 0;
        if (g_wnd && !g_borderless_active) window_apply_scale(cfg.window_scale);
    }
    if (g_pending_chain && dev && g_own_present && g_wnd) {
        g_pending_chain = 0;
        LOG("menu: rebuilding the presentation chain (vsync=%d)", cfg.vsync);
        scaler_set_output(dev, g_wnd, g_out_w, g_out_h);
    } else g_pending_chain = 0;
}
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
    ini_put_int("video", "window_scale", cfg.window_scale);
    ini_put_int("video", "snap_aspect", cfg.snap_aspect);
    ini_put_int("video", "fullscreen_mode", cfg.fullscreen_mode);
    ini_put_int("video", "dim_background", cfg.dim_background);
    ini_put_int("video", "dim_items", cfg.dim_items);
    ini_put_int("hfr", "max_frame_latency", cfg.max_frame_latency);
    ini_put_int("hfr", "fps", cfg.fps);
    ini_put_int("hfr", "vsync", cfg.vsync);
    ini_put_int("hfr", "substep", cfg.substep);
    ini_put_int("hfr", "subtick_input", cfg.subtick_input);
    ini_put_int("hfr", "enemy_interp", cfg.enemy_interp);
    ini_put_int("hfr", "debug", cfg.debug);
    for (int i = 0; g_game && i < (int)g_class_count; ++i) {
        char key[64]; snprintf(key, sizeof key, "sub_%s", g_classes[i].name);
        ini_put_int("systems", key, g_sub_enabled[i]);
    }
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
