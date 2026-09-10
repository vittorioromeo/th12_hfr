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
