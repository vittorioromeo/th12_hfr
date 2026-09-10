/* Places in TH11 v1.00a that another patch is known to take over, with the bytes the stock
   executable has there. Read from clean JP and English executables, which agree at all four.

   These are vpatch's (VsyncPatch, by swmpLV/75E) unconditional patch sites, found the same
   way as TH12's and confirmed against vpatch_th11.dll: all four appear in it as literals, the
   code at each has the same shape as its TH12 counterpart, and each of the three call sites
   targets the same timing routine at 0x446920 that TH12's target at 0x4508b0. See
   src/games/th12_conflicts.h for what the check is for. */
static const struct ConflictSite th11_conflicts[] = {
    {0x446428, 5, {0xe8,0xf3,0x04,0x00,0x00},           "the frame limiter"},
    {0x4465d6, 7, {0xa1,0x88,0x32,0x4c,0x00,0x8b,0x08}, "the Present call"},
    {0x446630, 7, {0x74,0x05,0xe8,0x59,0x37,0xfd,0xff}, "the frame timing call after Present"},
    {0x419d9a, 5, {0xe8,0x81,0xcb,0x02,0x00},           "the replay timing routine"},
};
