# th12_hfr — high refresh rate patch for Touhou 12 ~ Undefined Fantastic Object (v1.00b)

**Test build v0.11** — runs the game's engine at your display's refresh rate (120 / 144 / 165 /
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

**Sub-tick input (new in v0.11):** the keyboard/joystick is polled on every tick, using the game's
own input routine, and the movement and focus (slow) keys are fed to the player between frames.
The player therefore reacts to a key at the next tick (1/360 s at 360 Hz) instead of the next
60 Hz frame. Shot, bomb, pause and menu keys are still sampled once per frame, exactly as before,
so shot cadence, bomb timing and menus are unchanged.

**Presentation (new in v0.11):** the Direct3D device is created through Direct3D 9Ex and the
driver's present queue is limited to one frame (`max_frame_latency=1`). This removes up to two
frames of display latency that the default queue adds; vsync behaviour is unchanged.

Sub-steps are multiples of 1/256 frame chosen so that they sum to exactly one frame (43/256,
43/256, 42/256, … at 360 Hz); this keeps the engine's float timers exact.

Stage and boss patterns and RNG use are unchanged from the original game.

## Replays

Recordings made with the patch store their tick rate and the per-tick movement/focus inputs in
the replay file (two extra `USER` chunks, ignored by the game and other tools; the input chunk is
run-length encoded and typically a few KB). Playback uses the recorded rate and replays the
per-tick inputs, so it reproduces the run on the same refresh rate. Replays without the chunks
(stock recordings) are played with stock 60 Hz logic. A patched recording played in an unpatched
game may desync — the sub-stepped simulation is not bit-identical to 60 Hz.

The sub-step sequence is restarted at the first frame of every stage, so a recording and its
playback run the same sequence of steps even at ratios such as 144/60 that do not divide evenly.

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
subtick_input=1      ; poll input every tick; movement/focus reach the player between frames
d3d9ex=1             ; create the device through Direct3D 9Ex (0 = stock Direct3D 9, also disables the next two)
max_frame_latency=1  ; frames the driver may queue (1 = lowest latency, 0 = driver default)
flipex=0             ; windowed flip presentation model (experimental)
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
* Movement and focus keys take effect at the next tick; shot, bomb and menu keys at the next frame.
* Menus, HUD text, score popups, bombs and enemy hit detection stay at 60 Hz.
* With `d3d9ex=1` the game's textures live in the default pool (Direct3D 9Ex has no managed pool);
  this is the same approach used by OpenInputLagPatch. Set `d3d9ex=0` if you see rendering problems.
* The game's own FPS counter shows the presentation rate.

## Things worth testing

* Game speed: a full stage should take exactly as long as in the original (stage timers, boss
  spell timers).
* Pause (ESC) and unpause, game over → Continue, game over → title, stage clear, stage transitions.
* Shooting with all six shot types, bombs, grazing bullets and lasers, item collection, the UFO
  summons, boss death slow-motion.
* Replays: record a run, play it back on the same display (it should reproduce the run exactly —
  the log reports "per-tick input available" at each stage start); play a stock replay.
* Input feel: tapping a direction key should move the character for as little as one tick; check
  the log's `stats:` lines for `subtick polls` (should be about refresh − 60 per second while playing)
  and the `SetMaximumFrameLatency(1) -> 0x00000000` line.
* Windowed and fullscreen, Alt+Enter switching, 60 Hz displays (the patch should be a no-op).

## Version history

* v0.11 — sub-tick input (movement/focus polled every tick, stored in replays), Direct3D 9Ex with
  a one-frame present queue, per-stage restart of the sub-step sequence for deterministic replays.
* v0.10 — first shared test build: sub-stepped bullets/player/lasers/items/stage/ANM, frame-locked
  enemies with sprite interpolation, wall-clock frame pacing, replay rate chunk.

Each version is a separate archive (`th12_hfr_v0.NN.zip`); older versions stay available next to the newest one.

## Building from source

`source/` contains everything: `hfr.c` (the patch DLL), `launcher.c`, `build.sh` (mingw-w64,
`i686-w64-mingw32-gcc`). The DLL is plain C with a few inline-asm stubs; all game addresses are
for th12 v1.00b and are verified against the expected original bytes before patching.

## Credits / references

Engine layout notes from thprac (touhouworldcup), OpenInputLagPatch (khang06) and the thtk /
truth toolchains were used as a starting point; the rest was reverse-engineered from the binary.
