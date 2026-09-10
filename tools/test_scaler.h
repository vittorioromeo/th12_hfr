/* Output-scaling geometry. Pure integer maths, so it is checked exhaustively here rather
   than by looking at the screen. */
static void test_scale_rect(void) {
    const int W = 640, H = 480;
    /* stretch always fills */
    for (int dw = 1; dw <= 4000; dw += 37) for (int dh = 1; dh <= 3000; dh += 41) {
        struct ScaleRect r = scale_rect(W, H, dw, dh, SCALE_STRETCH);
        assert(r.x == 0 && r.y == 0 && r.w == dw && r.h == dh);
    }
    /* aspect fit: never overflows, stays centred, and keeps 4:3 within a pixel */
    for (int dw = 16; dw <= 4000; dw += 13) for (int dh = 16; dh <= 3000; dh += 17) {
        struct ScaleRect r = scale_rect(W, H, dw, dh, SCALE_ASPECT);
        assert(r.w >= 1 && r.h >= 1 && r.w <= dw && r.h <= dh);
        assert(r.x >= 0 && r.y >= 0 && r.x + r.w <= dw && r.y + r.h <= dh);
        assert(r.x - (dw - r.w - r.x) <= 1 && (dw - r.w - r.x) - r.x <= 1);
        assert(r.y - (dh - r.h - r.y) <= 1 && (dh - r.h - r.y) - r.y <= 1);
        assert(r.w == dw || r.h == dh);                       /* touches at least one edge */
        /* the free dimension is the ideal one rounded to the nearest pixel */
        if (r.w == dw) { long long ideal = (long long)dw * H; assert(llabs((long long)r.h * W - ideal) * 2 <= W); }
        else           { long long ideal = (long long)dh * W; assert(llabs((long long)r.w * H - ideal) * 2 <= H); }
        if (dw >= 128 && dh >= 128) {
            double want = (double)W / H, got = (double)r.w / r.h;
            assert(got > want - 0.02 && got < want + 0.02);
        }
    }
    /* integer scaling: exact multiples, centred, never larger than the target */
    for (int n = 1; n <= 6; ++n) for (int slack_w = 0; slack_w < W; slack_w += 91) for (int slack_h = 0; slack_h < H; slack_h += 79) {
        int dw = W * n + slack_w, dh = H * n + slack_h;
        struct ScaleRect r = scale_rect(W, H, dw, dh, SCALE_INTEGER);
        assert(r.w == W * n && r.h == H * n);
        assert(r.x == (dw - r.w) / 2 && r.y == (dh - r.h) / 2);
        assert(r.x + r.w <= dw && r.y + r.h <= dh);
    }
    /* below 1:1 the integer mode has nothing to snap to and falls back to fitting */
    struct ScaleRect small = scale_rect(W, H, 320, 240, SCALE_INTEGER);
    assert(small.w == 320 && small.h == 240);
    small = scale_rect(W, H, 320, 480, SCALE_INTEGER);
    assert(small.w == 320 && small.h == 240 && small.y == 120);
    /* exact multiples are identical in every mode that preserves aspect */
    for (int n = 1; n <= 8; ++n) {
        struct ScaleRect a = scale_rect(W, H, W * n, H * n, SCALE_ASPECT);
        struct ScaleRect i = scale_rect(W, H, W * n, H * n, SCALE_INTEGER);
        assert(a.x == 0 && a.y == 0 && a.w == W * n && a.h == H * n);
        assert(i.x == 0 && i.y == 0 && i.w == W * n && i.h == H * n);
    }
    /* common real displays, checked by hand */
    struct ScaleRect r;
    r = scale_rect(W, H, 1920, 1080, SCALE_ASPECT);  assert(r.w == 1440 && r.h == 1080 && r.x == 240 && r.y == 0);
    r = scale_rect(W, H, 1920, 1080, SCALE_INTEGER); assert(r.w == 1280 && r.h == 960  && r.x == 320 && r.y == 60);
    r = scale_rect(W, H, 2560, 1440, SCALE_INTEGER); assert(r.w == 1920 && r.h == 1440 && r.x == 320 && r.y == 0);
    r = scale_rect(W, H, 3840, 2160, SCALE_INTEGER); assert(r.w == 2560 && r.h == 1920 && r.x == 640 && r.y == 120);
    r = scale_rect(W, H, 1280, 1024, SCALE_ASPECT);  assert(r.w == 1280 && r.h == 960);
    /* degenerate inputs must not divide by zero or return something unusable */
    r = scale_rect(0, 0, 100, 100, SCALE_ASPECT); assert(r.w == 100 && r.h == 100);
    r = scale_rect(W, H, 0, 0, SCALE_INTEGER);    assert(r.w == 0 && r.h == 0);
    /* sharp-bilinear prepass factor covers the destination and stays bounded */
    for (int dw = 1; dw <= 6000; dw += 7) {
        int f = sharp_factor(W, H, dw, dw * 3 / 4);
        assert(f >= 1 && f <= 8);
        assert(f == 8 || W * f >= dw);
        if (f > 1) assert(W * (f - 1) < dw || H * (f - 1) < dw * 3 / 4);
    }
    assert(sharp_factor(W, H, 640, 480) == 1);
    assert(sharp_factor(W, H, 641, 480) == 2);
    assert(sharp_factor(W, H, 1920, 1080) == 3);
    assert(sharp_factor(W, H, 1280, 960) == 2);
    puts("PASS: output scaling geometry (stretch, aspect fit, integer) and sharp-bilinear prepass factors");
}

/* Window-resize aspect snapping: the edge being dragged is the one that survives. */
static void test_snap_client(void) {
    const int W = 640, H = 480;
    int cw, ch;
    cw = 1000; ch = 137; snap_client(WMSZ_RIGHT, W, H, &cw, &ch);  assert(cw == 1000 && ch == 750);
    cw = 1000; ch = 137; snap_client(WMSZ_LEFT, W, H, &cw, &ch);   assert(cw == 1000 && ch == 750);
    cw = 137;  ch = 900; snap_client(WMSZ_BOTTOM, W, H, &cw, &ch); assert(ch == 900 && cw == 1200);
    cw = 137;  ch = 900; snap_client(WMSZ_TOP, W, H, &cw, &ch);    assert(ch == 900 && cw == 1200);
    /* a corner follows whichever dimension was dragged further from the ratio */
    cw = 1600; ch = 500;  snap_client(WMSZ_BOTTOMRIGHT, W, H, &cw, &ch); assert(cw == 1600 && ch == 1200);
    cw = 500;  ch = 1200; snap_client(WMSZ_BOTTOMRIGHT, W, H, &cw, &ch); assert(ch == 1200 && cw == 1600);
    /* already correct stays put, in every direction */
    for (int n = 1; n <= 6; ++n) for (int e = WMSZ_LEFT; e <= WMSZ_BOTTOMRIGHT; ++e) {
        cw = W * n; ch = H * n; snap_client(e, W, H, &cw, &ch);
        assert(cw == W * n && ch == H * n);
    }
    /* never returns a degenerate size, whatever it is handed */
    for (int e = WMSZ_LEFT; e <= WMSZ_BOTTOMRIGHT; ++e) {
        cw = 0; ch = 0; snap_client(e, W, H, &cw, &ch); assert(cw >= 1 && ch >= 1);
        cw = 1; ch = 4000; snap_client(e, W, H, &cw, &ch); assert(cw >= 1 && ch >= 1);
    }
    cw = 100; ch = 100; snap_client(WMSZ_RIGHT, 0, 0, &cw, &ch); assert(cw == 100 && ch == 100);
    puts("PASS: window aspect snapping follows the dragged edge and never degenerates");
}

/* The menu key. This exists because of a real bug: the window's key messages and the
   runtime's own poll each announced a press, the two announcements did not always land in
   the same frame, and when they did not the two toggles cancelled -- the key looked dead.
   Both routes now report a level and only menu_key_press turns that into a press. */
static void test_menu_key(void) {
    struct menu_key k;
    memset(&k, 0, sizeof k);

    /* Both routes see the same press in the same poll: one press, not two. */
    hfr_menu_key_down(1); k = g_menu_key;
    assert(menu_key_press(&k, 1, 1) == 1);
    assert(menu_key_press(&k, 1, 1) == 0);            /* still held */
    k.msg_down = 0;
    assert(menu_key_press(&k, 0, 1) == 0);            /* released */

    /* The message arrives a poll after the hardware edge: still one press. This is the case
       that used to produce a tight open/close pair and no visible change on screen. */
    assert(menu_key_press(&k, 1, 1) == 1);            /* the poll sees it first */
    k.msg_down = 1; k.msg_tapped = 1;
    assert(menu_key_press(&k, 1, 1) == 0);            /* the message catches up */
    k.msg_down = 0;
    assert(menu_key_press(&k, 0, 1) == 0);

    /* ...and the other way round, when DirectInput has taken the keyboard and the poll lags. */
    k.msg_down = 1; k.msg_tapped = 1;
    assert(menu_key_press(&k, 0, 1) == 1);
    assert(menu_key_press(&k, 1, 1) == 0);
    k.msg_down = 0;
    assert(menu_key_press(&k, 0, 1) == 0);

    /* Every subsequent press still registers: the level really does fall again. */
    for (int i = 0; i < 50; ++i) {
        k.msg_down = 1; k.msg_tapped = 1;
        assert(menu_key_press(&k, 1, 1) == 1);
        k.msg_down = 0;
        assert(menu_key_press(&k, 0, 1) == 0);
    }

    /* A press that began and ended entirely between two polls -- a long frame, or a very
       short tap -- is still a press, and still only one. */
    memset(&k, 0, sizeof k);
    k.msg_tapped = 1;                                  /* KEYDOWN and KEYUP both already seen */
    assert(menu_key_press(&k, 0, 1) == 1);
    assert(menu_key_press(&k, 0, 1) == 0);             /* and not again on the next poll */

    /* Held with no focus: no press, and nothing left behind that eats the next one. */
    memset(&k, 0, sizeof k);
    assert(menu_key_press(&k, 1, 0) == 0);
    assert(menu_key_press(&k, 1, 0) == 0);
    k.msg_down = 1; k.msg_tapped = 1;
    assert(menu_key_press(&k, 1, 1) == 1);
    k.msg_down = 0;
    assert(menu_key_press(&k, 0, 1) == 0);

    /* A KEYUP lost because the window lost focus while the key was held must not wedge the
       key down forever. Focus loss clears it outright... */
    memset(&k, 0, sizeof k);
    k.msg_down = 1; k.msg_tapped = 1;
    assert(menu_key_press(&k, 1, 1) == 1);
    assert(menu_key_press(&k, 1, 0) == 0);             /* focus lost, key still held */
    assert(k.msg_down == 0 && k.msg_tapped == 0);
    assert(menu_key_press(&k, 0, 1) == 0);             /* focus back, key up */
    k.msg_down = 1; k.msg_tapped = 1;
    assert(menu_key_press(&k, 1, 1) == 1);             /* the next press lands */

    /* ...and a message route that somehow keeps claiming "down" while the hardware says up
       is eventually given up on rather than believed forever. */
    memset(&k, 0, sizeof k);
    k.msg_down = 1; k.msg_tapped = 1;
    assert(menu_key_press(&k, 1, 1) == 1);
    for (int i = 0; i < 200 && k.msg_down; ++i) menu_key_press(&k, 0, 1);
    assert(k.msg_down == 0 && k.level == 0);
    assert(menu_key_press(&k, 1, 1) == 1);             /* and presses land again */

    printf("PASS: menu key presses survive both routes reporting, a tap between polls, "
           "focus loss and a lost key-up\n");
}
