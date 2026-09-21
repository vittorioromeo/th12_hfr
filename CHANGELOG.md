# Changelog

Newest first. Versions are the release archive names; `-test` means what it says.
Full release notes for each version are on the
[releases page](https://github.com/vittorioromeo/th12_hfr/releases).

## Unreleased

- **Touhou 15 — Legacy of Lunatic Kingdom (v1.00b), at TH14's level.** Bullets, lasers, items,
  the player and sprite animation step at the display's rate; enemies and the player's options
  are interpolated; replays carry their recording rate; dimming and the whole video path work.
  No sub-tick input. It is TH14's engine with one structural change (a timer holds an index
  into a rate table instead of a rate pointer) and one new mechanic (the graze slow-down, whose
  recovery is now per frame). Tested under Wine only so far.
- **Sub-tick input on TH14 and TH15.** Movement and focus are sampled at every tick, recorded
  per tick in the replay's HFR chunk and applied on playback, as on TH10–13. A run recorded
  at 120 Hz with sub-tick input played back to the same frame, score, graze and power.
- **Fixed: watching a replay from TH14's menu crashed** (`th14.exe+0x3676e`, during loading).
  The load wrapper did not pass the game's result back, so the caller read whatever the log
  call left in EAX as "load failed" and carried on half-built. Present since TH14 support was
  added; TH15 had the same wrapper. The shared wrapper (TH11, TH12) passes it back too.
- **Fixed: TH14 and TH15 playback was never aligned to the stage's first frame.** The profile
  named the replay speed-control node (`0x455e60`) as the playback node; the one that latches
  recorded input is `0x455e50` (TH15: `0x45cea0`). The sub-step sequence now restarts there,
  as it does when recording.
- **Fixed: TH14 (and TH15) raised an access violation while closing**, at `th14.exe+0x1ebe`:
  the game's shutdown runs the update list once more with a flag that means "call each node's
  clean-up, not its update", and the replacement runner did not know where TH14 keeps that flag
  (`layout.runner_ending = 0x54`), so it ran a normal update after DirectInput was released.
  The game survived it, which is why it only ever showed in a `debug=1` log.
- The debug traces probe memory with `VirtualQuery`, not `IsBadReadPtr`, which logged an
  access violation in kernel32 for every miss.
- `test-games.ps1`: single-pass shader filters are recognised, the F10 check expects only the
  sizes that fit (a 1280x960 game on a 1440-line desktop has two), the firing check no longer
  compares live-shot counts, and a window that ran an older version of the script is named.
- TH14 and TH15 share their enemy and option interpolation (`src/games/th14_family.h`).
- New profile fields: `layout.replay_mode`, `draw.vm_slot_off`, `draw.anm_table_off`,
  `draw.anm_slots`.

## v0.6.1-test

- **Fixed: TH14 crashed on its first frame in v0.6-test.** The compiler merged two calls through
  one function pointer that differed only in calling convention, so TH14's frame function --
  the only one that takes its context in ECX -- was called the way TH10-13's is. Every call into
  the game with `this` in ECX (the frame function, the end-of-pass cleanup, node removal, the
  replay loader) is now an explicit register call, and the harness tests the conventions.
  TH10-13 and TH08 were not affected.

## v0.6-test

Touhou 8 — Imperishable Night, experimentally, and by a different route. TH14 fixes, and the
same fixes swept across TH10–13.

- **TH08 is presented at the display's rate from a simulation that stays at 60 Hz.** Direct3D 8
  is translated to 9 inside the patch, so scaling, filters, borderless fullscreen, internal
  resolution, dimming, screenshots and the F11 menu all apply. Nothing in game state is
  written: a per-frame trace of the title screen's demonstration is identical to the unpatched
  simulation's, so replays work in both directions.
- **The playfield is shown in the present** (`[fixed60] predict=1`). Interpolating between the
  last two 60 Hz states draws the player up to a frame late, which is input lag the stock game
  does not have. Bullets, enemies and the stage are carried forward along their last step
  instead, and the player is drawn where the keys held now will put her at the next tick.
  Menus and the interface are interpolated.
- Sprites are smoothed as rigid quads — centre, turn and scale — rather than as four
  independent corners, which made spinning bullets shimmer. The 3D stage background is
  smoothed through its scroll position and camera.
- `[fixed60] subtick=1`: sub-tick player movement as in New Classic. Off by default; not
  replay-safe, and off while a replay plays.
- `[fixed60] substep=1`: sub-stepped bullets, lasers and items on TH08. Experimental and off by
  default. Bullets match the stock game slot for slot at every 60 Hz boundary; collisions are
  tested at every step, so it is not replay-safe and is off while a replay plays.
- TH08 runs on the shared frame scheduler, with its own walker for the older engine's Chain.
- The Direct3D 8 bridge no longer requires `d3dx9_43.dll`, and steps over another patch that
  holds `Direct3DCreate8` instead of giving up the video path.
- Fixed: the F11 menu drew into a dead device when a game recreates its device in-process.

Touhou 14 and the TH10-13 sweep:

- **TH14: Reimu's shot no longer stops while the button is held.** The 14-step firing cycle is
  rewound by `timer_rewind`, which multiplies the amount by the game speed -- sub-stepped it
  rewound by 14/6, so only the last two steps of the pattern ever came round again. Tapping the
  button restarted the cycle, which is why tapping worked and holding did not.
- **TH14: F10 cycles the window size.** The profile claimed TH14 had its own F10; it does not --
  its window procedure handles only Alt+Enter and swallows the menu key. The patch supplies the
  cycle now, as it does for TH10.
- **TH14 replays carry their recording rate.** The four calls into the loader separate into two
  that play a replay and two that read a header for the menu's list; only the first two are
  hooked, which is what took the game down when all four were. TH14's replay magic is `t13r`,
  not `t14r`, and it keeps its replays under `%APPDATA%\ShanghaiAlice\th14\replay\`.
- `test.sh` runs the emitted-machine-code tests, which only `test.ps1` did, and there is now a
  TH14 set covering the shot cycle and the weapon timer.
- **F10 now works on TH11, TH12 and TH13 as well.** All three were marked as having an F10 of
  their own; none of them does -- like TH14, they handle only `WM_SYSCOMMAND` and never read
  VK_F10 -- so the key did nothing. The patch supplies the size cycle on every game now.
- **TH11, TH12 and TH13 hook both of the replay loader's play sites.** A replay start dispatches
  on a mode of 1 or 2 and only mode 1 was hooked, so a replay started the other way played back
  at 60 Hz without its recorded rate, silently.
- **TH10 no longer hooks the replay loader's header-reading site.** That site builds a throwaway
  manager to read a file's header for the menu's list; hooking it read simulation metadata off
  every replay on disk when that menu opened, which is the same mistake that crashed TH14 when
  its replay extension was first installed.

Tests and documentation:

- `test-games.ps1` gains `-Matrix` (one short launch per INI setting: frame cap, sub-stepping
  off, plain Direct3D 9, every shader filter, sharpening, internal resolution, texture
  upscaling, the launcher route) and `-Drive` (starts a stage with injected keys, holds fire,
  checks the simulation rate and TH14's shot cycle). It now covers TH08, checks that the F11
  menu is actually drawn, compares the installed DLLs with the build, restores each game's INI,
  and keeps every launch's log and screenshots.
- `docs/GAME_DIFFERENCES.md`: every game-specific behaviour side by side, with abstraction
  candidates; `tools/check_game_differences.py` keeps it current from both test suites.
- The README is half its length; mod compatibility, replays, upgrading, building and credits
  moved to `docs/`. The developer notes were rewritten as reference rather than diary.

## v0.5.5-test

Touhou 14 gameplay runs at the display's refresh rate.

- **TH14: bullets, the player, items and lasers move between 60 Hz frames**, with collision,
  culling and grazing tested every tick. Enemy sprites and the player's options are interpolated
  between their 60 Hz positions instead, because enemy logic is a script interpreter and the
  options chase the player by a proportion of the remaining distance.
- **TH14 replays are not rate-aware yet.** A replay records and plays back, but always at 60 Hz,
  so a run played with sub-stepping on may not reproduce. Measured, not assumed: bullet delay
  timers start a frame early under sub-stepping. Enemy and script behaviour is bit-identical
  across rates. Play with `substep=0` if a run matters.
- **The version in the log is no longer hard-coded** and had been stale since v0.5.3-test. The
  F11 menu's title carries it now as well.
- **The crash reporter keeps a slot per faulting address** instead of a budget of four, which
  harmless startup exceptions used to exhaust before the title screen.
- `replay_trace`, `replay_trace_from` and `replay_trace_to`: debugging aids, off by default,
  active only with `debug=1`.
- `docs/FIXED_STEP_RESEARCH.md` and `tools/research/`: an audit of fixed-rate simulation. No
  runtime change.

## v0.5.4-test

New Classic: sub-stepped projectiles behave, and the patch loads on Proton.

- **New Classic loads under Proton.** The game resolved its DXGI factory from the system
  library rather than from the proxy beside it, so the patch was never started -- no error,
  just an ordinary 60 Hz game. The proxy now recognises that startup path as well as its own.
  The override is still `WINEDLLOVERRIDES="dxgi=n,b" %command%`.
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
