**Touhou Koumakyou ~ New Classic is now supported**, experimentally, on the Steam build.

Everything below is new since **v0.4.12-test**. TH10–13 are unchanged: no gameplay,
scheduler, scaler or renderer code was touched, and a replay recorded on v0.4.12-test
behaves the same here. The only shared change is the F11 menu, which both runtimes now use.

## New Classic (experimental)

A separate x64 / Direct3D 11 runtime, since nothing from the x86 patch transfers — the game
is AMD64, uses DxLib, and advances its timers with integer increments.

- **Fixed 60 Hz simulation, presented at your display rate**, with sprite position, rotation
  and scale interpolated between native frames — including on the menus and title screen.
- **Dimming**: the background, items, effects and your own shots can each be faded so the
  bullets stand out, from the same F11 sliders and the same `dim_*` settings as TH10–13.
- **Two optional settings, both off by default**, take part of the simulation to the display
  rate. *Sub-tick player movement* polls input and moves the player once per drawn frame, so
  a direction change takes effect within the frame you make it. *Sub-stepped projectiles*
  advance enemy bullets and lasers a fraction of a frame at a time, running culling, grazing
  and collision at every step — which means a bullet that would have jumped past you between
  two 60 Hz frames can now hit you. Both have been run at 360 and 480 Hz.
- Enemies, the player's own shots, items and every script deliberately stay at 60 Hz: their
  discrete effects are applied once per 60 Hz frame, so sub-stepping their motion could not
  change an outcome.

Scaling, filters, sharpening, internal resolution and window management are TH10–13 only;
use the game's own display settings.

### Installing it

Put `dxgi.dll`, `touhou_hfr64.dll` and `touhou_hfr.ini` in the game's `th06nc` folder — the
one containing `th06nc.exe` — and **start the game from Steam as you normally would**. The
game loads `dxgi.dll` itself, so the patch installs on any launch and Steam stays the
launcher, leaving the overlay, playtime and achievements untouched. Do not put the 32-bit
`dinput8.dll` in this game.

ReShade also installs as `dxgi.dll`; only one of them can have that name in the folder.

## TH10–13

Install is unchanged — the same four files (`dinput8.dll`, `touhou_hfr.dll`,
`touhou_hfr.exe`, `touhou_hfr.ini`) next to the game.

- The Display tab no longer hides the dimming sliders on a backend without a scaler.
- The README has been rewritten for players: what the patch does, how to install it, what to
  do when something is wrong, and a limitations section that says plainly what it cannot do.

## Known limits

This is a `-test` build, and the name is deliberate.

- **A replay recorded with the patch is not guaranteed to play back in the unmodified game,
  or in a different build of this patch.** Replays recorded before the patch play back fine.
- **Scores set with this patch are not comparable** to unmodified play; don't submit them.
- **Sub-stepping changes outcomes and generally makes the game harder**, because collision is
  tested several times per frame instead of once.
- **No full-run native-versus-patched replay comparison has been made for any game.** The
  automated tests cover the scheduler, the patch transaction and the emitted machine code,
  and every supported title has been played on this build, but "the simulation is unchanged
  at 60 Hz boundaries" remains a well-argued design claim rather than a measured fact.
- New Classic's replay format stores one input word per 60 Hz frame, which cannot describe
  either of its two optional settings, and nothing turns them off for you. Turn them off by
  hand before recording or watching a replay.

The README's Limitations section has the rest, including the per-game gaps.

## Verifying the download

```
sha256  3a50818eb213a37abd6140c869743f6b14c613b21efa8f6da3c7d0cd6f5b0bc5
```

The archive contains both runtimes, the shaders, every document and the complete source. No
game file is modified by the patch, and no game file is included here.
