# Using Touhou HFR with other mods

User-facing notes. The audits behind them are in [MOD_COMPATIBILITY.md](MOD_COMPATIBILITY.md).
All of this applies to TH10–13; nothing here has been tried on TH08, TH14 or New Classic.

## THRotator

Install both normally; `d3d9.dll` and `dinput8.dll` do not collide. The patch recognises
THRotator at start-up and logs the division of work:

| THRotator owns | This patch owns |
| --- | --- |
| render target, rotation, HUD layout, window, presentation | frame rate and pacing, sub-stepping, interpolation, dimming, replays, input |

Disabled in this mode, because THRotator does its own: scaling mode, filters, sharpening,
internal resolution, texture upscaling, window sizing, borderless fullscreen. Configure those
in THRotator.

`[video] external_renderer`: `-1` (default) recognises THRotator, `1` forces the same treatment
for an unrecognised renderer, `0` keeps composing the picture here.

F11 works, drawn on the image THRotator presents. Confirmed on TH12 with THRotator 2.1.0 at
360 Hz.

## thprac

Install both and launch from thprac's launcher, or attach thprac to a running game. Their patch
sites do not overlap.

- **Untick "Use VsyncPatch" and "Use OpenInputLagPatch" in thprac's launcher.** Both are on by
  default and thprac loads either if it finds the DLL in the game folder (an old
  `vpatch_th12.dll` is enough). They replace the frame limiter, so this patch refuses to install
  beside them and says which box to untick.
- thprac's menu updates at the display rate. Hotkeys are unaffected; held keys repeat faster.
- Keep `internal_scale=1`. After a device reset (resize, Alt-Tab) thprac sizes its interface
  from the presentation parameters, which disagree with a higher internal resolution.

Needs v0.5.3-test or newer. Confirmed on TH12 with thprac 2.3.1.1 at 360 Hz, and on TH10, TH11
and TH13 against a stand-in reproducing thprac's hook mechanism. thprac and THRotator together
have not been tried.

## thcrap

Install both and start the game through thcrap as usual. Install order does not matter. This
patch never changes the executable on disk, so thcrap still identifies the game by hash.

With `d3d9ex=1` (default), the Direct3D object is created through 9Ex, which bypasses thcrap's
Direct3D hooks: its translation notes and device-lost handling are skipped. Text, fonts, images
and files are unaffected. Set `d3d9ex=0` to get those back, at the cost of `max_frame_latency`.

Needs v0.5.2-test or newer.

Not supported: executables that were translated on disk (the pre-thcrap English patches that
ship a modified `th10e.exe`). Their code differs, so the patch does not recognise them.

## vpatch, OpenInputLagPatch

Remove them. This patch replaces what they do, and refuses to install beside them.

## Direct3D 9 wrappers (PivotDX9 and similar)

The wrapper presents the game, so scaling mode and borderless fullscreen cannot take effect;
filters and the menu still work. The patch warns at start-up (`[video] warn_wrapper=0` silences
it).
