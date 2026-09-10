#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ logging */
static FILE* g_log;
static void logf_(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}
#define LOG(...) logf_(__VA_ARGS__)

/* Built-in filters occupy the first indices; shader filters are appended after them. */
enum { FILTER_NEAREST = 0, FILTER_BILINEAR = 1, FILTER_SHARP = 2, FILTER_BUILTIN_COUNT };

/* ------------------------------------------------------------------ config */
static struct {
    int fps;            /* 0 = auto from display */
    int vsync;          /* present interval one */
    int substep;        /* 1 = sub-step gameplay, 0 = stock logic at 60 Hz + high rate presentation */
    int log;
    int fullscreen_refresh; /* refresh rate to request in fullscreen (0 = auto = same as fps) */
    int show_stats;
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
    int resizable;          /* add a resize border to the game's window */
    int snap_aspect;        /* keep the window itself at the native aspect while dragging */
    int fullscreen_mode;    /* 0 leave the game's exclusive fullscreen, 1 borderless desktop */
} cfg = { 0, 1, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0,
          1, FILTER_SHARP, "", 1, 1, 1 };
