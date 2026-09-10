/* ------------------------------------------------------------------ window management
 * The game creates a fixed-size window and offers three preset sizes plus an exclusive
 * fullscreen mode that switches the display to 640x480. We add a resize border, let the
 * window be any size (the scaler letterboxes), and turn "fullscreen" into a borderless
 * window at the desktop resolution instead of a mode switch.
 *
 * The game keeps its own idea of the mode in its window-flags global and rewrites the
 * window whenever it resets the device, so rather than fighting it message by message we
 * re-assert the geometry we want once per frame; anything the game does is undone on the
 * next frame at the latest.
 */
enum { FS_GAME = 0, FS_BORDERLESS = 1 };

/* Snap a proposed client size to the native aspect ratio, following whichever edge the
   user is dragging. Pure; unit tested. */
static void snap_client(int edge, int nw, int nh, int* cw, int* ch) {
    if (nw <= 0 || nh <= 0) return;
    int w = *cw, h = *ch;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    switch (edge) {
    case WMSZ_LEFT: case WMSZ_RIGHT:
        h = (int)(((long long)w * nh + nw / 2) / nw); break;
    case WMSZ_TOP: case WMSZ_BOTTOM:
        w = (int)(((long long)h * nw + nh / 2) / nh); break;
    default: {   /* a corner: keep whichever dimension grew more, derive the other */
        long long by_w = (long long)w * nh, by_h = (long long)h * nw;
        if (by_w > by_h) h = (int)(((long long)w * nh + nw / 2) / nw);
        else             w = (int)(((long long)h * nw + nh / 2) / nh);
        break; }
    }
    *cw = w < 1 ? 1 : w;
    *ch = h < 1 ? 1 : h;
}

#ifndef HFR_SCALER_TEST_ONLY

static WNDPROC g_orig_wndproc;
static HWND    g_wnd;
static int     g_resize_pending;      /* client size changed; the swap chain must follow */
static int     g_in_sizemove;         /* inside a drag, defer the reset until it ends */
static int     g_minimized;
static int     g_win_ready;
static int     g_borderless_active;

static void monitor_rect(HWND h, RECT* r) {
    HMONITOR m = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi; mi.cbSize = sizeof mi;
    if (m && GetMonitorInfoA(m, &mi)) { *r = mi.rcMonitor; return; }
    r->left = 0; r->top = 0; r->right = GetSystemMetrics(SM_CXSCREEN); r->bottom = GetSystemMetrics(SM_CYSCREEN);
}
/* Resize the window so its client area becomes exactly cw x ch under the given style. */
static void set_client_size(HWND h, LONG style, int cw, int ch) {
    RECT r = { 0, 0, cw, ch };
    AdjustWindowRect(&r, (DWORD)style, FALSE);
    RECT cur; GetWindowRect(h, &cur);
    SetWindowPos(h, NULL, cur.left, cur.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

/* The game asks for an exclusive 640x480 mode when it goes fullscreen, which on a modern
   display means a mode switch and the monitor's own upscaler. Give it a borderless window
   covering the monitor instead; the scaler letterboxes into it. */
static int window_override_pp(D3DPRESENT_PARAMETERS* out, HWND hwnd) {
    if (cfg.fullscreen_mode != FS_BORDERLESS || out->Windowed) return 0;
    RECT m; monitor_rect(hwnd ? hwnd : g_wnd, &m);
    if (m.right - m.left < 1 || m.bottom - m.top < 1) return 0;
    out->Windowed = TRUE;
    out->FullScreen_RefreshRateInHz = 0;
    out->BackBufferWidth = (UINT)(m.right - m.left);
    out->BackBufferHeight = (UINT)(m.bottom - m.top);
    return 1;
}

static LRESULT CALLBACK hfr_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT handled = 0;
    if (hfr_menu_wndproc(h, msg, wp, lp, &handled)) return handled;
    switch (msg) {
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) { g_minimized = 1; break; }
        g_minimized = 0;
        if (!g_in_sizemove) g_resize_pending = 1;
        /* The game turns a maximise into exclusive fullscreen; we maximise for real and
           letterbox instead, so that message must not reach it. */
        if (wp == SIZE_MAXIMIZED) return 0;
        break;
    case WM_ENTERSIZEMOVE: g_in_sizemove = 1; return 0;
    case WM_EXITSIZEMOVE:  g_in_sizemove = 0; g_resize_pending = 1; return 0;
    case WM_SIZING:
        if (cfg.snap_aspect && !g_borderless_active && g_native_w > 0) {
            RECT* pr = (RECT*)lp;
            RECT c; GetClientRect(h, &c);
            RECT w; GetWindowRect(h, &w);
            int padx = (w.right - w.left) - (c.right - c.left);
            int pady = (w.bottom - w.top) - (c.bottom - c.top);
            int cw = (pr->right - pr->left) - padx, ch = (pr->bottom - pr->top) - pady;
            snap_client((int)wp, g_native_w, g_native_h, &cw, &ch);
            int ww = cw + padx, wh = ch + pady;
            if (wp == WMSZ_LEFT || wp == WMSZ_TOPLEFT || wp == WMSZ_BOTTOMLEFT) pr->left = pr->right - ww;
            else pr->right = pr->left + ww;
            if (wp == WMSZ_TOP || wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT) pr->top = pr->bottom - wh;
            else pr->bottom = pr->top + wh;
            return TRUE;
        }
        break;
    case WM_SETCURSOR:
        /* The game forces the arrow cursor and claims every WM_SETCURSOR while windowed,
           which stops the frame from ever showing a resize cursor. Let the default
           handler have the non-client area back. */
        if (LOWORD(lp) != HTCLIENT && !g_borderless_active)
            return DefWindowProcA(h, msg, wp, lp);
        break;
    case WM_GETMINMAXINFO:
        if (g_native_w > 0) {
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            RECT r = { 0, 0, g_native_w / 4, g_native_h / 4 };
            AdjustWindowRect(&r, (DWORD)GetWindowLongA(h, GWL_STYLE), FALSE);
            mm->ptMinTrackSize.x = r.right - r.left;
            mm->ptMinTrackSize.y = r.bottom - r.top;
            return 0;
        }
        break;
    default: break;
    }
    return g_orig_wndproc ? CallWindowProcA(g_orig_wndproc, h, msg, wp, lp) : DefWindowProcA(h, msg, wp, lp);
}

static void window_attach(HWND h) {
    if (!h || g_wnd == h) return;
    g_wnd = h;
    g_orig_wndproc = (WNDPROC)(LONG_PTR)SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)hfr_wndproc);
    if (!g_orig_wndproc) { LOG("window: cannot subclass %p; resizing disabled", (void*)h); g_wnd = NULL; return; }
    g_win_ready = 1;
    LOG("window: subclassed %p (resizable=%d borderless=%d)", (void*)h, cfg.resizable, cfg.fullscreen_mode);
}
static int g_window_scaled;           /* the startup size has been applied */
/* Size the client area to a percentage of the game's own, -1 meaning the largest whole
   multiple that fits the monitor's work area. Anything that would overflow the work area is
   held at the largest size of the same aspect that fits, and the window is centred, so a 2x
   or 3x request on a small screen still lands somewhere sensible. Takes effect through the
   ordinary resize path: WM_SIZE arrives, the presentation chain follows. */
static void window_apply_scale(int percent) {
    if (!g_wnd || g_native_w <= 0 || g_native_h <= 0) return;
    HMONITOR m = MonitorFromWindow(g_wnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi; mi.cbSize = sizeof mi;
    RECT work;
    if (m && GetMonitorInfoA(m, &mi)) work = mi.rcWork;
    else { work.left = 0; work.top = 0; work.right = GetSystemMetrics(SM_CXSCREEN); work.bottom = GetSystemMetrics(SM_CYSCREEN); }
    LONG style = GetWindowLongA(g_wnd, GWL_STYLE);
    RECT frame = { 0, 0, 0, 0 };
    AdjustWindowRect(&frame, (DWORD)style, FALSE);
    int padx = frame.right - frame.left, pady = frame.bottom - frame.top;
    int maxw = (work.right - work.left) - padx, maxh = (work.bottom - work.top) - pady;
    int cw, ch;
    if (percent < 0) {
        int k = maxw / g_native_w; if (maxh / g_native_h < k) k = maxh / g_native_h;
        if (k < 1) k = 1;
        cw = g_native_w * k; ch = g_native_h * k;
    } else {
        cw = (int)((long long)g_native_w * percent / 100); ch = (int)((long long)g_native_h * percent / 100);
    }
    if (cw < g_native_w / 4) cw = g_native_w / 4;
    if (ch < g_native_h / 4) ch = g_native_h / 4;
    if (cw > maxw || ch > maxh) {   /* keep the aspect, fit the work area */
        double s = (double)maxw / cw; if ((double)maxh / ch < s) s = (double)maxh / ch;
        cw = (int)(cw * s); ch = (int)(ch * s);
    }
    int ww = cw + padx, wh = ch + pady;
    int x = work.left + ((work.right - work.left) - ww) / 2;
    int y = work.top + ((work.bottom - work.top) - wh) / 2;
    SetWindowPos(g_wnd, NULL, x, y, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    g_resize_pending = 1;
    LOG("window: client sized to %dx%d (%s)", cw, ch,
        percent < 0 ? "largest whole multiple that fits" : "window_scale");
}
/* Re-assert the style and, in borderless, the geometry. Runs once per frame; a no-op
   unless something (usually the game's own reset path) has changed them. */
static void window_enforce(void) {
    if (!g_win_ready || !g_wnd || g_minimized) return;
    LONG style = GetWindowLongA(g_wnd, GWL_STYLE);
    int game_fullscreen = !(style & WS_CAPTION) && (style & WS_POPUP);
    int want_borderless = cfg.fullscreen_mode == FS_BORDERLESS && game_fullscreen;
    if (want_borderless) {
        RECT m; monitor_rect(g_wnd, &m);
        RECT cur; GetWindowRect(g_wnd, &cur);
        if (!g_borderless_active) {
            g_borderless_active = 1;
            LOG("window: borderless fullscreen %ldx%ld", m.right - m.left, m.bottom - m.top);
        }
        if (memcmp(&cur, &m, sizeof m)) {
            SetWindowPos(g_wnd, NULL, m.left, m.top, m.right - m.left, m.bottom - m.top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            g_resize_pending = 1;
        }
        return;
    }
    if (g_borderless_active) { g_borderless_active = 0; g_resize_pending = 1; }
    if (cfg.resizable && !game_fullscreen) {
        LONG want = style | WS_THICKFRAME | WS_MAXIMIZEBOX;
        if (want != style) {
            int cw, ch; client_size(g_wnd, &cw, &ch);
            SetWindowLongA(g_wnd, GWL_STYLE, want);
            set_client_size(g_wnd, want, cw, ch);   /* keep the client area the game asked for */
            LOG("window: resize border added, client %dx%d", cw, ch);
        }
    }
    /* The startup size, once. TH10's own dialog only offers a 640x480 window, and at 640x480
       every scaling mode and every filter produce the same 1:1 picture -- which reads as
       "none of it works". Later games offer larger windows in their dialogs, so their users
       never met this; here the patch can size the window itself. */
    if (!g_window_scaled && !game_fullscreen && g_native_w > 0) {
        g_window_scaled = 1;
        if (cfg.window_scale) window_apply_scale(cfg.window_scale);
    }
}
/* Opening the menu must not depend on how the game took the keyboard: it reads through
   DirectInput or GetKeyboardState rather than the message queue, and DirectInput can stop key
   messages reaching the window at all. So the key is watched two ways -- the window's own key
   messages, and GetAsyncKeyState -- and both report the same thing: whether the key is down.
   Only menu_key_press turns that into a press.

   It used to be the other way round, with each route announcing a press of its own. When both
   saw the same press they announced it twice, and because the two announcements did not always
   land in the same frame the two toggles often cancelled: the menu stopped responding to the
   key for as long as that timing held, which is what "F11 does nothing any more" was. Levels
   cannot disagree about how many presses happened, so that failure is now impossible by
   construction rather than by careful bookkeeping. */
struct menu_key {
    int level;      /* the key's state as of the last poll */
    int msg_down;   /* down according to the window's key messages */
    int msg_tapped; /* a whole press arrived by message since the last poll */
    int stale;      /* consecutive polls where msg_down outlived the hardware */
};

/* A message-route "down" that outlives the hardware for this many consecutive polls is a
   KEYUP that never arrived, not a key being held: a real press spans far more polls than
   this at any tick rate. The point is that the level can always fall again -- a level stuck
   high would look exactly like the key having stopped working. */
#define MENU_KEY_STALE_POLLS 30

int menu_key_press(struct menu_key* k, int polled_down, int focus) {
    if (!focus)                              { k->msg_down = 0; k->msg_tapped = 0; k->stale = 0; }
    else if (k->msg_down && !polled_down)    { if (++k->stale >= MENU_KEY_STALE_POLLS) { k->msg_down = 0; k->stale = 0; } }
    else                                     { k->stale = 0; }

    int down  = focus && (k->msg_down || polled_down);
    int press = down && !k->level;
    /* A press that both began and ended between two polls never shows up as a level at all.
       That is not a human tapping the key, but a long frame -- the log calls them gaps -- can
       stretch the window between polls far enough for a normal press to fall inside it.
       Only when the key is down neither now nor before: otherwise the message is describing
       the press this poll has already counted from the level rising. */
    if (!down && !k->level && k->msg_tapped && focus) press = 1;
    k->msg_tapped = 0;
    k->level = down;
    return press;
}

static int g_menu_request;          /* set by anything that wants the menu toggled once */
static struct menu_key g_menu_key;

void hfr_menu_requested(void) { g_menu_request = 1; }
void hfr_menu_key_down(int down) {
    if (down) g_menu_key.msg_tapped = 1;
    g_menu_key.msg_down = down ? 1 : 0;
}

static int app_has_focus(void) {
    HWND fg = GetForegroundWindow();
    if (!fg) return 0;
    if (fg == g_wnd) return 1;
    DWORD pid = 0;                     /* a wrapper may own a different top-level window */
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}
static void poll_menu_key(void) {
    static int told;
    int focus = app_has_focus();
    int held  = cfg.menu_key && (GetAsyncKeyState(cfg.menu_key) & 0x8000) != 0;

    if (menu_key_press(&g_menu_key, held, focus)) g_menu_request = 1;
    if (held && !focus && !told) { told = 1; LOG("menu: key seen but the game does not have focus"); }

    if (g_menu_request) {
        g_menu_request = 0;
        hfr_menu_toggle();
        LOG("menu: %s", hfr_menu_visible() ? "opened" : "closed");
    }
}
/* Called from the frame hook. Returns non-zero when the frame should be skipped. */
static int window_pump(IDirect3DDevice9* dev) {
    if (!g_win_ready) return 0;
    poll_menu_key();
    window_enforce();
    if (g_minimized) return 1;
    if (!g_resize_pending || g_in_sizemove || !dev || !g_scaler_ok) return 0;
    int cw, ch; client_size(g_wnd, &cw, &ch);
    if (cw < 1 || ch < 1) return 1;
    if (cw == g_out_w && ch == g_out_h) { g_resize_pending = 0; return 0; }
    g_resize_pending = 0;
    LOG("window: client %dx%d, rebuilding the presentation chain from %dx%d", cw, ch, g_out_w, g_out_h);
    if (g_own_present) return !scaler_set_output(dev, g_wnd, cw, ch);
    D3DPRESENT_PARAMETERS pp = *G_PP;
    HRESULT hr = dev->lpVtbl->Reset(dev, &pp);
    if (FAILED(hr)) LOG("window: reset for resize failed (0x%08lx)", (long)hr);
    return FAILED(hr);
}
#endif /* HFR_SCALER_TEST_ONLY */
