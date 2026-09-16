# Changelog

Newest first. Versions are the release archive names; `-test` means what it says.
Full release notes for each version are on the
[releases page](https://github.com/vittorioromeo/th12_hfr/releases).

## Unreleased

- **New Classic: bullets no longer fly too far while they are appearing.** With `[fixed60]
  substep=1`, a bullet's spawn-in animation -- the brief drift outward before it starts
  travelling properly -- was given a full extra frame of full-speed motion every frame, so it
  moved three to four times too fast and the ring a spread makes as it appears was visibly
  wider than in the unmodified game. Those states are also meant to be immune to the player
  while they play, and sub-stepping was collidable and grazeable during them; both are now
  what the 60 Hz game does. Ordinary bullet flight is unchanged and still sub-stepped.
- **New Classic: bullet animations no longer run at the tick rate.** With `[fixed60]
  substep=1`, every sub-step pass advanced each bullet's animation script a whole frame, so at
  360 Hz a script ran six times per game frame. Scripts that only pick a sprite were unaffected;
  scripts that move one were not, which is why cancel bursts scattered much further than in the
  unmodified game. The step is now made once per 60 Hz tick, like the bullet's age beside it and
  like the laser loop's equivalent. Bullet motion and collision are unchanged, and the bursts
  are still drawn smoothly between ticks.

## v0.5.3-test

Works alongside **THRotator** and **thprac** on TH10–13.

- THRotator: this patch recognises it, hands over the picture, and keeps the frame rate,
  sub-stepping, dimming, replays and input. `[video] external_renderer` controls it. The F11
  menu is drawn onto the presented image and re-acquired after every rotation.
- thprac: its practice menu never appeared before this build. The replacement update runner now
  ends on the game's own return instruction instead of one of its own, so a hook there still
  runs. The patch also names thprac's *Use VsyncPatch* / *Use OpenInputLagPatch* options when it
  finds one of those loaded, rather than giving advice that does not fit thprac's launcher.
- `package.ps1` no longer fails on Windows PowerShell 5.1; `RESOLUTION.md` and
  `MOD_COMPATIBILITY.md` now ship in the archive.

## v0.5.2-test

Works alongside **thcrap**, on Steam and standalone copies of TH10–13 alike. Earlier builds were
silently switched off by thcrap, which redirects the same imports by name and overwrote this
patch's Direct3D hook. Imports are now chained rather than replaced, and taken back once the game
is running.

## v0.5.1-test

Installs on the **Steam releases** of TH10–13, whose executables are DRM-wrapped and encrypted
until the game starts. Packaged releases were dropped from the repository.

## v0.5-test

Touhou 13 — Ten Desires, and the experimental New Classic backend, in one release.

## v0.4.x-test

Incremental releases: sharpening after the upscaling filter, the pointer in borderless
fullscreen, SSE arithmetic for the mod's clock, joystick polling on its own thread, dimming that
leaves the player's hitbox alone, TH12 curved lasers under sub-stepping, and — across v0.4.13 to
v0.4.20 — the New Classic backend: sub-tick player movement, sub-stepped bullets and lasers with
collision, and the dimming map.

## v0.3.0-test

Three games: TH10, TH11 and TH12 in one runtime.

## v0.2.0-test

The shared multi-game runtime, with TH11 and TH12 adapters, replacing the two separate per-game
patches.

## th12_hfr v0.11

Sub-tick input (movement and focus polled every tick, stored in replays), Direct3D 9Ex with a
one-frame present queue, per-stage restart of the sub-step sequence for deterministic replays.

## th12_hfr v0.10

First shared test build: sub-stepped bullets, player, lasers, items, stage and ANM; frame-locked
enemies with sprite interpolation; wall-clock frame pacing; the replay rate chunk.
