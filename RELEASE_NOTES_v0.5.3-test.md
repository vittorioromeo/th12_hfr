**Touhou HFR now works alongside THRotator and thprac** on TH10–13.

Everything else is unchanged from **v0.5.2-test**: no gameplay, scheduler, scaler, renderer or
New Classic code was touched, and a replay recorded on v0.5.2-test behaves the same here.

## THRotator

THRotator rotates the picture and rearranges the HUD. Both it and this patch want to decide what
ends up on screen, and two programs composing one image is how you get a picture nobody can
explain. So this patch now recognises THRotator and steps out of the picture: THRotator owns the
render target, the rotation, the layout, the window and the presentation, and this patch keeps
the frame rate and its pacing, sub-stepping, interpolation, dimming, replays and input.

Install both as each normally wants — `d3d9.dll` and `dinput8.dll` do not collide, so the game
loads them both. The log says in one block what each side is doing.

What this patch stops doing in that mode: scaling mode, upscaling filters, sharpening, internal
resolution, texture upscaling, window sizing and borderless fullscreen. THRotator does its own
version of all of those; configure them there.

It is recognised by asking: THRotator exports a version string for exactly this purpose. A
wrapper this patch does not know by name is caught a moment later by behaviour instead — one
that composes the image itself has to take over the device's back-buffer call and has no reason
to touch the swap chain, so the two answers differ. `external_renderer` under `[video]` decides:
`-1` (the default) recognises, `1` forces the same treatment, `0` keeps composing the picture
here.

**F11 works in that mode.** With a wrapper installed, the call that closes a scene *is* the
wrapper's compositor, so drawing the menu in a scene of this patch's own would compose the
rotated picture straight over it. The menu is drawn through the device underneath the wrapper
instead, so it lands on the presented image, in screen space, the right way up — and the surface
is picked up again after every device reset, which is what THRotator does each time it turns the
picture.

Confirmed on TH12 with THRotator 2.1.0 at 360 Hz, including rotation.

## thprac

With this patch installed, thprac's menu never appeared. No error, no crash, nothing in either
log — the key simply did nothing.

This patch replaces the game's update runner with its own, entered by a jump written over the
game's. Everything from that jump onwards belongs to this patch, including the function's very
last instruction — and in all four games that instruction is exactly where thprac's menu hook
sits. Its interface frame never opened, so its drawing hook had nothing to draw, and the whole
thing failed silently.

The replacement runner now finishes by jumping to the game's own return instruction rather than
returning by itself, leaving the registers and the stack exactly as the game's own epilogue does.
Anything hooked there runs as it always did. Nothing in this patch knows thprac exists; the
instruction simply stopped being taken.

That the two can share a game at all is checked rather than assumed —
`tools/check_thprac_overlap.py` compares thprac's own hook declarations against every byte this
patch writes and every byte it verifies, and for TH10, TH11, TH12 and TH13 the two sets are
disjoint. The one instruction above was the entire conflict.

**One thing to change in thprac's launcher: untick "Use VsyncPatch (if avaliable)" and "Use
OpenInputLagPatch (if avaliable)".** Both are on by default, and thprac loads either one it finds
sitting in the game's folder — an old `vpatch_th12.dll` is enough. Those replace the game's frame
limiter, which is the one job this patch cannot share, so it refuses to install beside them. When
that happens you now get a message box naming those two options rather than generic advice.

Two smaller things: the practice menu updates at the display rate rather than at 60 Hz, which is
what keeps it in step with the drawing — hotkeys are unaffected, but anything you hold to repeat
repeats faster. And leave `internal_scale` at 1 while using thprac if you can: thprac sizes its
interface from the back buffer at startup, where it agrees with this patch, but from the
presentation parameters again after a device reset, where it does not once the internal
resolution is higher.

Confirmed on TH12 with thprac 2.3.1.1 at 360 Hz, and on TH10, TH11 and TH13 against a stand-in
reproducing thprac's hook mechanism exactly. Running thprac and THRotator together with this
patch has not been tried.

## Also

- A THRotator bug worth knowing about, since it looks exactly like a compatibility failure:
  v2.1.0 aborts on a first run in a folder with no configuration, with an unexplained Visual C++
  runtime dialog. Dropping any valid `<exe>.throtator` in beside the game avoids it; THRotator's
  own `sample-config/` has one per game.

## Compatibility

Same as v0.5.2-test: TH10 v1.00a, TH11 v1.00a, TH12 v1.00b and TH13 v1.00c, Steam and standalone
alike, alongside thcrap. New Classic support is unchanged and still experimental.

Scores set with this patch are not comparable to unmodified play, and sub-stepping can change
outcomes. See the README.
