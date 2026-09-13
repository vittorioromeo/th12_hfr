# New Classic versus TH10–13: what is shared, what differs, what is missing

Touhou HFR supports two quite different games under one launcher. TH10, TH11, TH12 and TH13
are 32-bit Direct3D 9 games running one engine family; **Touhou Koumakyou: New Classic** is a
64-bit Direct3D 11 remaster on DxLib with an unrelated engine. This is the honest accounting
of what the patch does for each, written against the source as of v0.4.18-test.

The per-game records are [TH10_DEVNOTES.md](TH10_DEVNOTES.md),
[TH11_DEVNOTES.md](TH11_DEVNOTES.md), [DEVNOTES.md](DEVNOTES.md) (TH12),
[TH13_DEVNOTES.md](TH13_DEVNOTES.md) and [TH06NC_DEVNOTES.md](TH06NC_DEVNOTES.md); the shared
runtime is described in [DEVNOTES_RUNTIME.md](DEVNOTES_RUNTIME.md).

---

## 1. The short version

| | TH10–13 | New Classic |
| --- | --- | --- |
| State | supported | experimental |
| Architecture | x86, Direct3D 9(Ex) | AMD64, Direct3D 11 (DxLib) |
| Install | `dinput8.dll` proxy or launcher | launcher only (`touhou_hfr.exe` → `touhou_hfr64.exe`) |
| Simulation | runs at the tick rate, sub-stepped | 60 Hz, with selected systems sub-stepped |
| High-rate motion | player, bullets, items, lasers (not TH10), **both ANM managers** | player, enemy bullets, lasers (items deliberately 60 Hz, §22) |
| Sprite smoothing | interpolation for what is not sub-stepped | interpolation or prediction for everything else |
| Code patches | 65–101 verified sites per game | 27 frozen signatures, 16 patches |
| Video features | scaling, filters, sharpening, dimming, internal resolution | dimming only |
| Replays | extended format, per-tick input, recorded and played back | native format only, unextended |

---

## 2. What actually runs at the display rate

This is the part most worth getting right, because "high FPS gameplay" is easy to overstate.

**TH10–13.** The update list is run N times per displayed frame. Continuous quantities are
multiplied by the sub-step's duration; discrete blocks are skipped on ticks that are not frame
boundaries, using either "this is not a boundary tick" or "this object's timer did not change".
Which systems participate is a per-game table (`node_class`), and it is worth reading rather
than assuming — checked across all four games:

| Node class | TH10 | TH11 | TH12 | TH13 |
| --- | --- | --- | --- | --- |
| BulletManager, Player, ItemManager | sub | sub | sub | sub |
| LaserManager | **frame** | sub | sub | sub |
| Stage | **frame** | sub | sub | sub |
| **AnmManagerWorld, AnmManagerUI** | **sub** | **sub** | **sub** | **sub** |
| EnemyManager, Gui, Bomb, Spellcard, GameManager | frame | frame | frame | frame |

Two things fall out of that table. **Enemies are not sub-stepped in any of these games** — their
scripts and decisions stay at 60 Hz and their sprites are interpolated. And **both ANM managers
are sub-stepped in all four**, which is the mechanism that makes their menus, HUD and animated
UI smooth: the animation interpreter itself advances a fraction of a frame at a time, rather
than the rendering being interpolated after the fact.

**New Classic.** The simulation stays at 60 Hz and three things are lifted to the display rate:

- **Player movement and input** (`subtick=1`). Input is polled once per drawn frame instead of
  once per 60 Hz frame, and the player moves in slices that sum to exactly one frame. At 360 Hz
  that is six input samples a frame instead of one.
- **Enemy bullets** (`substep=1`). They advance in slices, and their culling, grazing, cancelling
  and collision run at every step.
- **Lasers** (`substep=1`). The beam extends in slices and its collision runs at every step.

Both of the ways the player can die — the bullet circle test at `0x6a980` and the laser
rotated-box test at `0x6aba0`, which write the same dying state at `player+0x7898` — are
therefore evaluated at the display rate. A projectile that would have jumped past the player
between two 60 Hz frames can now hit them. **This makes the game harder, not only smoother.**

**What is deliberately not sub-stepped in New Classic, and why.** Enemies, the player's shots,
items and effects. The reason is not difficulty but futility: their discrete effects are applied
once per 60 Hz frame and cannot safely be applied more often. The player's shots are boxes on the
player object, and the damage they deal is applied on the *enemy* side (`0x37a6c`,
`add [enemy+0x234], -0xa` per overlapping box) with nothing consuming the shot — running that
test per sub-step would multiply damage by the sub-step count. With damage fixed at 60 Hz and the
slices summing to exactly one frame, sub-stepping shot motion cannot change any outcome; it would
only move the drawn sprite, which interpolation already does. Enemies are the same argument plus
a cost: their motion is followed by an optional clamp to per-enemy bounds, so a slice pass would
have to reproduce that clamp or let bounded enemies overshoot and snap back once a frame.

So the gap against TH10–13 in *gameplay* terms is the player's shots and items, and closing it
would change nothing observable. The gap in *rendering* terms is bigger than it looks, and the
ANM row above is why: TH10–13 advance the animation interpreter itself between frames, so an
animation that scales, fades or changes frame is genuinely evaluated at the display rate. New
Classic interpolates the *result* of a 60 Hz animation step instead, which covers position,
rotation and scale but cannot cover a colour fade or an animation-frame change. **Sub-stepping
New Classic's ANM VM update (`0x69b0`) is the closest equivalent and the most promising
remaining rendering work** — see `TH06NC_DEVNOTES.md` §20.

---

## 3. Sprite smoothing

Both runtimes keep a per-VM pose history that snaps on a birth, a script change, a gap in ticks
or a teleport. New Classic hooks **three** VM draw entry points (`0x67f0`, `0x4dc0` and
`0x36c0`); the third is the one the screen manager uses, so menus, transitions and the title
screen are smoothed as well as the game — until v0.4.18 it was missed entirely and nothing on
those screens was ever smoothed (§20). New Classic's version additionally:

- **predicts instead of interpolating** when sub-tick player movement is on, so sprites line up
  with a player who is already ahead of the last native tick rather than lagging a frame behind it;
- **smooths rotation and scale as well as position** (VM `+0x9c/a0/a4` and `+0xe4/e8`), with
  rotation taking the short way round the wrap. This is what makes menus and HUD animations look
  smooth rather than stepped — most of them spin or grow rather than move;
- **invalidates automatically for sub-stepped objects**: a VM whose position changes within a
  native tick is drawn where it is rather than smoothed from a stale pose, so bullets do not get
  motion applied twice.

Not smoothed in either runtime: animation frames, colour fades, and 3D backgrounds.

---

## 4. Video, window and readability features

Everything in this section exists only for TH10–13. New Classic uses the game's own scaling and
its own window handling.

| Feature | TH10–13 | New Classic |
| --- | --- | --- |
| Window resizing, aspect snapping, borderless fullscreen | yes | no (game's own) |
| Output scaling: stretch, aspect fit, integer | yes | no |
| Upscaling filters (sharp-bilinear and others, `shaders/*.hlsl`) | yes | no |
| Sharpening post-process (CAS, unsharp mask) with strength slider | yes | no |
| Internal resolution (`internal_scale=2`/`3`) | yes | no |
| Dimming sliders for background, items, player shots, effects | yes | no |
| Texture upscaling | yes | no |
| D3D9Ex present queue / max frame latency / FlipEx | yes | n/a (D3D11) |
| VSync control | yes | yes (`[fixed60] vsync`) |
| Screenshot support | yes | no |
| Coexistence check against other patches (vpatch, OILP, wrappers) | yes | no |
| Cursor visibility in borderless fullscreen | yes | yes (shared code) |

Porting the scaler, filters and sharpening would mean a Direct3D 11 implementation of each; the
shader sources and the menu that drives them are already shared and would not need rewriting.

**Dimming is the exception, and it went the other way.** The x86 runtime fades at the Direct3D
level because its sprite manager batches quads into one draw call, so it has to flush the batch
around each classified object and fade the vertex colours. New Classic's runtime already wraps
every VM draw for interpolation, so it fades the VM's own colour before the draw and puts it
back after -- no D3D11 work at all, and about a tenth of the code. What it lacks instead is the
x86 rules' resolution: those match on ANM file, layer and script index, while this one matches
on the running draw callback, plus address arithmetic for the one class (items) that shares a
callback with something that must not fade. See [§22](TH06NC_DEVNOTES.md).

---

## 5. Input and replays

| | TH10–13 | New Classic |
| --- | --- | --- |
| Sub-tick input | yes, polled per tick, **recorded into the replay** | yes, polled per drawn frame, **not recorded** |
| Joystick handling | dedicated polling thread, `joyGetPosEx` hook | uses the game's own device poll |
| Replay format | extended (`t10r`–`t13r` plus sidecar metadata) | native, untouched |
| Replay playback with the patch | faithful; the recording's rate is reproduced | **not faithful if the features are on, and nothing disables them automatically** |

This is the one genuine functional gap rather than a cosmetic one. New Classic's native replay
stores one input word per 60 Hz frame, which cannot describe a player who moved from six input
samples, nor bullets whose collision was tested six times. There is also **no automatic guard**:
the byte previously believed to mean "a replay is playing" turned out to be set during ordinary play,
so it cannot mean playback in progress
([§17](TH06NC_DEVNOTES.md#17-why-neither-feature-had-ever-run-and-what-the-fps-readout-counts-2026-09-13)),
so both features must be switched off by hand before watching a replay. Closing this — a sidecar
carrying the sub-frame input stream, read back on playback — is the next piece of real work, and
it is what would make any of this usable for a scored run.

---

## 6. What code is shared, and what is new

The two runtimes are separate binaries built from one tree: `touhou_hfr.dll` (x86) and
`touhou_hfr64.dll` (AMD64). They cannot share object code, so "shared" means the same source
compiled into both.

**Shared source, compiled into both**

| File | What it is |
| --- | --- |
| `src/ui/menu.cpp` (524 lines) | the entire F11 menu: tabs, controls, help text, key handling |
| `src/ui/ui_api.h` | the settings enum both backends implement |
| `src/ui/menu_key.h` | menu-key edge handling |
| `src/core/patch.c` | the patch transaction: queue, preflight, all-or-nothing commit |
| `src/identity.h` / `src/launcher*.c` | the executable registry and launcher dispatch |
| `shaders/*.hlsl` | authored once (used only by the x86 backend today) |
| `third_party/imgui`, `third_party/minhook` | vendored |

**Written for New Classic, new in this tree**

| File | Lines | What it is |
| --- | --- | --- |
| `src/hfr64.c` | 575 | the whole x64 runtime: clock, patches, relays, sub-stepping, hooks |
| `src/backends/fixed_clock.h` | 13 | the fixed 60 Hz clock with wall-clock phase |
| `src/backends/fixed_history.h` | 77 | pose history, prediction, rotation and scale smoothing |
| `src/backends/subtick.h` | 57 | the τ slice accounting and the direction table |
| `src/backends/substep.h` | 61 | the exact dyadic sub-step schedule |
| `src/backends/fixed_game.h` | 51 | the profile: every RVA the runtime needs |
| `src/games/th06nc.c` | 123 | the profile's values, 27 frozen signatures, 7 guard ranges and the dimming map |
| `src/ui/overlay_fixed.c` | 65 | the x64 side of the settings API |
| `src/ui/menu_dx11.cpp`, `src/ui/overlay_dx11.cpp` | 78 | the D3D11 ImGui backend |
| `tools/porting/xrefs64.py` | 53 | AMD64 xrefs decoded from real function boundaries |

**Not used by the x64 build at all**: the whole of `src/core/` except `patch.c` — the scheduler,
speed sites, runner, replay, input, limiter, scaler, shaders, texture scaling, dimming, window
management, conflict detection and the D3D9 code, about 4,900 lines.

**Design shared as ideas rather than code.** The dyadic sub-step schedule, the "scale the
continuous, gate the discrete" patching pattern, the frozen-signature preflight, the
all-or-nothing patch transaction and the pose history are all the x86 runtime's designs ported
by hand. Two of them changed in the port:

- The **sub-step schedule** had to be rewritten. The x86 one lets a tick straddle a frame
  boundary, which is correct where the engine's own float timers accumulate the step; New
  Classic has integer timers and this runtime decides when the 60 Hz logic runs, so a straddling
  step would apply part of the next frame's motion before that frame's logic. The schedule now
  partitions exactly: one Bresenham deals a second's ticks to its 60 frames, another deals each
  frame's 256 units to that frame's ticks.
- **Sub-stepping is driven from outside the update list.** TH10–13 replace the game's update
  runner; New Classic calls the projectile callback directly on a sub-step pass, with five
  relocated blocks standing aside. A per-callback dispatch gate at the runner (`0x3beb0`) was
  designed for this and turned out not to be needed.

---

## 7. Verification, and how far it goes

| | TH10–13 | New Classic |
| --- | --- | --- |
| Signature/preflight tests | yes, per game | yes, 27 signatures, plus every dimming rule and pool |
| Scheduler tests | yes | yes, eight rates |
| Emitted machine code executed in Unicorn | yes, per game | yes, all 9 relays |
| Replay round-trip tests | yes | n/a |
| Menu rendered against a real device | yes (D3D9) | shares `menu.cpp`, tested via the x86 harness |
| Played by the owner | yes, extensively | yes, at 360 and 480 Hz |
| Native-versus-patched comparison over identical replays | not done | not done |

Neither runtime has had the comparison that would justify calling the sub-stepping
outcome-preserving. For New Classic that matters more, because the features deliberately change
outcomes.

---

## 8. Summary: what New Classic is missing

In rough order of how much it would be worth doing.

1. **Sub-tick replay recording.** The only gap that blocks real use. Everything else is optional.
2. **The video stack in Direct3D 11** — scaling, filters, sharpening, internal resolution,
   texture upscaling. The menu and the shaders already exist; the backend does not.
3. **Dimming.** Needs the draw-order attribution the x86 backend gets from the game's draw runner.
4. **Window management** — borderless, aspect snapping, integer scaling.
5. **Screenshots and coexistence checks.**
6. **Smoothing for animation frames and colour fades.** Position, rotation and scale are
   covered; a menu that fades rather than moves still steps at 60 Hz.
7. **Exact sub-stepped positions for enemies, shots and items**, in place of interpolated ones.
   Rendering only; it cannot change an outcome.
