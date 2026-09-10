/* Places in TH13 v1.00c that another patch is known to take over, with the bytes the stock
   executable has there. Found the way ADDING_A_GAME.md describes: every immediate in
   vpatch_th13.dll landing in the code section, kept where the code has the right shape --
   the frame limiter's call to the timing routine (0x45d880), the Present call's device load,
   the timing check after Present, and the replay timing call. Same bytes in both executables. */
static const struct ConflictSite th13_conflicts[] = {
    {0x45d2f6, 5, {0xe8,0x85,0x05,0x00,0x00},                "the frame limiter"},
    {0x45d4b6, 7, {0xa1,0xa8,0xc6,0x4d,0x00,0x8b,0x08},      "the Present call"},
    {0x45de73, 7, {0xf6,0x05,0xa8,0xc8,0x4d,0x00,0x02},      "the frame timing check after Present"},
    {0x42499b, 5, {0xe8,0xe0,0x8e,0x03,0x00},                "the replay timing routine"},
};
