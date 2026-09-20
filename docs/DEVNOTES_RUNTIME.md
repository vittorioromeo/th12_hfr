# Developer notes: the shared runtime

Cross-game findings for the shared x86 runtime: design decisions with their reasons, measurements,
traps, and open work. Section numbers are referenced from source comments and other documents;
keep them stable.

| Document | Covers |
| --- | --- |
| [ARCHITECTURE.md](../ARCHITECTURE.md) | source map, installation, replay format |
| [ADDING_A_GAME.md](../ADDING_A_GAME.md) | the porting procedure, profile states, finding per-game fields |
| [RESOLUTION.md](RESOLUTION.md) | scaling, filters, presentation, window and menu in detail |
| [TESTING.md](TESTING.md) | the harness and the test suites |
| [MOD_COMPATIBILITY.md](MOD_COMPATIBILITY.md) | THRotator/thprac source audit, confirmed hook conflicts, proposed compatibility work |
| [FIXED_STEP_RESEARCH.md](FIXED_STEP_RESEARCH.md) | scheduling measurements and the fixed-step proposal |
| [games/](games/) `*_DEVNOTES.md` | per-game addresses, layouts and hooks |

The original per-game build's notes (`DEVNOTES.md`, `TH11_DEVNOTES.md`) now live in
[TH12_DEVNOTES.md](games/TH12_DEVNOTES.md) and [TH11_DEVNOTES.md](games/TH11_DEVNOTES.md); the
TH12 file holds the original sub-stepping design.

---

## 1. What is shared and what is per-game

This document covers TH10, TH11, TH12 and TH13 on the x86 runtime. TH14 and TH08 have adapters
and their own notes in [games/](games/); §11 records what TH14 fed back.

The picture is one implementation for every game and uses no game address: arbitrary window
resizing with letterboxing, three scaling modes, borderless fullscreen, a multi-pass filter chain
with four bundled upscalers, a Dear ImGui menu covering every setting, the screenshot fix, the
crash reporter, and the guards against other patches.

The high frame rate is almost entirely per-game and needs addresses.

Consequences of that split: TH11 got the whole video feature set with no code change; a game
whose engine is not worked out can still have all of the picture; and a profile can ship the
shared half while its simulation half is unfinished (§6).

---

## 2. Direct3D 9 rules

Detail for most of these is in [RESOLUTION.md](RESOLUTION.md) (2.3, 2.3.1, 2.5).

- **Never read from a surface that is currently bound as the render target.**
  `GetRenderTargetData` and `StretchRect` both hang the device. Unbind, copy, rebind. The
  screenshot fix failed three times on this.
- **`ps_3_0` cannot be used with the fixed-function vertex pipeline.** Filters are drawn through
  a pass-through vertex shader with clip-space vertices; the built-in blits use the
  pre-transformed path.
- **The device is never reset.** `FUN_00431700`, the game's pre-reset release, only releases
  three pointers that are never assigned anywhere in the build, so the game reloads nothing after
  a reset, and with Direct3D 9Ex every texture is in `D3DPOOL_DEFAULT`, whose contents are
  undefined after one. Presentation goes through an additional swap chain of our own. The game's
  own Alt+Enter has been broken since the 9Ex conversion.
- **Intermediates carry alpha whatever the back buffer is.** The windowed back buffer is
  `X8R8G8B8`. ScaleFX's first pass writes an edge distance in each of four components; a quarter
  of its data was dropped and later passes chose the wrong neighbour. Symptom: a dotted outline
  tracing every sprite.
- **A chain that overshoots the window is averaged down, not sampled.** ScaleFX's 3x image in a
  1.5x window: one bilinear tap per destination pixel keeps two source pixels out of every three
  and gives the same dotted edge. Four bilinear taps at the quarter points of the destination
  pixel's footprint average it.
- **Reset the texture-coordinate transform before any fixed-function draw of your own.** TH10's
  stage renderer leaves `D3DTSS_TEXTURETRANSFORMFLAGS` enabled on stage 0 for its scrolling cloud
  layers. The Dear ImGui backend does not reset that state, so during gameplay the menu's UVs
  went through the game's texture matrix and the overlay sampled nothing; at the title screen it
  was fine. Found by bisecting a mask of state resets at run time, which is the fast method when
  the symptom is "draws nothing".

The alpha and resample faults look identical on screen and have unrelated causes: "it renders
something" is not evidence that a filter port is right.

---

## 3. Filters

Shaders compile at run time with whatever `d3dx9` is available; no bytecode is shipped. A filter
is a shared header plus `//! pass` blocks, each with its own `//! scale` and optional
`//! float`. A pass can read the previous pass, the game's original image, and any earlier pass.
The rules live in `src/core/shader_parse.h`, included by both the runtime and the checking tool.
When the tool had its own copy it drifted: it validated against a two-sampler prologue that no
longer existed. The file format is in [RESOLUTION.md](RESOLUTION.md) 2.3 and `shaders/README.md`.

### Which compiler

- TH10 ships `d3dx9_31` (2006). Its HLSL compiler rejects an early return inside an `if`
  (`error X3500: asymetric returns from if statements not yet implemented`, spelled that way) and
  cannot build MMPX or Super-xBR.
- From `d3dx9_42` onward, `D3DXCompileShader` forwards to a separate `D3DCompiler_NN.dll` that
  may not be installed, and then it fails too.

The loader therefore ignores version numbers. It asks each candidate to compile a probe that uses
the features the bundled filters use and takes the first that passes. In a real TH10 run it
rejected 43, 42 and 41 and settled on 40.

### What is bundled, and what cannot be

All four bundled filters are MIT and were ported from upstream GLSL or Cg, never from Magpie.
Magpie is GPL-3.0 as a whole and its effect files carry no separate header, so its HLSL port of
an MIT shader is still GPL-3.0.

| Bundled | Licence | Shape |
| --- | --- | --- |
| MMPX | MIT | 1 pass, 2x |
| xBR-lv2 (Hyllian) | MIT | 1 pass, free scale |
| Super-xBR (Hyllian) | MIT | 3 passes, 2x |
| ScaleFX (Sp00kyFox) | MIT | 5 passes, 3x |

ScaleFX's output contains only colours from the original, so it cannot ring or halo on sprite
edges.

| Not bundled | Why |
| --- | --- |
| xBRZ | GPLv3, including its shader ports. |
| hqx | LGPL-2.1+ in every implementation whose provenance can be traced. Two forks ship permissive licences (brunexgeek claims Apache-2.0, janert claims MIT) but each documents deriving from LGPL/GPL sources. |
| NNEDI3 | GPL/LGPL down to its trained weights. |
| FSRCNNX | LGPL-3.0 shader, GPL-3.0 trainer. |
| Anime4K | MIT, but its cheapest useful preset is ~25 passes (the limit is 8) and it is trained to repair compression-damaged 1080p anime video, not 640x480 sprites with hard one-pixel edges. |

A filter dropped into `shaders/` can be under any licence. `shaders/README.md` carries the
evidence for each entry.

### Post-processes

Sharpening is not a filter. A filter decides how the game's 640x480 pixels become the window's;
a sharpening pass takes the resampled picture at the window's size as its input. Post-processes
are a separate list (`g_posts`, files marked `//! post`), chosen separately in the menu, and run
in `scaler_blit` after the final draw:

- With one selected and its strength above zero, the final draw goes into a window-sized
  intermediate in the back buffer's format (`g_post`) and the pass draws that into the back
  buffer at the same rectangle. Cost: one window-sized draw.
- With none, the final draw goes straight to the back buffer.
- Strength arrives as `Params.x` (`c3`, added to the prologue and zeroed for every filter pass).

`cas.hlsl` is AMD's CAS with the better-diagonals soft min/max; the strength drives AMD's own
`-1/lerp(8,5,s)` peak. `unsharp-mask.hlsl` is the textbook operator on luma, clamped to ±0.12,
kept as the reference point. Both checked in the rig on TH12 at 1600x1200: CAS at 100% visibly
tightens sprite edges without a halo.

### The pointer

The game hides the pointer whenever it believes it is fullscreen: `while (ShowCursor(FALSE) >= 0);`
at the switch and `SetCursor(NULL)` on every `WM_SETCURSOR`. TH10-13 all do it the same way,
through their import tables. Over the borderless window that leaves no pointer.

Both imports are hooked (window.c). The game's calls only test the sign of the count, so it is
given a count kept in -1..0. What Windows sees is decided once a frame by `cursor_want_visible`:
the menu open wins, then the game's own wish in a real window, then `cursor=` for borderless
(hidden / visible / visible for two seconds after the last mouse message). `SetCursor(NULL)`
becomes the arrow whenever the pointer is meant to be visible, since a null cursor image hides it
regardless of the count. ShowCursor's count is per thread, so every real call is made on the
game's thread: the hooks run there, and so does `window_pump`.

## 3a. Internal resolution and texture upscaling

Positions are floats (TH12/TH13 round `MotionState` positions to 1/100 px), but the game draws
into a 640x480 target that is then magnified, so at 360 Hz a slow bullet still steps from pixel
to pixel. The sub-pixel information is lost at rasterisation.

**`video.internal_scale = N`** hands the game a target N times its own size and scales what it
submits to that target (d3d9.c): viewports, clear rectangles, and every pre-transformed `XYZRHW`
vertex in `DrawPrimitiveUP`, keeping the D3D9 half-texel rule (`x' = (x + 0.5)·N − 0.5`).

- Draws to any other target (the game's render-to-texture surfaces, our presentation chain) are
  untouched; the `SetRenderTarget` hook decides. Anything drawn through a real projection needs
  nothing.
- The screenshot path reduces the big surface back to 640x480 first, because the game writes its
  BMP from the locked surface with sizes from its own present parameters.
- The engine's screen capture (`D3DXLoadSurfaceFromSurface` with a 640x480-coordinate source
  rect: the pause backdrop, spell backgrounds) has its source rectangle scaled; otherwise it
  shows the top-left quarter.
- The device is created at N× once. Changing it needs a restart, so it is an INI setting and not
  a menu item.

**Scaling alone only sharpens.** With the target at 2× the vertex stream still carries no
fractions other than 0 and ½: every 2D vertex sits on a whole 640x480 pixel. The ANM quad builder
(`0x467350` in TH13) rounds the four corners with `frndint` before the half-texel offset whenever
the VM's flag bit 0 is set, which is nearly every sprite; the other draw modes (rotated, 3D)
never round. Four two-byte NOPs, kept as frozen `sprite_round_sites` in the profile and applied
only when `internal_scale > 1`, remove the rounding; the stream then carries 0.85, 0.52, 0.30 …
during gameplay. The game's sampler is bilinear, so a bullet at x = 100.3 sits between pixels.
Games without known sites get the sharper raster and the log says so.

**Verification.** Screenshots cannot show this: the rig presents at ~25 fps, so two captures are
many frames apart. The `DrawPrimitiveUP` hook counted vertices off the pixel grid and kept the
set of distinct fractions seen, before and after the NOPs.

**Effect on filters.** Upscaling filters see an N× source. They still run, but sprites are
already magnified by the game's bilinear sampler, so MMPX or xBR have little to do; "nearest" or
"bilinear" is the natural pairing. "Pixel perfect" is relative to the N× surface.

**Texture upscaling** (`video.texture_scale = N`, `texture_filter`; `src/core/texscale.c`).

- Every texture the game creates goes through the device's `CreateTexture`, D3DX's loaders
  included (`D3DXCreateTextureFromFileInMemoryEx` is hooked as well), so that hook registers them.
- On a texture's first `SetTexture` a render-target copy N times its size is made and the
  pixel-art filter runs into it on the GPU. The bind then substitutes the copy; UVs are relative,
  so the game needs no change.
- The bundled filters write alpha = 1 and reason about colour only, and a sprite sheet is mostly
  alpha. Each texture is run twice: once as colour premultiplied by alpha (transparent texels
  read as black), once as alpha spread to grey. A final pass divides one by the other and writes
  the alpha, so the silhouette gets the same treatment as the colours.
- Textures the game rewrites (the stage title, dialogue text: GDI into a DIB, then
  `D3DXLoadSurfaceFromMemory`) are marked stale by the surface-load and `LockRect` hooks and
  redone on their next bind. The class `Release` hook drops the copy with the texture.
- Excluded: render targets, mipmapped and oversize textures, and everything past a 512 MB budget.
  Our own draws (the presentation blit, the menu) run with substitution off.
- Verified in the rig at 2×+2× with xBR-lv2: title art, gameplay, stage title, trance background,
  HUD, all clean, ~17 MB of copies in stage 1.

With N× rasterisation and sub-pixel placement, texture upscaling is an HD mode rather than a
smoothing filter.

**Trap.** The three D3D9Ex managed-pool hooks sit next to the texture hooks in the same file.
Dropping them makes every managed vertex buffer creation fail and the game crashes at startup in
its supervisor init. `git diff` on the file finds it; the log does not.

---

## 3b. Dimming: which draw belongs to which object

The feature fades the stage towards black and chosen sprite classes (items, effects, player
shots, TH13's spirits) towards transparent so bullets stand out. The work is attributing each
draw call to the object that made it.

### What does not identify a draw

- **The sprite layer.** The ANM manager draws its layers through one function with the layer
  number in EAX (TH13: `0x46f380`), and thanm's listings give a layer per script (bullets 15,
  items 10, enemies 8, player 11). There are no draws on layers 8, 10 or 15, ever. EnemyManager,
  ItemManager, BulletManager and the player draw their VMs themselves from their own draw
  callbacks (24 callers of the VM draw `0x46a700`); the layer lists carry only free-standing
  effects and interface pieces. The layer is a property of the script, not of who draws it.
- **The draw call.** The sprite manager batches: consecutive 2D sprites with the same texture and
  blend accumulate and are flushed as one `DrawPrimitiveUP` when either changes or on request.
  The items' quads are typically flushed by the first sprite of the next callback (the player's
  draw showed 34 primitives in one call).

### Attribution by draw callback

Every object registers a draw callback with a priority; the draw runner walks them in order
(TH13 `0x470c30`, list at manager+0x40, dispatch `mov ecx,[esi+0x24]; mov edx,[esi+8]; call edx`).
The profile names that dispatch and `dimming.c` wraps it: flush the batch, record the node's
priority in `g_draw_prio`, call the callback, flush again, forget the priority. Every draw call
then happens under exactly one callback. The two extra flushes per callback are no-ops when
nothing is pending, which is nearly always.

| | TH10 | TH11 | TH12 | TH13 |
| --- | --- | --- | --- | --- |
| batch flush (`ESI` = the ANM manager, whose pointer the profile already has) | `0x442f50` | `0x44fd10` | `0x45a3c0` | `0x4679a0` |
| sprite VM draw (VM in EAX, six carried bytes) | `0x4451c0` | `0x451ef0` | `0x45c900` | `0x46a700` |
| VM: pointer to the loaded ANM | +0x308 | +0x3b0 | +0x3f8 | +0x30 |
| VM: 16-bit script index | +0x38a | +0x3a2 | +0x3ea | +0x4aa |
| `world_prio` | 11 | 11 | 12 | 12 |
| ItemManager callback | 25 | 25 | 27 | 26 |
| BulletManager callback | 29 | 29 | 31 | |
| bullet-layer callback | 33 | 32 | 34 | |

Per-game draw runner and dispatch addresses are in each game's devnotes.

### Where the background ends

TH11 onwards render the stage into an offscreen target, switch to a second one for the world,
copy the first into it, then copy back and forth for effects and finally onto the back buffer.
Dimming "at the first world draw" loses to the copy that follows it. The dim quad is drawn
*before the first callback with priority >= world_prio*, over the current viewport, which at that
moment is the finished stage in whichever target holds it; every later copy carries the dim.

`world_prio` is the callback that switches targets (TH11 11, TH12 12, TH13 12). TH10 draws
straight into the back buffer, so it is the first callback after the Stage's 2D pass (11); there
the viewport is the playfield and the interface is painted around it afterwards.

### The items

Items are the ItemManager's callback (priorities in the table). Its draws have their vertex alpha
scaled in the copy the internal-resolution path already makes, or their colour when the
destination blend is ONE. Draws that lack a diffuse component are left alone; none of the four
games' item draws do.

### Finer than a callback: the sprite VM

The sprite-layer callbacks draw the player's shots, the spirits, the bullet cancels and the hit
sparks from one list, and TH11 draws the enemies through it as well. So the sprite VM draw is
wrapped too (addresses in the table). Before a VM draws, C classifies it; when its class differs
from the batch's, the batch is flushed first, and the class rides with the batch to its draw
call.

A VM is identified by a pointer to its *loaded ANM* (a struct that begins with the slot index and
the file name, the same in all four games) and its sprite layer. Both were found by dumping the
first 300 words of a few VMs per callback under `debug=1` (the trace still does it): the pointer
by looking for words that point at "n.anm", the layer at +0x24 / +0x20 by matching the
layer-thunk numbers. The sprite id is also there (TH13/TH11 keep `slot << 16 | sprite` next to
the pointer, TH12 a plain id at +0x3e0) but no rule has needed it.

Rules in a profile:

- `pl*.anm` on layers 10..13 are the player's shots (the body scripts set no layer, in every game
  and every character).
- `astral.anm` is TH13's spirits; `effect.anm` the effects.
- `bullet.anm` on anything but its item and bullet layers is the bullet cancels.
- `enemy.anm` is never touched.
- The manager callbacks (lasers, bullets) are excluded by priority first.

Cost: two words stored and a table walk per VM draw, and a flush where classes alternate, a few
per frame.

**A classified VM is flushed on its own.** Setting the class at VM entry and letting the batch
run on fades the wrong things: boss sprites and spell-card portraits faded "in certain
animations". Quads from paths the wrap does not see (a manager building quads itself, as TH13's
Effects manager does by calling the quad builder directly; a VM drawn through another entry) land
in a batch whose class an earlier effect VM set. The VM draw is therefore wrapped at both ends.
The entry stub swaps the caller's return address for an exit stub and keeps the real one on a
small stack, since children re-enter the draw. A classified VM's quads are flushed before it if
something else is pending and after it always, so no faded draw call carries another object's
quads. Unclassified VMs batch as before. Cost: one draw call per faded sprite, which is what
3D-mode sprites cost the game anyway.

**A sprite gets its colour one of two ways.** 2D-mode sprites carry it in the vertices, and the
copy the internal-resolution path makes is faded there. 3D-mode sprites (`ins_302(1)`; TH13's
petals, enemy deaths in TH10-12) are drawn from a static unit-quad vertex buffer with the colour
in `D3DRS_TEXTUREFACTOR`; the `DrawPrimitive` hook fades that and puts it back. Missing the
second path is why death explosions did not fade. The background class fades colour rather than
alpha in both paths: TH10 draws some spell backgrounds as sprites *above* the world (stage 2's,
on layers 4-5 → priority 16-17), and a rule can name them `DIM_BACKGROUND` so they darken like
the rest.

**Script ranges.** Rules carry an optional script range, read from the VM's 16-bit script index
(offsets in the table; found at the VM-init function, the one that stores the loaded-ANM pointer:
`mov word [vm+X], bx` with bx the script parameter, next to `mov word [vm+X-4], cx` for the
slot). The only layer shared between classes so far is TH12's `enemy.anm` layer 7, where the UFOs
(scripts 135-138, enemies the player shoots) sit among the spawn flashes. The trace prints
`anm:layer/script` per VM so a rule can be written from the table without a listing.

### Where the enemy deaths and the hitbox are

| | TH10 | TH11 | TH12 | TH13 |
| --- | --- | --- | --- | --- |
| death bursts | `bullet.anm` 353-442, layer 13 | `bullet.anm` 75-188, layer 15 | `bullet.anm` 78-152, `ins_68(16)` | `effect.anm` |
| player hitbox (`DIM_NONE`) | `bullet.anm` 351-352 | `bullet.anm` 73-74 | `bullet.anm` 76-77 | `pl*.anm` layer 12 |
| first death scripts | 353/355 | 75/77 | 78 | |
| `enemy.anm` lowest layer (spawn flashes, auras) | 4 | 6 | 7 | |

- The TH10-12 death scripts set the *bullet* layer themselves and draw under the bullet-layer
  callback. That layer looks like it holds the bullets. It does not: the BulletManager draws
  bullets from its own callback with the VM's layer left at 0. The layer must not be excluded.
- The two scripts before each death range are the player's hitbox, the two sprites that turn
  about the player while focused, drawn on the same layer by the same callback. Each game has a
  script-range `DIM_NONE` rule ahead of the `bullet.anm` catch-all. In a trace the pair shows as
  one draw each per frame, `v0` orbiting the player's centre at a radius of 45 (a rotating
  64-pixel square's corner) while the body's `v0` stays put.
- Two readings of the ANM listings are wrong. The sixteen one-sprite additive scripts on
  `enemy.anm`'s lowest layer are spawn-in flashes and auras, not deaths (they are effects, so the
  effect rule still applies). The small circles of `bullet.anm` script 164 never draw at all.
- Method: `debug=2` over the title demo, which kills things on its own, plus a burst of `xwd`
  screenshots (`xwd -root` takes 90 ms, an `import` two seconds). The per-quad `uv` in the table
  names the sprite and thanm names the script. For the hitbox: a `debug=2` run with the focus key
  held for eight seconds (the log line count before and after marks the window), then the VMs
  present in every focused table and no unfocused one.

### The draw census

The per-frame trace samples one frame every ten seconds. Anything short-lived (an item, a bomb,
a death) can miss every traced frame; TH14's items were missed twice. With `debug` on, every VM
draw of every frame feeds a `(priority, ANM, layer)` census, reported on the stats line with how
the current rules classify each row:

```text
draw census: priority 35, bullet.anm layer 0, 41022 draws -- not faded
```

A manager that builds its own quads draws no VMs; TH13's items do this, which is why its item
rule matches on priority with a NULL ANM. A second row type counts, per draw callback, how many
draw calls it made and whether any VM was behind them:

```text
draw census: priority 65, (no VM: its own quads) layer -1, 8100 draws -- not faded
```

The census copies the ANM name. Keeping the `const char*` the VM hands over and comparing
pointers is wrong: an ANM record is freed and its memory reused, so names print as garbage, every
reallocation of the same file becomes another row, and the fixed table fills before the
short-lived sprite arrives.

### Known imprecision

- The options orbit the player on the shot layer, so they fade with the shots.
- TH13's trance re-blends the stage texture over the world with `DESTCOLOR/INVDESTCOLOR`, which
  brightens the dimmed stage back towards its texture. Rare and short; left alone.

### Frame-time diagnostics

With the five-second stats, when a frame ran long:

- `hitch:` for any gap over 40 ms between presents, with that frame's draw calls, forced batch
  flushes, sprite VM draws and texture upscales.
- `frame time:` with the longest time spent *inside* the game's frame function (its draw and the
  present, vsync included) and *outside* it (its loop and its own waits), each with a count of
  frames over 8 ms, a span breakdown (before the first draw, drawing, in Present, after Present)
  and a histogram of the gaps between presents.
- `joystick:` counts the real `joyGetPosEx` calls made on our thread and the longest of them (§4).

Under `debug=1`, additionally:

- a `long frame:` line for each of the first sixty frames over 12 ms, with its span breakdown;
- once: `clock:`, `process:` (compat layer, version lie, non-Windows modules) and `monitors:` (the
  window's placement, styles and every monitor's rate);
- for the first few windows: `threads:` (affinity, priorities, per-thread CPU), `focus:` (the
  foreground window and EcoQoS throttling) and `screen:` (DWM composition rate, dropped frames,
  the chain's present statistics);
- a 1 kHz sampling profile of the main thread (`sampler.c`: modules by share, and the game-side
  return addresses seen while it sat in the system).

None of the `debug` probes run by default.

A histogram sitting on 15.6 ms with exactly 64 long gaps a second is a 24-bit clock, not the game
(§4). TH10's real hitches (a brief `0.0fps` reading every few seconds) were its native FPS
watchdog: rates over 65 FPS are taken for a broken clock, the fourth reading zeroes the QPC
frequency, and the retained deadline loop then runs through ~14 M iterations of catch-up (28 ms).
The adapter bypasses that recovery branch; [TH10_DEVNOTES](games/TH10_DEVNOTES.md) §6b has the
code, the TH13 comparison and the regression fixture.

### Debug levels

| Level | Output |
| --- | --- |
| `debug=1` | state dumps; a draw table every ten seconds of a stage, with VM word dumps |
| `debug=2` | the table every half second, no word dumps |
| `debug=3` | every other frame for the first minute; the log flushed once per frame instead of per line (per-line flushing under Wine halved the frame rate); the first vertex of every batched draw |

## 4. Bugs and the rules they left

### The clock that lied

**Symptom.** Every TH10-12 log at 360 Hz showed 64 long gaps a second, a gap histogram sitting on
15.6 ms, and presents arriving in bursts. TH13 showed none of it.

**Cause.** Direct3D 9 created without `D3DCREATE_FPU_PRESERVE` puts the calling thread's x87
control word into 24-bit precision and leaves it there. TH13 sets 53-bit back in-game; TH10-12
never do. The mod's clock is `QueryPerformanceCounter / frequency` in a `double`. The `fild`
loads the counter exactly, but the `fdiv` rounds its result to 24 significant bits, so seconds
since boot come back in steps of (uptime / 2^24): about 5 ms after a day of uptime, 15.6 ms after
three. Every `now_s()` was rounded to that grid. The "stalls" were one grid step, the "bursts"
several frames rounding to the same value, and the limiter's deadline arithmetic ran on the same
rounded clock.

**What it looks like and is not.** The pattern is the Windows timer's rhythm, which points at the
compositor, driver frame queueing, EcoQoS, timer resolution or the window style. An event-query
GPU sync, a no-vsync software pacer and `flipex` all made play worse ("much worse", per the user)
without changing the pattern.

**Diagnosis.** Time the same 2 ms `NtDelayExecution` three ways: QPC in our double said 15.6 ms,
the kernel's interrupt time said 2.0, `timeGetTime` said 2.

**Fix.** `-msse2 -mfpmath=sse` for every C and C++ unit (build.sh and build.ps1); SSE arithmetic
does not read the x87 control word. The C runtime still formats through x87 in `vfprintf`, so
`logf_` wraps it with `_controlfp(_PC_53)` and restores the game's setting after.

**Rule.** On the game's thread the FPU is in the state the game wants. Do not trust a double
computed there unless the arithmetic is SSE.

### The joystick that enumerated

TH10-12 call `joyGetPosEx` from the frame function before the game logic. With no controller
attached, winmm re-enumerates HID devices on the calling thread every second or so, 8 to 30 ms
each time. At 60 Hz that hides inside a frame; at 360 Hz it is a hole of ten frames a few times a
second.

The call is made continuously on a thread of our own (500 Hz with a controller, every quarter
second without). The game's call is answered from the latest reading under a critical section,
with the caller's `dwSize`/`dwFlags` put back. The hook is installed for every game whether or
not sub-tick input is on, because the stall is the same either way. The `joystick:` stats line
shows how long the real calls take.

### The menu key that stopped working

A single press was announced twice, once by the window's key messages and once by the runtime's
own poll of the key. When the two landed in different frames the toggles cancelled: eight
scripted taps produced four toggles.

Both routes now report whether the key is **down**, and one function turns that level into a
press, so the routes cannot disagree about the number of presses. Three remaining wedges are
closed: a KEYUP lost when the window loses focus mid-press, a message-route "down" that outlives
the hardware, and a press that begins and ends between two polls.

### The menu that could not be brought back

The menu was open when the game changed resolution. Opened centred on a 1817x1156 surface it sits
near (900, 580); the game switches to 640x480 and that is off the edge. ImGui clamps windows only
enough to keep the title bar reachable, which leaves nearly all of a 620-wide window outside a
640-wide surface.

**Trap.** `ImGuiCond_Appearing` looks like it re-places the window on every reopen. It never
fires: while the menu is hidden the render function returns before `ImGui::NewFrame()`, so
ImGui's frame counter does not advance, and "appearing" means not submitted for at least two
frames. The menu tracks its own placement instead ([RESOLUTION.md](RESOLUTION.md) 2.6.2).

### Video features with no visible effect on TH10

Everything installed and no scaling mode or filter changed the picture. The window was 640x480.
TH10's own dialog offers nothing larger (TH11 and TH12 offer 960x720 and 1280x960), and at
exactly the game's size every scaling mode and every upscaler produce the same 1:1 picture.
`window_scale` sizes the window at startup (and from the menu), and the INI comment explains why
it exists.

### Positional lists

The config defaults were a bare list of eighteen values in struct order, so inserting a field
anywhere but the end silently shifted every default after it. Inserting TH10 at the front of the
identity table silently repointed TH11's and TH12's profiles at their neighbours' identities.
Both now use names (designated initialisers, named enum slots).

### Dead code that duplicated live code

Binding an intermediate and preparing the pipeline existed twice, in `run_prepass` and inside
`run_chain`, and `run_prepass` still carried a shader branch that nothing had called since chains
replaced the single-pass filter path. Nothing fails when only one of two copies is corrected.

---

## 5. Coexisting with other patches

Guard mechanics and dialogs are in [RESOLUTION.md](RESOLUTION.md) 2.8; the thprac and THRotator
audits are in [MOD_COMPATIBILITY.md](MOD_COMPATIBILITY.md).

### vpatch (VsyncPatch, swmpLV/75E)

Not a wrapper. `vpatch.exe` is a 46 KB stub that finds `*vpatch*.dll`; the per-game DLL launches
the game suspended and injects itself with `CreateRemoteThread` + `LoadLibrary`. Its imports are
`KERNEL32`, `USER32`, `SHLWAPI`: no `d3d9`, no `dinput8`. It patches game code in memory at
hardcoded addresses and calls Direct3D through the game's own device global.

It replaces the frame limiter and calls `Present` on its own schedule, as this patch does. Its
sites do not overlap ours (all 69 frozen signatures checked against all 16 of its TH12
addresses), so the identity check cannot notice, both installs report success, and the game runs
at whichever scheduler wrote last.

**Load order.** Injecting into a suspended process looks certain to arrive first. It does not:
the injected thread runs loader initialisation before its `LoadLibrary` call, and loader
initialisation loads this DLL because the game imports it. `WINEDEBUG=+loaddll` shows the game
process loading `DINPUT8.dll` and only then `vpatch_th12.dll`. When this patch installs, the game
is still clean, and an install-time-only guard finds nothing when launched through `vpatch.exe`.

Hence two checks: refuse at install (catches an executable already modified on disk), and warn on
the first frame, when it is too late to refuse. Detection is a loaded module named for a known
patch, plus the game's frame loop no longer being the code it shipped with, whoever patched it.

**Is vpatch still needed?** Not for these games. Its live features are a frame limiter, its own
Present scheduling, window geometry and an input fix. The first three are this patch's territory,
and vpatch does them with a whole-number frame cap and a stretched 640x480 window. It does not
change the render resolution. Three things it does that this patch does not:

- `BugFixGetDeviceState`: a foreground check on the DirectInput path, the fix for input running
  away after alt-tab. This patch calls ZUN's poll routine rather than replacing it, so it
  inherits stock behaviour.
- `BugFixTh12Shadow`: rev6-only, one render state; fixes UFO's Palanquin Ship shadow on Radeon
  and Intel.
- `ReplaySlowFPS`: slow-motion replay on Shift.

### OpenInputLagPatch (OILP)

The open replacement for vpatch: a `dinput8.dll` proxy plus `oilp_loader.exe`, with per-game
address tables for TH6–TH18. The source is public and was used as an address cross-reference for
the TH13 port. What it does:

- **Its own frame limiter in place of the game's**, with vpatch's central trick: wait *first*
  (waitable timer, then spin) until `BltPrepareTime` (2 ms by default) before the 60 Hz deadline,
  and only then let the game poll input, update, draw and present. In the stock game the poll
  happens at the start of the frame and the limiter then idles for up to ~14 ms, so the sampled
  input is a frame stale when shown. It forces the game's "fast" input-latency mode and hooks
  that mode's per-frame call.
- **D3D9Ex** with `SetMaximumFrameLatency(1)` and `D3DPRESENT_INTERVAL_IMMEDIATE`.
- **Replay speed control** on held keys (skip at 240 fps, slow-motion at 30 fps, both
  configurable), a fullscreen refresh-rate choice, and a frame-time overlay.
- `GameFPS` above 60 speeds the game up. There is no sub-stepping and no interpolation.
- No alt-tab/foreground input fix; that is vpatch's `BugFixGetDeviceState`.

**Relation to this patch.** The D3D9Ex part and the disabling of the game's limiter and latency
`Sleep` are identical to ours (same three per-frame call sites, `frame_calls`). The late-poll
limiter is unnecessary here. In our loop each present slot is poll → tick → draw → Present with
the only wait inside Present, and movement/focus are polled again on every minor tick
(`subtick_input_begin` calls the game's own DirectInput routine, merges the movement and focus
bits, and records them in the replay's `HFRI` chunk). At 360 Hz movement input is at most one
2.8 ms slot stale, against 16–30 ms stock. Applying OILP's reserve inside our slot would gain
1–2 ms at the cost of a spin and a risk of missed refreshes whenever tick+draw overruns the
reserve; that is less than the jitter of a keyboard's USB polling, so it is not done.

**Still frame-sampled, by design.** Shot, bomb and pause edges come from the game's own poll on
the boundary tick, once per 60 Hz frame. The game counts those in frames and the runner masks
them on minor ticks so a bomb cannot fire twice. That is the same sampling rate as stock+OILP,
with a shorter path to the screen. Processing a bomb on a minor tick would change game semantics
(the deathbomb window is frame-counted) and diverge from stock replays; if ever done, it must be
an option the replay records.

**Worth taking from this family**, in order: vpatch's foreground check on the DirectInput path,
then replay skip and slow-motion on held keys, which the tick-rate machinery makes nearly free
(§9).

**Coexistence.** OILP takes the same frame loop. The frame-loop guard treats it like vpatch, and
`openinputlagpatch` is on the known-module list. OILP normally installs *as* `dinput8.dll`, the
same file name as our proxy, so the two cannot share a folder; the module check only matters when
`oilp_loader.exe` loads it under its own name.

### d3d9 wrappers (PivotDX9 and friends)

Not a conflict. The patch installs, the game runs, the filters work. The wrapper takes the last
step, placing the image in the window, so the scaling modes and borderless fullscreen have no
effect: the geometry is computed against the game's back buffer and the wrapper then stretches
that however it likes.

This cannot be fixed from inside, so it is reported: three lines in the log, the two controls
greyed out in the menu with the reason above them, and a startup dialog only when a setting in
use depends on it. `video.warn_wrapper=0` silences it. The wrapper and vpatch notices share one
piece of dialog machinery and show at most one box a run. Detail: [RESOLUTION.md](RESOLUTION.md)
2.5.1 and 2.6.3.

### 5c. With a wrapper installed, `EndScene` is the compositor

A rotation wrapper such as THRotator replaces `d3d9.dll`, answers `GetBackBuffer` with a target
of its own so the game draws there, and in `EndScene` composes that target onto the real back
buffer (rotated, HUD rearranged) before the game calls `Present`. This patch does the same thing
to the same surface: whichever answers `GetBackBuffer` last is the one the game draws on, and the
other composes an empty surface.

`external_renderer` mode hands the picture over completely (`g_scaler_enabled = 0`) and keeps
only what is upstream of composition.

**The menu.** Three placements of the overlay draw were tried against a stand-in wrapper: inside
the Present hook, inside an `EndScene` hook, and with no scene at all. All three succeeded by
every return value (the right surface confirmed by pointer against the stand-in's own,
`SetRenderTarget` returning `D3D_OK`, `Clear` returning `D3D_OK`) and none appeared on screen.
With a wrapper installed, `device->EndScene` **is** the compositor: closing the scene that drew
the menu runs the wrapper's composition over it. If the patch's own `EndScene` hook is installed,
closing the scene also re-enters that hook and recurses.

**Fix.** A wrapper wraps the device but has no reason to wrap the swap chain, so
`GetSwapChain(0)->GetDevice()` returns the real device, whose `BeginScene`/`EndScene` compose
nothing. The overlay is drawn through that, from an `EndScene` hook, after the compositor has
run. When the chain returns the same object the patch hooked (a stand-in that patches a vtable
instead of wrapping), there is no safe way to close a scene, and the menu says so.

**Verification.** The stand-in reproduced the render-target substitution and the compositor but
not the object wrapping, so the menu path was unverified until THRotator 2.1.0 ran it: TH12 at
360 Hz, menu on the presented surface, surface re-acquired across every device reset, including
the rotation between 1280x960 and 960x1280.

**`after_device` runs again after every successful reset**, and a rotation wrapper resets
whenever it turns the picture. Anything that belongs there once a run must guard itself. The
wrapper notice was guarded by `show_notice` (at most one box per process), but its log line sat
outside the guard and claimed six warnings in one session. `show_notice` now reports whether it
spoke, and the line is written only then.

### 5d. Import slots: chain, and re-assert

thcrap's injector lets the Windows loader finish and stops the game's thread at the executable's
entry point, which is *after* this DLL has loaded and installed, because the game imports it. It
then walks the import table, matches by name, overwrites whatever it finds, and chains to
`GetProcAddress(dll, func)`: the library's own function, not the pointer it replaced. Its source
says so ("we can override any existing patches"). Every import this patch had hooked and thcrap
also detoured was dropped, `d3d9!Direct3DCreate9` among them, which is how the device is
obtained. Both patches reported success.

Rules:

- **Hook an import by chaining to whatever the slot held**, whoever put it there. Abstaining
  from a slot somebody else owns is wrong: it makes the order of arrival decide who works.
- **Re-assert the hooks once, later.** The trigger is `kernel32!QueryPerformanceCounter`: the
  game calls it before it asks for Direct3D (measured), no translation patch has a reason to
  take it, and it is readable while a DRM wrapper's stub is the only thing that has run, which
  is why the same entry point identifies a Steam copy. Whoever took the slot ends up nested
  inside this patch, and the log says when it happened.

**Trap.** Redirecting the *exporting library's export table* instead of the import slot works and
is wrong. An import slot belongs to the game; an export table belongs to the whole process.
Handing a foreign address to `d3d9!Direct3DCreate9` crashed Steam copies on the first frame, in
`ntdll`, because Steam's overlay reads that table too.

`tools/check_patch_overlap.py` compares thcrap's own game definitions against every byte this
patch writes and verifies. For all four games the two sets are disjoint, so the import table was
the whole conflict.

### 5e. `runner_ret`: the replaced function's `ret` is not ours

The replacement update runner is entered by a five-byte jump written over the game's own, so the
rest of the function, including its `ret`, belongs to this patch. In all four games that `ret` is
where thprac puts its practice-menu hook. A replacement that returns by itself removes the hook
silently: thprac's `GameGuiProgress` state machine makes its draw-side hook render nothing
without a matching update, so the menu never appears and neither log has an error.

The entry thunks are assembly and end by jumping to the game's own `ret`, leaving EAX, ESP and
the callee-saved registers as the game's epilogue does. TH10's runner takes its argument on the
stack and ends in `ret 4`; entering with the argument still there handles that. The address is
`addr.runner_ret`, read and jumped to but never written, and checked before use: a `ret`, a
`ret imm16`, or an `int3` (a breakpoint already sitting there is the case this exists for). It is
the `ret` that ends `runner_fn`; per-game values are in each game's devnotes, and the lookup
procedure is in [ADDING_A_GAME.md](../ADDING_A_GAME.md).

The catch-up pass in `limiter.c` calls `hfr_runner` directly rather than through the thunk: it is
an extra update with no frame drawn behind it, and a menu should not be told about it.

`tools/test_thprac_stub.c` verifies this in a running game. It reproduces thprac's hook mechanism
(one `0xCC`, a VEH at the front of the chain, a codecave holding the original instruction and a
jump back) and its progress state machine. Before the fix, on TH12 under Wine: update hook 0
hits, draw hook 476, every one with no frame open. After: the two fire in step in all four
games, in both installation orders.

## 6. Multi-game

### Profile states

Provisional (identified, no hooks at all), no simulation addresses (whole video path, stock
60 Hz), and fully described: see [ADDING_A_GAME.md](../ADDING_A_GAME.md), "Three states a game
can be in". In the second state `install()` logs what it skipped. For that state to work, the
per-frame housekeeping runs from the Present hook when there is no frame hook, so no video
feature depends on a gameplay address.

### What is shared and what is not

Shared, one implementation: the entire video path, the menu, the conflict guard, the crash
reporter, the scheduler, the replay machinery, and the speed-site installation (a table in the
profile; it used to be the same function written twice).

Not shared: `install_sites`. It is ~100 lines of hand-written x86 per game encoding that game's
quirks: item states, laser classes, fixed-point movement carry, player shot callbacks. This is
the real cost of a new game.

### Finding the per-game fields

Each was found for one game and then for the others by pattern.
[ADDING_A_GAME.md](../ADDING_A_GAME.md) has the procedures; the results and the checks that are
not there:

**The screenshot routine.** Find `snapshot/th%.3d.bmp` and its one reference, then disassemble
forward: the filename is built there and the routine is called a few dozen bytes later behind a
`lea eax,[esp+N]` guarded by `cmp esi,0x3e8`. Confirm by comparing instruction sequences (TH11's
was identical to TH12's, 61 instructions to the first `ret`) or, better, by checking that the
function calls `GetBackBuffer`, `LockRect`, `UnlockRect` and `Release` through the device vtable,
which is what settled TH10's. The stub must not disturb EAX (the filename) and on TH10 also not
ESI, which the caller sets immediately before.

| | screenshot_fn | screenshot_call |
| --- | --- | --- |
| TH10 | `0x420670` | `0x4392c1` |
| TH11 | `0x429ca0` | `0x446901` |
| TH12 | `0x42fca0` | `0x450891` |

**The conflict sites.** Collect every 32-bit immediate in `vpatch_thNN.dll` landing in the game's
code section, then keep those whose code has the right shape: `a1 xx xx xx 00 8b 08`
(`mov eax,[device]; mov ecx,[eax]`) is a Present call, and the frame limiter and replay timing
are `e8` calls to one shared timing routine (TH10 `0x439540`, TH11 `0x446920`, TH12 `0x4508b0`).
**Check the bytes are identical in the Japanese and English executables**: a per-language
difference would refuse to install for someone whose game is fine. Then verify by running the
game through `vpatch.exe` and watching the guard name the site.

Identification signatures need the same JP/EN check, and are deliberately *not* the conflict
sites. Identity is settled first from bytes no known patch touches, so a mismatch always means
"something else patched this game" and never "this is the wrong game".

---

## 7. TH10: a speed model that is not one float

The complete TH10 record is [TH10_DEVNOTES.md](games/TH10_DEVNOTES.md).

**Speed model.** TH11 and TH12 each write a literal `1.0` into one game-speed float at ~21 sites,
fifteen of which this patch redirects; sub-stepping multiplies that float by the sub-step
duration. TH10 has no single global: its top candidates take 12, 12 and 10 writes, spread across
per-object timers that carry a pointer to the shared speed at `+0x0c`. Its `install_sites` is
hand-written: twelve `SpeedSite` entries (the generalised `{addr,len,op,pop_float}` table), plus
movement-ftol, item-homing and Cartesian-integration hooks that TH11/TH12 do not need in that
form. The `SpeedSite` emitter and the runner's `critical_flag_mask` / `runner_return8_ends` knobs
keep all of that on the shared runtime.

**Critical section.** TH10 enters the game's critical section unconditionally
(`push 0x492274; call EnterCriticalSection`, no flag test); TH11/TH12 gate it behind
`misc_flags & 0x8000`. The runner handles this with `critical_flag_mask` (0 for TH10 means
"always"). The harness's bare fixture has no initialised section, so it initialises one
(`tools/test_runner.h`).

**The player-timer guard every game has.** The enemy hit test opens with "player state timer
unchanged since last frame → no damage", a stock double-hit guard. Sub-stepped, the integer timer
advances on the last minor tick of a frame, so on the boundary tick (the only tick the 60 Hz
enemy code runs the test) it always reads unchanged and no shot lands. The replacement test is
"the float timer advanced across the last Player update", which the runner tracks. Without it,
enemies are immune unless Player sub-stepping is off; TH10's port shipped that way once. It is
the same six-byte `cmp` in each game. Look for it first in any new game.

| | TH10 | TH11 | TH12 | TH13 |
| --- | --- | --- | --- | --- |
| hit-test guard | `0x42863e` | `0x434814` | `0x439ef2` | `0x446888` (in `0x446870`, see §7a) |

A gameplay system sub-stepped without the per-frame hooks that keep its own counters in whole
frames looks smoother and is wrong in hit detection.

**A fault at `0x42b1e0` that is not the patch.** TH10 aborts partway through init when a data
file is short, then dereferences a null in its own cleanup path; vanilla does the same. The rig
had a truncated `th10e.dat` and `thbgm.dat`. The registers-and-stack backtrace the exception
handler logs shows it: the faulting frame's return address is in the game's data-load cleanup,
not in any stub. With the real files TH10 boots, plays at a 240 Hz tick rate with zero repeated
frames, and records/replays HFR replays with per-tick input.

---

## 7a. TH13: porting TH12's hooks

The complete TH13 record, including object layouts and the porting tools, is
[TH13_DEVNOTES.md](games/TH13_DEVNOTES.md).

TH13 has one game-speed float (`0x4c0a28`) written at 16 sites, so the `SpeedSite` design applied
unchanged, and the video path came up on the first run.

**Engine changes absorbed by profile fields.** None needed a line in the shared runner beyond
reading the field.

| Change | Field |
| --- | --- |
| `UpdateFunc` grew a field; the callback argument moved from `+0x20` to `+0x24` | `layout.node_arg` |
| The runner keeps the next list node inside itself at `+0x50` and re-reads it after every callback and every removal (TH12 kept it in a register) | `layout.runner_next` |
| `remove_node` takes `(runner, node)` instead of `(node, runner)` | `remove_node_runner_first` |
| The critical section is gated on a byte flag at `0x4e49ed` rather than `misc_flags & 0x8000` | `critical_flag_mask = 0xff` on that byte |
| Enemy list head at `+0xb0` (`+0x68` before); it was the last enemy-interpolation constant hard-coded in shared code | `layout.enemy_list` |

**Replays live in `%APPDATA%`.** TH13, like every game from TH12.5 on, chdirs into
`%APPDATA%\ShanghaiAlice\th13\` around every save and load and keeps the string at `0x4dd0d1`.
Without this the extension chunk is appended to a file beside the executable that the game never
wrote. `addr.data_dir` names that string, and `replay_path()` prefers it when it is non-empty. It
is empty when `APPDATA` is unset, which is also when the game falls back to its own directory.

**The MotionState hook is not wanted.** TH12's `MotionState::step` adds a raw per-frame velocity,
so the runtime scales it by the sub-step. TH13 split the object into a pre-step (`0x4736a0`) that
recomputes the velocity from speed and angle *and multiplies by the game speed*, and a step
(`0x473780`) that adds it. The runtime already sets the game speed per tick, so scaling the step
as well moves every shot and bullet at `dt²`. Rule: before porting a "scale this increment" hook,
check whether the increment is now derived from something the speed float already scales.

**Addresses do not port by byte shape; structures do.** Thunks and small helpers matched across
the two binaries by normalised instruction sequence (`th13/match.py`). The sites that matter,
inside big update functions, mostly did not, and the decompiled-body similarity matcher
(`th13/dmatch.py`) only narrowed the function. What worked was reading what the TH12 hook means
(which object, which field, which timer gates it) and finding the same operation in TH13:

- The ECL variable getter (`0x420380`) gave the enemy layout in one table: position `+0x1230`, VM
  ids in a sub-object at `+0x11ec`, flags `+0x521c` with every bit two places higher than TH12's.
- The inline timer tick pattern (`mov edx,[+4]; mov [+0],edx; fld [ptr]; fcomp 0.99…`) marks
  every per-object timer.
- The `Timer::add` callers with a constant argument (`-14.0`, and the ANM `wait N`, inlined into
  `AnmVm::update` in TH13 where TH12 had a helper) are the ones to give stock `value * logical`
  semantics.

**The per-shot guard that only fails at 360 Hz.** TH13's enemy hit test (`0x446870`) has, besides
the player-timer guard (§7), a per-shot one: a shot counts only if *its own* countdown timer's
integer changed on the last tick and `int % interval == 0`. TH12's test is the inverse (it
*skips* on that tick), so TH11/TH12 never showed this. Sub-stepped, the shot's float ticks by
`dt` and its integer changes on whichever tick the float crosses a whole number, while the enemy
code runs on the boundary tick only. At 120 and 240 Hz `dt` is exact in float32, shots fired on
the boundary keep crossing on the boundary, and the rig passed. At 360 Hz `dt = 1/6` is not
exact, the crossing landed one tick off, and no shot ever hit.

Fix: a timer that exists for *frame-counted* decisions is ticked by the logical speed on the
boundary tick and not at all on minor ticks (`0x4436b4`), so it reads exactly as at 60 Hz at any
rate. Rule: any "integer timer changed" test that 60 Hz code makes against a sub-stepped object's
timer needs this; test at a rate whose `dt` is not a power of two.

**TH12 hooks with no TH13 counterpart.** A port's hook list can legitimately be half empty.

- The curve laser still computes "graze every 3 frames" but no longer acts on it, and the beam
  laser has no graze branch, so only the line laser is gated.
- The UFO attraction hook has nothing to attach to.
- The player's `state_timer % 60` block is gone, and the new every-3-frames block at `0x443792`
  is guarded by the game itself.

---

## 8. Verification, and traps in the rig

Test suites are described in [TESTING.md](TESTING.md).

### Techniques

- **The vectored exception handler**, logging module and offset. It is the only thing a tester
  can send back from a windowed crash. It identified both the screenshot routine and TH10's fault
  site.
- **The `IM_ASSERT` hook**, which disables the overlay and logs rather than taking the game down.
- **Comparing instruction sequences between games** to confirm a pattern-matched address.
- **Byte-comparing the patched image and the emitted stubs before and after a refactor.** For the
  speed-site refactor the stubs were identical and the images differed only in nine call
  displacements that all moved by the same `0x220`: our own functions shifting on rebuild.
- **A rate whose `dt` is not exactly representable.** 120 and 240 Hz give `dt` of 0.5 and 0.25;
  every float32 sum lands exactly and phase-alignment bugs stay hidden. 144 and 360 Hz give
  0.416667 and 0.166667, which round; the TH13 shot guard (§7a) only failed there. The rig can
  run any `fps=` value.
- **Checking the instrument.** When every log agrees on a pattern that no fix touches, time one
  known quantity three ways (a 2 ms kernel delay by QPC, interrupt time and `timeGetTime`) and
  see which clock disagrees (§4).
- **Counting inside the hook** for questions about what the game submits (§3a).
- **Measuring a claim.** A tester said the high frame rate was interpolated frames drawn twice.
  The stats line counts presents that happened with no logic tick behind them, which is what a
  duplicated frame is. TH11 in gameplay at a 240 Hz tick rate: 0 repeated frames out of 579, with
  230 ticks/s behind 116 presents/s. The number is in every log.

### Traps in the Wine/Xvfb rig

- **Wine substitutes its own `d3dx9_NN` unless told otherwise.** Super-xBR appeared to fail to
  compile on TH11 with `E5017: Aborting due to not yet implemented feature`, which reads like an
  old-compiler limitation. It is Wine's incomplete HLSL compiler. With the genuine DLL, every
  filter compiles on every real d3dx9 from 33 to 41. Force native with
  `WINEDLLOVERRIDES="d3dx9_37=n"` before believing any shader result. The genuine old-compiler
  failure (§3) has Microsoft's own error text, spelling mistake included.
- **The games black out on their own** about twenty seconds after being left at the title screen,
  and a screenshot taken then is black too. Vanilla with no patch loaded does the same; it is the
  idle demo under a software renderer. It was once mistaken for a screenshot bug.
- **A timed-out command leaves its state behind.** A resize test that appeared to show stretching
  instead of letterboxing had run vanilla, because an earlier command timed out before restoring
  the DLL it renamed. Check the patch is loaded before believing a test.
- **Check the data files.** Truncated game data makes TH10 fault in its own cleanup (§7).
- **The harness must not buffer its output.** Under Wine stdout is block-buffered into a pipe, so
  a run that hangs prints nothing. It is unbuffered now.

---

## 9. Open work, in order of value

1. **The alt-tab input fix** vpatch has and we do not: a foreground check on the DirectInput
   path. Cheap, and it affects all four supported games.
2. **`ReplaySlowFPS`**: slow-motion replay on a held key. The tick-rate machinery makes this
   nearly free.
3. **The `UI_*` settings live in four places**: the enum, both switches and the save function,
   with a `default:` that stops the compiler noticing an omission. A table of
   `{id, name, section, &cfg.field}` would collapse all four and the INI read, which is a fifth.
   Nothing is inconsistent today; it is a drift risk.
4. **`tools/embed_shaders.py` restates the pass-splitting rule** that `shader_parse.h` owns,
   because the build step is Python and the runtime is C. The checker tool includes the real
   header, so the two implementations that matter cannot disagree.
5. **thprac beyond the menu hook** (§5e). Its practice menu runs, but the rest of
   `docs/MOD_COMPATIBILITY.md`'s list is open: anything thprac counts in frames runs at the
   presentation rate, its FPS controls compete with this patch's pacing, its `ReplayClearParam`
   drops foreign replay chunks rather than preserving them, and its `io.DisplaySize` disagrees
   with itself after a device reset once `internal_scale > 1`.
6. **TH14 and beyond.** TH13's port (§7a) is the template: the speed-float pattern held, the
   engine changes were absorbed by profile fields, and the per-object hooks were found by meaning
   rather than by byte shape.
7. **New Classic's projectile slice asymmetry** (§10).

## 10. Sub-steps per 60 Hz frame at a rate that is not a multiple of 60

With sub-stepping on, the logic rate follows the display, so at 144 Hz the scheduler
(`timing.c`) runs 144 ticks a second against 60 Hz frames. 144/60 is 2.4, so the number of ticks
inside one 60 Hz frame cannot be constant. Measured over 200 frames:

| rate | ticks per 60 Hz frame | travel between consecutive collision tests |
|---|---|---|
| 144 | 2 or 3, period 5: `3 2 3 2 2` | 106 or 107 units of 256 -> 0.4141 / 0.4180 frames |
| 165 | 2 or 3, period 4: `3 3 3 2` | 93 or 94 -> 0.3633 / 0.3672 |
| 240 | 4, always | 64 -> 0.2500 |
| 360 | 6, always | 42 or 43 -> 0.1641 / 0.1680 |

- **A 2-tick frame does not loosen hit detection.** Tunnelling is bounded by the distance a
  bullet travels between two consecutive collision tests, and collision is tested once per tick.
  Ticks are evenly spaced in real time, so that distance is uniform: 0.414 to 0.418 of a game
  frame at 144 Hz, everywhere. The 2-or-3 split is where the 60 Hz boundaries fall among evenly
  spaced ticks.
- 360 Hz has the same one-unit dither in `dt` (42 or 43 of 256) while giving exactly six ticks a
  frame. The Bresenham step keeps the slices summing to exactly one frame, and it dithers whether
  or not the rate divides 60.
- The split decides where the *major* tick falls, the boundary tick on which the frame-locked
  systems run. Those run exactly sixty times a second at every rate.
- **Rejected alternative:** fix N sub-steps per 60 Hz frame and present whenever the panel is
  ready. That makes the count uniform and the *spacing* non-uniform, and a presented frame no
  longer coincides with a tick, so every sub-stepped object would need interpolating to draw.

### New Classic: the concern is live there

New Classic does not use `timing.c`.

**Trap.** `substep.h`'s nested Bresenham (steps of 0.5 and 0.332 frames at 144Hz) is not the live
New Classic loop: its only callers are in `tools/test_fixed.c`. Production uses
`fixed_clock_step()` for the frame clock and `subtick_slice()` for the slices, and
`subtick_slice` is not a Bresenham: it returns the wall-clock time elapsed since the last slice,
expressed in frames. Grep a function's callers before measuring it.

[FIXED_STEP_RESEARCH.md](FIXED_STEP_RESEARCH.md) models the live call order and reports a
projectile slice ranging from 1.389 ms to 12.5 ms at 144Hz, a twelfth of a frame to three
quarters of one. The asymmetry comes from `update_first()`: on a major tick it finishes the
outgoing frame with `projectiles_slice(ticks + 1.0)` so the 60Hz pass reads exactly the positions
the unmodified game would, then runs the native logic and moves the *player* to the new phase
with `subtick_move(ticks + phase)`, but does not advance the projectiles to that phase. The
projectiles' `moved_to` stays on the frame boundary while the phase has moved on, so the next
minor slice carries the extra.

That is deliberate for the player (the native pass must see stock positions) and looks unintended
for the projectiles. Open: a bounded investigation before any larger scheduling work. Check the
player/projectile collision order, the frame transition, native fast-forward and replay behaviour
before changing it.

## 11. Profile mistakes found on TH14, checked across TH10-13

Three kinds of profile mistake found while porting TH14 were checked against TH10-13, together
with two fields that turned out correct. Check all of them in any new profile.
([TH08_DEVNOTES](games/TH08_DEVNOTES.md) §10 records the same checks for TH08.)

State before the sweep; every "wrong" is now fixed in `src/games/`:

| what TH14 turned up | TH10 | TH11 | TH12 | TH13 |
| --- | --- | --- | --- | --- |
| a cycle rewind multiplied by the game speed | already fixed | already fixed | already fixed | already fixed |
| `native_size_cycle` claims an F10 the game does not have | correct (0) | **wrong** | **wrong** | **wrong** |
| only one of the loader's two play sites hooked | correct | **wrong** | **wrong** | **wrong** |
| the loader's header-reading site hooked as well | **wrong** | correct | correct | correct |
| replay magic | correct | correct | correct | correct |
| `data_dir` | correct (none) | correct | correct | correct |

### Discrete timer rewinds scaled by the game speed

`Timer::add` with a constant count is scaled by the timer's rate, so sub-stepped a fourteen-frame
rewind becomes a rewind of 14/6. Every game has it in the player's shot cycle. TH10-13 patch it
at two sites each:

```
TH10  0x428243 shot cycle (-15.0)   0x440e3d ANM wait
TH11  0x4343fc shot cycle (-14.0)   0x4355cd ANM wait
TH12  0x439ac2 shot cycle (-14.0)   0x43adbd ANM wait
TH13  0x44647c shot cycle (-14.0)   0x4629ef ANM wait
TH14  0x45101a shot cycle (14)      -- none --
```

TH14 was missing it, and its Reimu stopped firing. TH14 has no second site. Of the four
rate-aware rewinds in the executable, one is the shot cycle, two are unreferenced copies of the
helper (`0x4085b0` and `0x411530`, zero direct callers), and the fourth is inlined in the
dialogue text routine at `0x442d03`, which is frame-locked and therefore runs at a factor of one.
Method: list every reference to the `0.99f` guard constant that every rate-aware timer operation
uses. There are ninety, of which exactly four rewind.

### `native_size_cycle` (F10)

TH14's `native_size_cycle = 1` was inherited from TH13's profile and was untrue. Two checks:
nothing in the executable reads VK_F10, and the window procedure handles only `WM_SYSCOMMAND` and
swallows `SC_KEYMENU`. The same checks clear TH11, TH12 and TH13: none compares anything against
`0x79`, none indexes a key array at `0x79`, and all three window procedures have the same
`cmp esi, 0x112` / `sub eax, 0xf090` shape as TH14 and nothing else. F10 did nothing natively on
four of the five games, contrary to what the README said. TH10's flag was 0 for a different
reason: its own resolution dialog is stuck at 640x480. The flag is now 0 in all five profiles.

### The replay loader: play sites and peek sites

Every game dispatches a replay start on a mode of 1 or 2. The two branches are the same call with
the same real manager; mode 1 additionally stores that manager in a global. Both are play sites.
The loader's remaining call sites build a throwaway manager, write 2 into its `[+0x10]`, load a
file into it to read the header, and discard it: that is the menu building its list.

```
            play (mode 1)   play (mode 2)   peek
TH10        0x429257        0x42948c        0x429765
TH11        0x435a0d        0x435c2e        0x435f74
TH12        0x43b1d2        0x43b439        0x43b774
TH13        0x447c1b        0x447d41        0x448043, 0x4523de, 0x4524d6
TH14        0x4549bc        0x454b40        0x454fd3, 0x45ee0f
```

- **Hook both play sites.** TH11, TH12 and TH13 hooked mode 1 only, so a replay started the other
  way played back without its recorded rate, silently, at 60Hz. TH13's adapter comment said
  "(mode 1)". `replay_load_call` is now `replay_load_calls[2]`.
- **Never hook a peek site.** TH10 hooked all three sites. The load wrapper reads simulation
  metadata off the file and puts up a message box for anything it does not recognise, so opening
  a menu that lists replays does that once per file on disk. The same mistake took TH14 down when
  its extension was first installed. TH10 has one peek site rather than TH14's two, which is
  presumably why it went unnoticed.
