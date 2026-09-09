# th12_hfr — high refresh rate patch for Touhou 12 ~ Undefined Fantastic Object (v1.00b)

**Test build v0.10** — runs the game's engine at your display's refresh rate (120 / 144 / 165 /
240 / 360 Hz …) with the game speed unchanged. Works with `th12.exe` and `th12e.exe` (English
static patch), and alongside thcrap or vpatch.

## Install

Copy into the game folder (next to `th12.exe`):

* `dinput8.dll` — the patch. It loads automatically however you start the game (`th12.exe`,
  `th12e.exe`, thcrap, vpatch). If you already have another `dinput8.dll` proxy, use the launcher
  below instead and skip this file.
* `th12_hfr.ini` — configuration (defaults are fine: vsync at the display rate, everything on).
* `th12_hfr.exe` + `th12_hfr.dll` — optional launcher that injects the same patch (needed only if
  you can't use the `dinput8.dll` route). It starts `th12e.exe` if present, else `th12.exe`
  (override with `[launcher] exe=`).

A log is written to `th12_hfr.log` next to the game — please attach it to bug reports.

Requirements: a display running above 60 Hz (windowed or fullscreen). In exclusive fullscreen
the patch requests your `fps`/`fullscreen_refresh` rate; windowed mode uses the desktop rate.

## What it does (short version)

The engine keeps its 60 Hz frame as the unit of game logic, but the update loop now runs at the
display rate. Each engine system is either

* **sub-stepped** — runs every tick with the game-speed multiplier set to the sub-step (bullets,
  the player and player shots, lasers, items, the 3D stage, all ANM sprites), or
* **frame-locked** — runs only on frame-boundary ticks with the stock game speed, i.e.
  bit-identical to the original 60 Hz behaviour (enemies / ECL scripts, HUD, menus, bombs, game
  flow, input sampling, replay recording).

Enemy and boss sprites are drawn at positions interpolated between the last two frames, so they
move smoothly although their scripts run at 60 Hz. Bullet/laser hits on the player are evaluated
every tick. Frame pacing uses vsync (a software limiter otherwise) and is anchored to the wall
clock, so the game speed stays exact even if vblanks are missed.

Sub-steps are multiples of 1/256 frame chosen so that they sum to exactly one frame (43/256,
43/256, 42/256, … at 360 Hz); this keeps the engine's float timers exact.

Stage and boss patterns, RNG use and input sampling are unchanged from the original game.

## Replays

Recordings made with the patch store their tick rate in the replay file (an extra `USER` chunk,
ignored by the game and other tools). Playback uses the recorded rate, so it reproduces the run
on the same refresh rate. Replays without the chunk (stock recordings) are played with stock
60 Hz logic. A patched recording played at a different refresh rate, or in an unpatched game, may
desync — the sub-stepped simulation is not bit-identical to 60 Hz.

## Configuration (`th12_hfr.ini`)

```
[launcher]
exe=                 ; executable started by th12_hfr.exe (empty = th12e.exe if present, else th12.exe)

[hfr]
fps=0                ; 0 = use the display's refresh rate; otherwise ticks per second
vsync=1              ; 1 = vsync (recommended); 0 = software limiter only
substep=1            ; 0 = stock 60 Hz logic, only presentation at the display rate (for comparison)
fullscreen_refresh=0 ; refresh rate requested in exclusive fullscreen (0 = same as fps / automatic)
enemy_interp=1       ; draw enemy sprites at frame-interpolated positions
log=1                ; write th12_hfr.log
debug=0              ; 1 = verbose state dumps in the log (only when asked to)

[systems]            ; per-system sub-stepping switches, for troubleshooting only
sub_BulletManager=1 sub_Player=1 sub_LaserManager=1 sub_ItemManager=1 sub_Stage=1
sub_AnmManagerWorld=1 sub_AnmManagerUI=1 sub_Bomb=0 sub_Gui=0
```

## Known differences from the original

* Bullet/laser hits on the player are checked every tick instead of once per frame, so a bullet
  can no longer "tunnel" through the hitbox between two frames (rare in the original).
* Player, item, bullet and laser motion is integrated in sub-steps; positions at frame boundaries
  match the original up to float rounding.
* Menus, HUD text, score popups, bombs and enemy hit detection stay at 60 Hz.
* The game's own FPS counter shows the presentation rate.

## Things worth testing

* Game speed: a full stage should take exactly as long as in the original (stage timers, boss
  spell timers).
* Pause (ESC) and unpause, game over → Continue, game over → title, stage clear, stage transitions.
* Shooting with all six shot types, bombs, grazing bullets and lasers, item collection, the UFO
  summons, boss death slow-motion.
* Replays: record a run, play it back on the same display; play a stock replay.
* Windowed and fullscreen, Alt+Enter switching, 60 Hz displays (the patch should be a no-op).

## Building from source

`source/` contains everything: `hfr.c` (the patch DLL), `launcher.c`, `build.sh` (mingw-w64,
`i686-w64-mingw32-gcc`). The DLL is plain C with a few inline-asm stubs; all game addresses are
for th12 v1.00b and are verified against the expected original bytes before patching.

## Credits / references

Engine layout notes from thprac (touhouworldcup), OpenInputLagPatch (khang06) and the thtk /
truth toolchains were used as a starting point; the rest was reverse-engineered from the binary.
