/* The narrow interface between the C runtime and the C++ menu. The menu never sees the
   runtime's globals; it reads and writes settings through these, so the two sides can be
   compiled separately and the menu can be left out of a build entirely. */
#pragma once
#include <windows.h>
#include <d3d9.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    UI_SCALING = 0,          /* SCALE_STRETCH / SCALE_ASPECT / SCALE_INTEGER */
    UI_FILTER,               /* index into the filter registry */
    UI_RESIZABLE,
    UI_SNAP_ASPECT,
    UI_FULLSCREEN_MODE,      /* 0 keep the game's exclusive mode, 1 borderless */
    UI_VSYNC,
    UI_MAX_FRAME_LATENCY,
    UI_SETTING_COUNT
};

/* Implemented by the runtime (C). */
int         hfr_ui_get(int id);
void        hfr_ui_set(int id, int value);
int         hfr_ui_filter_count(void);
const char* hfr_ui_filter_name(int index);
int         hfr_ui_filter_is_fixed_scale(int index);
void        hfr_ui_save(void);
void        hfr_ui_status(char* buf, int len);
void        hfr_ui_scale_info(char* buf, int len);
int         hfr_ui_menu_key(void);
void        hfr_ui_report(const char* fmt, ...);   /* into the patch's log */
void        hfr_menu_requested(void);              /* the key arrived; act on it once per frame */

/* Implemented by the menu (C++); all are safe to call when the menu failed to start.
   A build without the menu (the test harness) defines HFR_NO_UI and gets local no-ops. */
#ifndef HFR_NO_UI
int  hfr_menu_init(IDirect3DDevice9* dev, HWND hwnd);
void hfr_menu_shutdown(void);
void hfr_menu_invalidate(void);
void hfr_menu_render(IDirect3DDevice9* dev, int width, int height);
int  hfr_menu_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);
void hfr_menu_toggle(void);
int  hfr_menu_visible(void);
#else
static int  hfr_menu_init(IDirect3DDevice9* dev, HWND hwnd) { (void)dev; (void)hwnd; return 0; }
static void hfr_menu_shutdown(void) {}
static void hfr_menu_invalidate(void) {}
static void hfr_menu_render(IDirect3DDevice9* dev, int width, int height) { (void)dev; (void)width; (void)height; }
static int  hfr_menu_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result)
            { (void)hwnd; (void)msg; (void)wp; (void)lp; (void)result; return 0; }
static void hfr_menu_toggle(void) {}
static int  hfr_menu_visible(void) { return 0; }
#endif

#ifdef __cplusplus
}
#endif
