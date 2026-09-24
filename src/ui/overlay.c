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

/* The i-th sub-steppable class, and how many there are. */
static int ui_sub_class(int i) {
    if (!g_game || i < 0) return -1;
    for (size_t k = 0; k < g_class_count; ++k)
        if (g_classes[k].mode == MODE_SUB && i-- == 0) return (int)k;
    return -1;
}
static int ui_sub_count(void) {
    int n = 0;
    for (size_t k = 0; g_game && k < g_class_count; ++k) n += g_classes[k].mode == MODE_SUB;
    return n;
}
static int ui_fixed_logic(void) { return g_game && g_game->install_presentation != NULL; }
static int ui_has_class(const char* name) {
    for (size_t k = 0; g_game && k < g_class_count; ++k) if (g_classes[k].mode == MODE_SUB && !strcmp(g_classes[k].name, name)) return 1;
    return 0;
}
int hfr_ui_get(int id) {
    switch (id) {
    case UI_VIDEO_AVAILABLE:    return 1;
    case UI_FIXED_LOGIC:        return ui_fixed_logic();
    case UI_SCALING:            return cfg.scaling;
    case UI_FILTER:             return cfg.filter;
    case UI_RESIZABLE:          return cfg.resizable;
    case UI_WINDOW_SCALE:       return cfg.window_scale;
    case UI_SNAP_ASPECT:        return cfg.snap_aspect;
    case UI_FULLSCREEN_MODE:    return cfg.fullscreen_mode;
    case UI_VSYNC:              return cfg.vsync;
    case UI_MAX_FRAME_LATENCY:  return cfg.max_frame_latency;
    case UI_FPS:                return cfg.fps;
    case UI_SUBSTEP:            return ui_fixed_logic() ? cfg.fixed_substep : cfg.substep;
    case UI_PREDICT:            return cfg.predict;
    case UI_PREDICT_AVAILABLE:  return ui_fixed_logic();
    case UI_GAME_SPEED:         return g_speed_pct;
    case UI_SPEED_KEY_SLOWER:   return cfg.speed_slower_key;
    case UI_SPEED_KEY_FASTER:   return cfg.speed_faster_key;
    case UI_SPEED_KEY_RESET:    return cfg.speed_reset_key;
    case UI_REPLAY_SAFE:        return 1;
    /* Which classes this game can actually fade: the background whenever the world's priority
       is known, and every category some rule mentions. It used to answer "all of them", which
       was true of TH10-13 and is not true of a game whose rules are still being written -- and
       a menu slider that does nothing is worse than one that is not offered. */
    case UI_DIM_CLASSES: {
        if (!g_game) return 0;
        int mask = g_game->draw.world_prio > 0 ? (1 << DIM_BACKGROUND) : 0;
        for (size_t i = 0; i < g_game->draw.rule_count; ++i) {
            int c = g_game->draw.rules[i].category;
            if (c >= 0 && c < DIM_COUNT) mask |= 1 << c;
        }
        return mask;
    }
    /* A fixed-logic game has one switch for both: moving the player between frame ticks is
       what takes the scheduler off 60 Hz there, and it polls the input when it does. */
    case UI_SUBTICK_INPUT:      return cfg.subtick_input;
    case UI_ENEMY_INTERP:       return cfg.enemy_interp;
    case UI_DEBUG:              return cfg.debug;
    case UI_D3D9EX:             return g_using_ex;
    case UI_OWN_PRESENT:        return g_own_present;
    case UI_BORDERLESS_ACTIVE:  return g_borderless_active;
    case UI_DIM_BACKGROUND: case UI_DIM_ITEMS: case UI_DIM_EFFECTS: case UI_DIM_SPECIAL: case UI_DIM_PLAYER_SHOTS:
        return cfg.dim[id - UI_DIM_BACKGROUND];
    case UI_DIM_AVAILABLE:      return g_dim_available && g_game && g_game->draw.world_prio > 0;
    /* Sub-stepping is not a switch, it is a set of described systems: with none of them
       classified there is nothing to step a fraction of a frame, and turning it on only makes
       the runner take minor ticks on which every node is skipped -- the game then draws from
       state its own update never advanced. Same for sub-tick input and the two addresses it
       reads and writes. Both are read-only answers about the profile, so the menu greys them
       out and the setters below refuse them however they are asked. */
    case UI_SUBSTEP_AVAILABLE:  return ui_fixed_logic() ? ui_has_class("projectiles") : ui_sub_count() != 0;
    case UI_SUBTICK_AVAILABLE:  return ui_fixed_logic() ? ui_has_class("player") : (g_game && g_game->addr.poll_input && g_game->addr.game_input);
    case UI_SHARPEN:            return cfg.sharpen;
    case UI_SHARPEN_STRENGTH:   return cfg.sharpen_strength;
    case UI_CURSOR:             return cfg.cursor;
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
    case UI_SHARPEN:
        if (value < 0 || value >= post_count()) { cfg.sharpen = -1; snprintf(cfg.sharpen_name, sizeof cfg.sharpen_name, "%s", "none"); }
        else { cfg.sharpen = value; snprintf(cfg.sharpen_name, sizeof cfg.sharpen_name, "%s", post_at(value)->name); }
        break;
    case UI_SHARPEN_STRENGTH: cfg.sharpen_strength = value < 0 ? 0 : (value > 100 ? 100 : value); break;
    case UI_CURSOR:          cfg.cursor = value < 0 ? 0 : (value > 2 ? 2 : value); break;
    case UI_RESIZABLE:       cfg.resizable = !!value; break;
    case UI_WINDOW_SCALE:
        cfg.window_scale = value < -1 ? -1 : (value > 800 ? 800 : value);
        if (cfg.window_scale) g_pending_window = 1;   /* 0 means "leave it": nothing to apply */
        break;
    case UI_SNAP_ASPECT:     cfg.snap_aspect = !!value; break;
    case UI_FULLSCREEN_MODE: cfg.fullscreen_mode = !!value; break;
    case UI_VSYNC:           cfg.vsync = !!value; g_pending_chain = 1; break;
    case UI_FPS:             cfg.fps = value < 0 ? 0 : (value > 1000 ? 1000 : value); g_pending_rate = 1; break;
    case UI_SUBSTEP:
        if (ui_fixed_logic()) {
            cfg.fixed_substep = !!value && hfr_ui_get(UI_SUBSTEP_AVAILABLE);
            cfg.substep = cfg.subtick_input || cfg.fixed_substep; g_pending_rate = 1; break;
        }
        cfg.substep = !!value && hfr_ui_get(UI_SUBSTEP_AVAILABLE); g_pending_rate = 1; break;
    case UI_SUBTICK_INPUT:
        cfg.subtick_input = !!value && hfr_ui_get(UI_SUBTICK_AVAILABLE);
        /* a fixed-logic game leaves 60 Hz when either of its two options wants ticks between frames */
        if (ui_fixed_logic()) { cfg.substep = cfg.subtick_input || cfg.fixed_substep; g_pending_rate = 1; }
        break;
    case UI_PREDICT:         cfg.predict = !!value; break;
    case UI_GAME_SPEED:      set_game_speed(value); break;
    case UI_ENEMY_INTERP:    cfg.enemy_interp = !!value; break;
    case UI_DEBUG:           cfg.debug = !!value; break;
    case UI_DIM_BACKGROUND: case UI_DIM_ITEMS: case UI_DIM_EFFECTS: case UI_DIM_SPECIAL: case UI_DIM_PLAYER_SHOTS:
        cfg.dim[id - UI_DIM_BACKGROUND] = value < 0 ? 0 : (value > 100 ? 100 : value); break;
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
int         hfr_ui_post_count(void) { return post_count(); }
const char* hfr_ui_post_name(int index) { struct Filter* f = post_at(index); return f ? f->name : ""; }
int         hfr_ui_menu_key(void) { return cfg.menu_key; }
const char* hfr_ui_dim_special_name(void) { return g_game ? g_game->draw.special_name : NULL; }
static const struct GameToggle* ui_toggle(int i) { return g_game && i >= 0 && (size_t)i < g_game->toggle_count ? &g_game->toggles[i] : NULL; }
int         hfr_ui_toggle_count(void) { return g_game ? (int)g_game->toggle_count : 0; }
const char* hfr_ui_toggle_label(int i) { const struct GameToggle* g = ui_toggle(i); return g ? g->label : ""; }
const char* hfr_ui_toggle_tip(int i) { const struct GameToggle* g = ui_toggle(i); return g ? g->tip : ""; }
int         hfr_ui_toggle_get(int i) { const struct GameToggle* g = ui_toggle(i); return g ? *g->value : 0; }
void        hfr_ui_toggle_set(int i, int v) { const struct GameToggle* g = ui_toggle(i); if (g) { *g->value = !!v; LOG("menu: %s -> %d", g->key, *g->value); } }
/* The menu's list of sub-stepped systems is the systems that can be sub-stepped, which is not
   the same as the class table: a profile names every callback it has identified so that the
   census has something to report against, and most of them are MODE_FRAME. A checkbox against
   one of those cannot do anything -- node_mode answers MODE_FRAME for it however the switch is
   set -- and an inert checkbox in a panel for narrowing down a problem is worse than no
   checkbox, because it makes a system look ruled out when it was never stepped. */
int         hfr_ui_system_count(void) { return ui_sub_count(); }
const char* hfr_ui_system_name(int i) { int k = ui_sub_class(i); return k < 0 ? "" : g_classes[k].name; }
int         hfr_ui_system_get(int i) { int k = ui_sub_class(i); return k < 0 ? 0 : g_sub_enabled[k]; }
void        hfr_ui_system_set(int i, int v) { int k = ui_sub_class(i); if (k >= 0) g_sub_enabled[k] = !!v; }
const char* hfr_ui_present_path(void) { return g_own_present ? "own swap chain" : "the game's swap chain"; }
/* A stage in progress is recording a replay, and the recording carries the simulation
   settings; changing them part way through would describe the run incorrectly. */
int hfr_ui_simulation_patched(void) {
    return g_game && (g_game->install_presentation || (g_game->addr.runner_fn && g_game->addr.frame_calls[0]));
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
    WritePrivateProfileStringA("video", "sharpen", cfg.sharpen_name, g_ini_path);
    ini_put_int("video", "sharpen_strength", cfg.sharpen_strength);
    ini_put_int("video", "cursor", cfg.cursor);
    ini_put_int("video", "resizable", cfg.resizable);
    ini_put_int("video", "window_scale", cfg.window_scale);
    ini_put_int("video", "snap_aspect", cfg.snap_aspect);
    ini_put_int("video", "fullscreen_mode", cfg.fullscreen_mode);
    for (int i = 0; i < DIM_COUNT; ++i) { char key[32]; snprintf(key, sizeof key, "dim_%s", DIM_NAMES[i]); ini_put_int("video", key, cfg.dim[i]); }
    for (size_t i = 0; g_game && i < g_game->toggle_count; ++i) ini_put_int("game", g_game->toggles[i].key, *g_game->toggles[i].value);
    ini_put_int("hfr", "max_frame_latency", cfg.max_frame_latency);
    ini_put_int("hfr", "fps", cfg.fps);
    ini_put_int("hfr", "vsync", cfg.vsync);
    if (ui_fixed_logic()) {
        /* TH08: smoothing in the section it shares with New Classic, the two gameplay switches
           under the other games' names (its replays carry the rate and the input as theirs do) */
        ini_put_int("fixed60", "interpolate", cfg.enemy_interp);
        ini_put_int("fixed60", "predict", cfg.predict);
        ini_put_int("hfr", "substep", cfg.fixed_substep);
        ini_put_int("hfr", "subtick_input", cfg.subtick_input);
    } else {
        ini_put_int("hfr", "substep", cfg.substep);
        ini_put_int("hfr", "subtick_input", cfg.subtick_input);
        ini_put_int("hfr", "enemy_interp", cfg.enemy_interp);
    }
    ini_put_int("hfr", "debug", cfg.debug);
    /* Only the systems that can be sub-stepped: a `sub_` key for a MODE_FRAME class would be
       read back into a flag nothing consults, and the file is read by people. */
    for (int i = 0; g_game && !ui_fixed_logic() && i < (int)g_class_count; ++i) if (g_classes[i].mode == MODE_SUB) {
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
void hfr_ui_rate_info(char* buf, int len) { snprintf(buf, (size_t)len, "%d ticks/s", g_logic_rate); }
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
