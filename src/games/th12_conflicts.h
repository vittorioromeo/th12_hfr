/* Places in TH12 v1.00b that another patch is known to take over, with the bytes the stock
   executable has there. Read from clean JP and English executables, which agree at all four.

   These are vpatch's (VsyncPatch, by swmpLV/75E) four unconditional patch sites. Its other
   twelve depend on its INI, so they are no use for detection; these are written whatever it
   is configured to do. The first two are the ones that matter: 0x4503f8 is the game's frame
   limiter and 0x450586 loads the device pointer for the game's Present call, and this patch
   replaces the same two things. Two frame schedulers in one process means whoever writes
   second wins, and the loser's trampoline is destroyed mid-instruction.

   The check is not really about vpatch, though: it asks whether the game's frame loop still
   looks like ZUN wrote it. Anything else that takes it over trips the same wire. */
static const struct ConflictSite th12_conflicts[] = {
    {0x4503f8, 5, {0xe8,0xb3,0x04,0x00,0x00},                "the frame limiter"},
    {0x450586, 7, {0xa1,0xf0,0xe8,0x4c,0x00,0x8b,0x08},      "the Present call"},
    {0x4505d9, 7, {0x83,0x3d,0xe0,0x43,0x4b,0x00,0x00},      "the frame timing call after Present"},
    {0x41cb7a, 5, {0xe8,0x31,0x3d,0x03,0x00},                "the replay timing routine"},
};
