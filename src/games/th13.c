/* TH13 v1.00c: the video path only, so far. Everything about the picture -- scaling modes,
   filters, the resizable window, borderless fullscreen, the menu -- needs no address from the
   game and is active; the simulation (runner_fn, frame_calls) is not described yet, so the
   game runs at its stock 60 Hz. The English build (th13e.exe) is the same code with an
   appended section that loads th13e.dll; identity accepts both image sizes. */
static const struct GameProfile th13_profile = {
    .identity=&game_identities[GI_TH13],
    .addr={
        .device=0x4dc6a8,.pp=0x4dc794,
        .screenshot_fn=0x43a950,.screenshot_call=0x45d856,
    },
    .d3dx="d3dx9_43.dll",
    .native_size_cycle=1,
};
