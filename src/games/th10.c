/* TH10 (Mountain of Faith), the first game of this engine.
 *
 * Everything about the picture works: the scaling modes, the filters, the resizable window,
 * borderless fullscreen, the menu, the screenshot fix and the conflict guard. None of that
 * needs an address from the game, which is the point of keeping the video path free of them.
 *
 * The simulation is not described here, so TH10 runs at its stock 60 Hz. That is not a matter
 * of filling in more addresses: TH10 predates the single game-speed float that TH11 and TH12
 * both write a literal 1.0 into at 22 sites, which is what the sub-stepping design hangs off.
 * Its top three candidate globals take 12, 12 and 10 writes, so its speed model has to be
 * worked out on its own terms first. install_sites is per-game hand-written code besides.
 *
 * A profile with no simulation addresses is a supported shape, not a broken one: install()
 * checks for them, skips what it cannot do, says so in the log, and the menu disables the
 * timing controls with the reason. See ADDING_A_GAME.md.
 *
 * NOT YET USABLE. Identification, the conflict guard, the screenshot routine and the whole
 * shader chain are verified, but the game faults at 0x42b1e0 shortly after the first frames
 * are presented, and vanilla TH10 in the same rig does not. Ruled out so far, each by removing
 * it and reproducing the fault unchanged: the screenshot stub, Direct3D 9Ex and its texture
 * conversion, and the sub-tick input hook. What is left is the device redirect itself -- the
 * render target handed to the game in place of its back buffer -- which TH10 evidently uses in
 * some way TH11 and TH12 do not. The faulting instruction reads through EBX at the entry of a
 * function near the code that writes a "TH10" file header, so the next step is to find that
 * function's caller and see what it expects to be holding.
 */
static const struct GameProfile th10_profile = {
    .identity = &game_identities[GI_TH10],
    .addr = {
        /* The one pair that is known: the game's BMP screenshot routine and its call site.
           Found from the "snapshot/th%.3d.bmp" string, and confirmed by the routine calling
           GetBackBuffer, LockRect, UnlockRect and Release through the device vtable. Without
           this the screenshot key crashes, because what the patch hands the game as a back
           buffer is a render target and those are not lockable. */
        .screenshot_fn = 0x420670, .screenshot_call = 0x4392c1,
    },
    .d3dx = "d3dx9_31.dll",
};
