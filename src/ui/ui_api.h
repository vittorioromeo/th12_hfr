/* The narrow interface between the C runtime and the C++ menu. The menu never sees the
   runtime's globals; it reads and writes settings through these, so the two sides can be
   compiled separately and the menu can be left out of a build entirely. */
#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    UI_SCALING = 0,          /* SCALE_STRETCH / SCALE_ASPECT / SCALE_INTEGER */
    UI_FILTER,               /* index into the filter registry */
    UI_RESIZABLE,
    UI_WINDOW_SCALE,         /* percent of the game's own size; 0 as the game made it, -1 largest fit */
    UI_SNAP_ASPECT,
    UI_FULLSCREEN_MODE,      /* 0 leave the window alone, 1 borderless over the monitor */
    UI_VSYNC,
    UI_MAX_FRAME_LATENCY,
    UI_FPS,                  /* ticks per second; 0 follows the display */
    UI_SUBSTEP,
    UI_SUBTICK_INPUT,
    UI_ENEMY_INTERP,
    UI_DEBUG,
    UI_D3D9EX,               /* fixed once the device exists */
    UI_OWN_PRESENT,          /* likewise */
    UI_BORDERLESS_ACTIVE,    /* read-only: the window is currently covering the monitor */
    UI_DIM_BACKGROUND,       /* percent the stage background fades towards black */
    UI_DIM_ITEMS,            /* percent the pickups fade towards transparent */
    UI_DIM_EFFECTS,          /* ... the cosmetic effects: explosions, particles, hit sparks */
    UI_DIM_SPECIAL,          /* ... the game's own extra class (hfr_ui_dim_special_name says which) */
    UI_DIM_PLAYER_SHOTS,     /* ... the player's own shots */
    UI_DIM_AVAILABLE,        /* read-only: this game's draw order is known to the patch */
    UI_SHARPEN,              /* index into the post-process registry; -1 for none */
    UI_SHARPEN_STRENGTH,     /* 0..100 percent */
    UI_CURSOR,               /* the mouse pointer in borderless fullscreen: 0 as the game does (hidden), 1 visible, 2 visible while moving */
    UI_FIXED_LOGIC,           /* read-only: fixed 60 Hz simulation with render interpolation */
    UI_VIDEO_AVAILABLE,       /* read-only: scaler and window controls have a backend */
    UI_SOFTWARE_CURSOR,       /* read-only: game hides the OS cursor; draw one in the menu */
    UI_SETTING_COUNT
};

/* Implemented by the runtime (C). */
int         hfr_ui_get(int id);
void        hfr_ui_set(int id, int value);
int         hfr_ui_filter_count(void);
const char* hfr_ui_filter_name(int index);
int         hfr_ui_filter_is_fixed_scale(int index);
int         hfr_ui_post_count(void);          /* post-processes (sharpening) available after the filter */
const char* hfr_ui_post_name(int index);
void        hfr_ui_save(void);
void        hfr_ui_status(char* buf, int len);
void        hfr_ui_scale_info(char* buf, int len);
int         hfr_ui_menu_key(void);
const char* hfr_ui_dim_special_name(void);   /* what UI_DIM_SPECIAL fades in this game; NULL when it has nothing */
/* Settings that only take effect on the next run, so the menu can say so rather than
   pretending a change did something. */
/* True while the simulation must not be reconfigured: a stage is running, or a replay is
   playing. Changing the tick rate or what is sub-stepped mid-stage would desynchronise the
   replay being recorded. */
int         hfr_ui_simulation_locked(void);
int         hfr_ui_simulation_patched(void);  /* 0 when this game's engine is not described yet */
int         hfr_ui_system_count(void);
const char* hfr_ui_system_name(int index);
int         hfr_ui_system_get(int index);
void        hfr_ui_system_set(int index, int value);
const char* hfr_ui_present_path(void);
void        hfr_ui_report(const char* fmt, ...);   /* into the patch's log */
void        hfr_menu_requested(void);              /* toggle the menu once, on the next frame */
void        hfr_menu_key_down(int down);           /* the menu key's state per the window's messages */

/* Implemented by the menu (C++); all are safe to call when the menu failed to start.
   A build without the menu (the test harness) defines HFR_NO_UI and gets local no-ops. */
#ifndef HFR_NO_UI
int  hfr_menu_init(void* dev, HWND hwnd);
void hfr_menu_shutdown(void);
void hfr_menu_invalidate(void);
void hfr_menu_render(void* dev, int width, int height);
int  hfr_menu_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);
void hfr_menu_toggle(void);
int  hfr_menu_visible(void);
#else
static int  hfr_menu_init(void* dev, HWND hwnd) { (void)dev; (void)hwnd; return 0; }
static void hfr_menu_shutdown(void) {}
static void hfr_menu_invalidate(void) {}
static void hfr_menu_render(void* dev, int width, int height) { (void)dev; (void)width; (void)height; }
static int  hfr_menu_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result)
            { (void)hwnd; (void)msg; (void)wp; (void)lp; (void)result; return 0; }
static void hfr_menu_toggle(void) {}
static int  hfr_menu_visible(void) { return 0; }
#endif

#ifdef __cplusplus
}
#endif
