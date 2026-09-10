/* Places in TH10 v1.00a that another patch is known to take over, with the bytes a clean
   executable has there -- byte-identical in the Japanese and English builds. Found the same
   way as TH11's: every 32-bit immediate in vpatch_th10.dll that lands in the game's code, kept
   when the code there has the right shape. The two calls both target 0x439540, matching the
   pattern where the frame limiter and the replay timing call one timing routine, as they do in
   TH11 (0x446920) and TH12 (0x4508b0). Three sites load a device pointer for a Present call;
   which of them vpatch takes is not certain, so all three are listed -- a site that vpatch
   leaves alone simply never trips, and the check for a loaded vpatch module catches it anyway. */
static const struct ConflictSite th10_conflicts[] = {
    {0x439397, 5, {0xe8,0xa4,0x01,0x00,0x00},           "the frame limiter"},
    {0x4134b8, 5, {0xe8,0x83,0x60,0x02,0x00},           "the replay timing routine"},
    {0x4399e9, 7, {0xa1,0x2c,0x1c,0x49,0x00,0x8b,0x08}, "a Present call"},
    {0x439a27, 7, {0xa1,0x2c,0x1c,0x49,0x00,0x8b,0x08}, "a Present call"},
    {0x438d1d, 7, {0xa1,0x30,0x1c,0x49,0x00,0x8b,0x08}, "a Present call"},
};
