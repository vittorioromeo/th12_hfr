#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <ctype.h>
#include <d3d9.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ logging */
static FILE* g_log;
static int g_log_lazy;   /* debug>=3: flushed once per frame instead of per line (the per-frame draw traces are big) */
static int g_frame_seen; /* the game has drawn at least one frame: everything that loads has loaded */
static void logf_(const char* fmt, ...) {
    if (!g_log) return;
    /* Direct3D 9 leaves the game's thread in 24-bit x87 precision (see DEVNOTES_RUNTIME §3b,
       "the clock that lied"); our arithmetic is SSE, but the C runtime's number formatting is
       not, so format at full precision and put the game's setting back. */
    unsigned cw = _controlfp(0, 0); _controlfp(_PC_53, _MCW_PC);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    _controlfp(cw, _MCW_PC);
    fputc('\n', g_log); if (!g_log_lazy) fflush(g_log);
}
#define LOG(...) logf_(__VA_ARGS__)

/* Built-in filters occupy the first indices; shader filters are appended after them. */
enum { FILTER_NEAREST = 0, FILTER_BILINEAR = 1, FILTER_SHARP = 2, FILTER_BUILTIN_COUNT };

#include "dim_classes.h"

/* ------------------------------------------------------------------ config */
static struct {
    int fps;            /* 0 = auto from display */
    int vsync;          /* present interval one */
    int substep;        /* 1 = sub-step gameplay, 0 = stock logic at 60 Hz + high rate presentation */
    int log;
    int fullscreen_refresh; /* refresh rate to request in fullscreen (0 = auto = same as fps) */
    int show_stats;
    int warn_wrapper;   /* say so when something else is presenting and settings are inert */
    int enemy_interp;
    int debug;
    int subtick_input;      /* poll the keyboard/joystick every tick and feed movement/focus to the player */
    int d3d9ex;             /* create the device through Direct3D9Ex */
    int max_frame_latency;  /* IDirect3DDevice9Ex::SetMaximumFrameLatency (0 = leave default) */
    int flipex;             /* windowed: D3DSWAPEFFECT_FLIPEX (experimental) */
    /* output scaling */
    int scaling;            /* 0 stretch, 1 aspect fit, 2 integer (pixel perfect) */
    int filter;             /* index into the filter registry */
    char filter_name[32];   /* how it was written in the INI, so it survives folder changes */
    char sharpen_name[32];  /* post-process run over the finished image; "none" for off */
    int sharpen;            /* index into the post-process registry, or -1 */
    int sharpen_strength;   /* 0..100 percent, the post-process's Params.x */
    int cursor;             /* the pointer in borderless fullscreen: 0 hidden as the game does, 1 visible, 2 visible while moving */
    int resizable;          /* add a resize border to the game's window */
    int window_scale;       /* client size at startup, in percent of the game's own (200 = 1280x960);
                               0 leaves the window as the game made it, -1 takes the largest
                               whole multiple that fits the monitor */
    int snap_aspect;        /* keep the window itself at the native aspect while dragging */
    int fullscreen_mode;    /* 0 leave the game's exclusive fullscreen, 1 borderless desktop */
    int menu_key;           /* virtual-key code that opens the in-game menu */
    int size_cycle_key;     /* cycles the window size, for games without their own F10 (0 = off) */
    int own_present;        /* -1 auto, 0 present through the game's chain, 1 through ours */
    int internal_scale;     /* the game draws at N times 640x480 (1 = as shipped) */
    int texture_scale;      /* textures magnified N times at load with texture_filter (0/1 = off) */
    char texture_filter_name[32];
    int dim[5];             /* DIM_*: percent each class of drawing is faded (the background towards black,
                               everything else towards transparent); 0 = off */
/* Named, not positional: the old form was a bare list of eighteen numbers that had to stay in
   the same order as the fields above, so inserting a setting anywhere but the end silently
   shifted every default after it. Anything omitted here is zero. */
} cfg = {
    .fps = 0, .vsync = 1, .substep = 1, .log = 1,
    .fullscreen_refresh = 0, .show_stats = 0, .warn_wrapper = 1,
    .enemy_interp = 1, .debug = 0, .subtick_input = 1, .d3d9ex = 1,
    .max_frame_latency = 1, .flipex = 0,
    .scaling = 1, .filter = FILTER_SHARP, .filter_name = "", .sharpen_name = "none", .sharpen = -1, .sharpen_strength = 50, .cursor = 2,
    .resizable = 1, .window_scale = 0, .snap_aspect = 0, .fullscreen_mode = 1,
    .menu_key = VK_F11, .size_cycle_key = VK_F10, .own_present = -1, .internal_scale = 1, .texture_scale = 0, .texture_filter_name = "xbr-lv2",
};
