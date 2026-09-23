# TH08 — Imperishable Night

TH08 belongs to the older engine family: Direct3D 8 (through a vendored d3d8to9), the TH06-era
`Chain` of update and draw callbacks, and a simulation written to run at one speed. It shares
the scheduler, the picture, the window and the menu with TH10–15. It differs in where the
extra frames come from. TH10–15 run the whole simulation faster; TH08 keeps it a 60 Hz
simulation (§4 gives the reason) and slices only the two parts where the rate shows: the
player's movement (`[hfr] subtick_input`, §6) and the bullets and lasers (`[hfr] substep`, §9), both
on by default. Everything else runs once a frame and is drawn at the display's rate (§5).
Replays carry the rate, the settings and the per-tick input (§10), as the later games' do.

Implementation: `src/games/th08.c`, `src/games/th08_signatures.h` (35 signatures;
`tools/th08_signatures.json`), `src/backends/d3d8.c`, `d3d8_bridge.cpp`,
`third_party/d3d8to9`.

## 1. Executable and references

TH08 1.00d, x86, image base `0x400000`, image size `0x14dc000`.
SHA256: `330fbdbf58a710829d65277b4f312cfbb38d5448b3df523e79350b879213d924`.

The image is about 21 MiB, mostly static object pools. The launcher mapper's bounded
allocation limit is 64 MiB for this reason (a 16 MiB limit rejects the image); signature
validation still applies. The test harness's fixture is 22 MiB for the same reason
(`tools/test_hfr.c`).

Engine reference: <https://github.com/GensokyoClub/th08> at
`e874b98e210b3be1ed21c5e03d54b883eac5069c`, whose expected executable hash matches. It names
about 2200 functions and decompiles the framework (`Chain`, `Supervisor`, `GameWindow`,
`AnmManager`, the menus) and almost none of the gameplay: `Player.cpp`, `BulletManager.cpp` and
`EnemyManager.cpp` are stubs. Everything from §4 on about the player, the bullets and the draw
callbacks is read from the binary (Ghidra 11.3.2 headless with `tools/FixFuncs.java` and
`tools/ExportAll.java`, then Capstone for exact bytes). No game executable, asset or
decompilation dump is distributed.

## 2. Test rig and the parity trace

Rig: Wine in a container with no GPU. `wine th08.exe` under `xvfb-run` with `openbox` running;
without a window manager the game window never gets keyboard focus and DirectInput reads
nothing. `xdotool keydown/keyup` drives it; ImageMagick's `import` reads the screen. llvmpipe
manages about 120–180 presentations a second.

- **The title screen's demonstration is a replay** and starts by itself after about 25 seconds
  of idling: 3100 frames of a real stage with no file and no input. It is deterministic from
  the stage's third frame (the first two differ run to run; something seeds before the stage
  does).
- **`[hfr] debug=1` with `replay_trace=1` writes `th08_trace.txt`**: one line per game frame,
  taken at the frame boundary, with the RNG's seed and count, the player, and over all 1536
  bullet slots the live count, a hash of the state words and the summed quantised positions,
  plus the live lasers. `replay_trace_from`/`_to` add one line per live bullet for a window of
  frames (`replay_trace=2`: also on the ticks between frames, §8). Two runs diff
  cleanly, and the first differing line is the frame where a change stops agreeing with the
  60 Hz game. Every "state is identical" claim below is that diff over the whole demonstration.
- Wine's HLSL compiler rejects all four pixel-art filters, so texture upscaling is not
  exercised on the rig (§12).

## 3. The frame, the Chain and the scheduler

| Address | Role |
| --- | --- |
| `0x441e70` | `GameWindow::Render`, thiscall; six-byte prologue trampoline |
| `0x441ecf` | Its 60 Hz time gate (`jp`), NOPed: pacing is the scheduler's |
| `0x441f4d` | `call Chain::RunCalcChain` (`0x43ca50`) → `th08_update` |
| `0x441f5a` | `call SoundPlayer::ProcessQueues` (`0x45d790`) → frame ticks only |
| `0x441fec` | `call Chain::RunDrawChain` (`0x43cb60`) → `th08_draw` |
| `0x44215b` | `call Supervisor::TakeSnapshot` (`0x44748f`) in `GameWindow::Present` |
| `0x462df2` | DrawInner's (`0x4628b0`) call to `AddSpriteToDrawBuffer` (`0x462f10`) |
| `0x44ba6a`, `0x44ba7c` | the two `fmul [multiplier]` of the player's movement |
| `0x164f548` | `g_Chain`: the calc chain's root element, the draw chain's at `+0x20` |
| `0x17ce758` | `g_Supervisor`; `+8` the device, `+0x188` (`0x17ce8e0`) the frame-rate multiplier |
| `0x43ef70` / `0x43efb0` | `Supervisor::Enter/LeaveCriticalSectionWrapper(this, 0)` |
| `0x43cf50` | `Chain::CutImpl(this, elem)` |
| `0x18b8a68` | the SoundPlayer object passed to `ProcessQueues` on a catch-up tick |
| `0x18bdc90` / `0x462e40` | AnmManager pointer / its sprite batch flush (used before the dim quad, §7) |
| `0x164d520` / `0x164d524` | RNG seed (u16) / RNG count (u32) |

`GameWindow::Render` is taken over by the **shared frame scheduler** (`core/frame.c`) as
TH10–15's frame function is: `patch_jmp(0x441e70, hfr_frame_ecx)`. `GameProfile` has three
callbacks for an engine outside the TH10–15 family: `frame_original` (call the game's frame
function once the entry is ours), `update_only` (the catch-up tick: an update pass with no
frame drawn behind it) and `replay_playing`. TH08 sets the first two and tracks replay playback
itself (the walker notes when callback `0x452550` runs). A private wall-clock pacer is not an
alternative: it has no catch-up and drifts with every dropped frame.

A chain element is `{i16 priority, u16 flags, callback, added, deleted, prev, next, unk,
arg}`, 0x20 bytes. Callbacks are `fastcall(arg)` and answer 0 remove me, 1 continue, 2 call
again, 3 break (the pass returns 1), 4 exit (0), 5 error (−1), 6 restart from the first
element (calc chain only).

`th08_walk` is `RunCalcChain`/`RunDrawChain` re-walked lock for lock, with three additions:

- A tick that is not a frame boundary only calls what is classified `MODE_SUB`.
- The element that answered *break* on the last frame tick (a pause, a menu) ends the minor
  ticks there too.
- The draw walk records whose callback is running.

`th08_classes` lists Player and BulletManager as `MODE_SUB`. That alone does not sub-step
them: the walker forces Player to a frame callback (only its movement is sliced, §6) and
decides BulletManager per tick from `th08_projectiles_sub()` (§9).

Update callbacks, from a `debug=1` census of one stage:

| Priority | Callback | | Priority | Callback | |
| --- | --- | --- | --- | --- | --- |
| 0 | `0x445453` | Supervisor (polls input) | 11 | `0x42c660` | EnemyManager |
| 1 | `0x402200` | AsciiManager | 12 | `0x418010` | Spellcard |
| 2 | `0x439bc7` | GameManager | 13 | `0x427bf0` | EffectManager |
| 3 | `0x45b160`, `0x45b800` | ScreenEffect | 14 | `0x431240` | BulletManager — also items (`0x440500`) and lasers |
| 4 | `0x467399` | Title | 15 | `0x4338ca` | Gui |
| 6 | `0x452550` | replay playback | 18 | `0x452490` | replay record |
| 8 | `0x407400` | Background | | | |
| 9 | `0x44c390` | Player | | | |

Draw callbacks (bold = "the playfield" in §5; `debug=1` logs each once as
`draw node: 0x%06x priority %d`):

| Priority | Callback | | Priority | Callback | |
| --- | --- | --- | --- | --- | --- |
| 0 | `0x445bd4` | | **11** | `0x42eb90` | **enemies** |
| 1 | `0x45bbf0` | | **12** | `0x427f00` | **effects** |
| 2 | `0x445d3e` | | **13** | `0x432b50` | **bullets, lasers and items** |
| 3 | `0x47087f` | title | 14 | `0x402430` | ascii |
| 5 | `0x43aa03` | GameManager | 15 | `0x418030` | spellcard |
| **6** | `0x409200` | **background** | 16 | `0x445bc0` | fps |
| **7** | `0x409640` | **background** | 17 | `0x433927` | gui |
| **8** | `0x42e120` | **enemies** | 20 | `0x4023d0` | ascii |
| **9** | `0x44d530` | **player (her sprite, options, shots)** | 21 | `0x45bb50` | screen effect |
| **10** | `0x44d630` | **player (her sprite, options, shots)** | | | |

## 4. The multiplier, and why the default is not TH10-style sub-stepping

`Supervisor+0x188` is the ancestor of TH10's game speed. `ZunTimer::Increment` accumulates it
into a sub-frame and keeps the previous integer, `AnmManager` scales rotation and scale growth
by it, and the player's movement is `position += speed * multiplier`. It looks as if the engine
can be sub-stepped as TH10–15 are, by setting the multiplier to a sixth and calling everything
six times. It cannot: with Player, EffectManager and BulletManager classified `MODE_SUB` at
120 Hz, the demonstration diverges by frame 367.

- **A bullet's motion is `position += velocity` with no multiplier** (`0x43149b`: the vector
  add `0x410a70`; the spawn states do the same with `velocity / 2`, `/ 2.5`, `/ 3`). The
  multiplier is baked into the velocity when it changes: the two effect callbacks that set it
  (`0x4251b0` to `1/n`, `0x425290` back to 1) walk all 1536 bullets rescaling them. Bullets
  sub-stepped naively fly N times too fast. The executable has 68 references to the
  multiplier; TH10 onward have hundreds.
- **The update is full of per-call counters**: the bullet and manager counters gated in §9
  (item 4), the player's focus-transition count at `+8`, and the human/youkai gauge, whose
  step is `ftol(n * multiplier)` and truncates to nothing at a sixth.

§9 handles these for the projectiles. §5 is the default because it gives the player's
responsiveness and a smooth picture with **no write to game state**.

### Object pools

| Object | Location | Fields |
| --- | --- | --- |
| `g_BulletManager` | `0xf54e90` | bullets at `+0x1a880`, lasers at `+0x660938`, per-call counters at `+0x6ba53c`/`+0x6ba54c` |
| Bullet | 1536 of `0x10b8` | VMs at `+0`, `+0x2a4`, `+0x548`, `+0x7ec`, `+0xa90`; position `+0xd44`, velocity `+0xd50`, speed `+0xd68`, angle `+0xd74`, timer `+0xd8c` (previous, sub-frame `+4`, current `+8`), off-screen grace `+0xda8`, flags `+0xdac`/`+0xdb0`, state word `+0xdb8` (0 free, 1 live, 2–4 spawning, 5 dying), off-screen count `+0xdba` (i16) |
| Laser | 256 of `0x59c` | live flag `+0x584`; timer `previous` `+0x588`, `current` `+0x590` |
| `g_ItemManager` | `0x1653648` | a linked list through `+0x2dc` from `+0x17b088`; the pool spans `0x17b088` bytes |
| Player shots | 128 of `0x484` | at `Player+0xbe838` |
| `g_Background` | `0x4e4030` | scroll position `+0x824`, camera block `+0x6394` (§5) |

A sprite VM is the first `0x2a4` bytes of its object: script pointer `+0x21c`, integer script
time `+0x40` (sub-frame at `+0x3c`), flags `+0x1f8` (blend mode in bits 4–5), colour `+0x1f0`,
position `+0x208`.

### Player (`0x17d5ef8`)

| Offset | Field |
| --- | --- |
| `[0]` (byte) | state; 1 or 2 = dying or re-entering (the game is moving her) |
| `+3` (byte) | focused, as the last frame tick left it |
| `+8` | focus-transition count (counts calls) |
| `+0x10` | her own VM |
| `+0x2b4` | position (`0x17d61ac`) |
| `+0x38c/+0x398`, `+0x3a4/+0x3b0`, `+0x3bc/+0x3c8` | the three hit boxes, min/max = position ∓ the half-sizes |
| `+0x3d4`, `+0x3e0`, `+0x3ec` | the three half-sizes |
| `+0x3f8` | velocity (`0x17d62f0`) |
| `+0x404` / `+0x408` | axis multipliers x / y |
| `+0x40c` | four options, `0x2f4` apart |
| `+0xfdc`, `+0xfe0` | when `+0xfdc` is non-zero, focus is bit 0 of `+0xfe0`; otherwise the input's focus bit (`4`) |
| `+0xbe834`, `+0xe2b24` | pointers to the two effects pinned to her: the focus ring and the hit-box marker |
| `+0xbe838` | shot pool |
| `+0xe2a74` | unfocused speed table pointer: straight `+0x24`, diagonal `+0x2c` |
| `+0xe2a78` | focused speed table pointer: straight `+0x28`, diagonal `+0x30` |

Related: `Player::OnUpdate` `0x44c390`; `HandlePlayerInputs` `0x44aec0`; movement bounds at
`0x164d2ec` (min x, min y, width, height); `0x160f534`, the callback's own early-out flag;
`g_ReplayCurFrameInput` `0x164d52c`; `Controller::GetInput` `0x43d970` (cdecl, returns the
button word, reads keyboard and pad, applies the key configuration, latches nothing). Input
bits: focus `4`, up `0x10`, down `0x20`, left `0x40`, right `0x80`.

## 5. Presentation: predicted playfield, interpolated interface

A 60 Hz simulation shown at 360 Hz can be smoothed two ways. **Interpolation shows the past**:
between the last two 60 Hz states, exact, up to a frame late. Applied to everything it draws
the player up to 16 ms behind where the stock game draws her, which is added input lag.
**Prediction shows the present**: the last state carried forward along its last step; never
late, wrong for one frame when something turns.

Settings: `[fixed60] interpolate=1` (smoothing as a whole) and `[fixed60] predict=1`, both
default; F11 → Timing → *Show the playfield in the present*. Log line:
`TH08 presentation: interpolate=%d predict=%d subtick=%d substep=%d`.

- **The playfield is predicted, the interface is interpolated.** Once the player is shown in
  the present, everything she can collide with has to be shown there too, or every bullet
  looks a frame further away than the collision test finds it. "Playfield" is the bold draw
  callbacks of §3. Menus, HUD and title need smoothness, not latency.
- **The player is predicted from the keys, not from her last step.** The frame tick that will
  move her has not sampled the input yet, but `Controller::GetInput` (`0x43d970`) can be
  called now. `position + velocity(keys now) × phase`, clamped to the playfield, is where that
  tick will put her if the keys stay down. During a replay the replay's word
  (`g_ReplayCurFrameInput`, `0x164d52c`) is used instead; it is the only input there is. Her
  own VM, her four options and the two effects pinned to her (§4, Player table) are shifted
  by that lead in the quad hook. Dying or re-entering (`Player[0]` of 1 or 2) and a paused
  playfield get no lead.
- **Cost at input edges**: a direction pressed or released part way through a frame moves the
  sprite by that fraction of a frame's travel at once (at most 4.5 px unfocused, 2 px
  focused). The picture stays a truthful one of the hit box.
- **Nothing in the game is written.** Position, collision, graze, RNG and replays are the
  stock game's: the demonstration's trace is identical line for line with prediction on, off,
  and with the patch's smoothing disabled entirely.

Measured prediction cost (`debug=1` logs `TH08 prediction: ...` and
`TH08 prediction by class ...` every five seconds): over the demonstration, 0.1–0.6 % of
bullet sprite-frames miss by more than 4 px for their one frame, and 2–7 % by more than 1 px,
nearly all of the latter in the first half second of a bullet's life, where its spawn
animation shrinks on an ease-out curve. Effects miss more (30 % over 1 px: particles with
random acceleration) and it does not matter.

### The quad hook

All 2D drawing (`DrawNoRotation`, the rotated draw, the text draw, everything in
`AnmManager`) goes through DrawInner and one call site, `0x462df2`. At the hook ECX is the
manager, the stack argument is the vertex array (four vertices of 28 bytes: x, y, z, rhw,
colour, u, v) and `[ebp+8]` is the VM. The relay copies the original argument, pushes the VM,
calls a fastcall helper (`th08_quad`) and returns with `ret 4`. The helper moves the temporary
vertices, calls the native batch copier and puts them back; no VM is written.

History is one entry per VM (8192 entries, hashed by VM address): the quad it drew on the last
two frame ticks.

- **A quad is posed as a rigid shape, not as four points.** Moving each corner along its own
  straight line is right for translation and wrong for a sprite that spins: interpolated along
  chords it shrinks between ticks, predicted along tangents it swells and snaps back sixty
  times a second, which on a screen of spinning bullets is a shimmer. The centre moves along a
  line; the corners turn about it by the angle the quad turned between the ticks (from the
  first corner's arm; the quad is rigid apart from scale) and grow by the ratio their arms
  grew. A turn over a radian is a flip or a swapped sprite, not a rotation, and falls back to
  straight lines.
- A gap in ticks, a script restart, the script time running backwards, or the centre moving
  64 px shows the quad where it is.
- **A VM that draws twice in one tick with different shapes is standing in for several
  sprites** (text does this per glyph) and is left alone for the next two seconds (120
  ticks). Without that stickiness its first draw of every tick is smoothed from its *last*
  draw of the tick before, which is a different glyph's position.

### The background

The stage background is 3D: its quads live in the world and the camera flies through them.
They reach `AddSpriteToDrawBuffer` through call sites of their own inside the background
renderer `0x40a1b0`, so the quad hook does not see them. The motion is in two places, the
scroll position at `Background+0x824` and the camera block at `+0x6394` (six vectors and a
field of view; `g_Background` is `0x4e4030`), and `0x40a1b0` reads both at draw time. They are
captured after `Background::OnUpdate` on every frame tick and, around the two background draw
callbacks only, replaced by this presentation's value and then restored. A component changing
by more than 64 is a cut and is shown as one.

## 6. Sub-tick player movement (`[hfr] subtick_input=1`, on by default)

`Player::OnUpdate` (`0x44c390`) stays a 60 Hz callback; only
the integration in `HandlePlayerInputs` (`0x44aec0`) is sliced. The two `fmul [multiplier]`
operands are pointed at `th08_move_factor` (the game's own multiplier times the tick's
length), and on the ticks between frame boundaries `th08_player_minor` repeats that
integration from the same fields:

- direction by the game's priority chain (the four diagonals, then down, up, left, right);
- speed from the focused or unfocused speed table, times the axis multipliers;
- clamp to the bounds at `0x164d2ec`;
- the three hit boxes rewritten as position ∓ the half-sizes (offsets in §4's Player table).

The scheduler leaves 60 Hz for this (`cfg.substep`), and live input is polled on the minor
ticks through `Controller::GetInput` (`0x43d970`), which reads the keyboard and the pad,
applies the key configuration and latches nothing.

This changes the game, deliberately: she is at `P + v·dt` when the frame tick tests the
bullets against her, and enemies that aim at her on the frame tick see where she is then. So
graze and hits land on other frames than in the stock game, and aimed patterns point a
fraction of a frame's movement elsewhere; with frame-sampled input and nothing else changed,
the demonstration's RNG parts from the 60 Hz run at frame 505. It is the same trade TH10–20
make, and it is replay-safe the same way: the file carries the rate, the settings and the
input of every tick between frames (§10). A stock replay plays at 60, where none of this
runs.

## 7. Other features

- **Dimming.** The quad hook has the VM and the vertex colours, so a sprite is classified by
  where its VM lives and its alpha scaled in place, with no batch flushes and no Direct3D
  hooks. Effects are everything EffectManager's callback draws bar what is pinned to the
  player; items are ItemManager's pool; player shots are the shot pool and the options. The
  engine's additive mode is `SRCALPHA/ONE`, so alpha fades it like any other sprite. The
  background is the shared black quad, blended over the (still playfield-sized) viewport
  after the second background callback. The profile's `draw.rules` are declarative only, so
  that the menu offers the sliders (priority 99 matches nothing; `world_prio = 8`).
- **Screenshots.** `TakeSnapshot` asks for the back buffer and locks it; the call is wrapped
  so the back-buffer hook hands over a lockable copy. "Home was just pressed" lasts a 60 Hz
  frame, which is several presentations, so only the presentation after a frame tick takes
  one.
- **The draw guard** hashes the RNG (`0x164d520`, 8 bytes) and the player's position and
  velocity (`0x17d61ac`, `0x17d62f0`, 12 bytes each) around the draw walk. It needs eight
  consecutive failures to trip: a stage loads on a thread of its own and draws random numbers
  while the loading screen is presented, so a single failure on a stage's first frame is
  normal (seen under Wine). Tripped, it logs `TH08 draw guard: drawing changed gameplay state
  on 8 consecutive frames; smoothing is off`.
- **The D3D8 bridge** (vendored d3d8to9) does not require `d3dx9_43.dll`: D3DX is wanted only
  for shader assembly, which these games do not use, and one surface-copy fallback, and the
  translation null-checks all three entry points. Any D3DX 9 is loaded if present.
- **Another patch holding `Direct3DCreate8`** is stepped over with a log line; the video path
  is not abandoned. TH10–13 make the same trade when they go through `Direct3DCreate9Ex`.
- **9Ex is opt-in** (`[fixed60] d3d9ex=1`): it works under Wine, with the device hooks
  converting the bridge's managed resources, and has not been played on Windows.
- **Texture upscaling**: TH08's statically linked D3DX8 creates every sheet with a full mip
  chain, which `texscale.c` treats as ineligible for D3D9 games; D3D8 games are exempt.
  Untested beyond that (§2).
- **The hidden window.** TH08's fullscreen start-up creates its window without `WS_VISIBLE`
  and relies on exclusive device creation to show it. The patch turns that presentation into
  a window, so after a successful D3D8-backed `CreateDevice` it shows a hidden window itself:
  once, at creation, never from the frame loop, so a later minimise is respected. Related:
  `patch_vtable` accepts another vtable with the same original implementation and refuses a
  different one. Returning as soon as one original pointer is recorded leaves the second
  Direct3D object's vtable unhooked.
- **The menu is recreated per device.** TH08's first start decides a restart-requiring option
  changed and rebuilds its window and device inside the process. `hook_CreateDevice` shuts
  the menu down first; otherwise Dear ImGui keeps drawing into the first device.

## 8. Debug tools for the parity work

All debug-only and behind environment variables, so a normal run pays nothing.

- `[hfr] replay_trace=1` keeps the display's rate for a replay that has none of its own (the
  demonstration, any stock replay), so sub-stepping stays on through it: that is how the
  demonstration is put through §9. A replay with a recorded rate plays at that rate, so a
  recording and its playback can be traced side by side.
- The trace marks the first frame of every stage (`stage N recording|playback`, and the frame
  count restarts) and its end (`end`), writes no line for a frame that did not run (a pause),
  and has, besides the bullets, a hash of the live lasers (position, angle, extent, width:
  `lh=`) and the live items' count and summed positions (`ni=`, `ix=`, `iy=`), and the input
  word (`in=`, which a recording and its playback hold a frame apart, so it is not compared).
- `TH08_TEST_LASTTICK=1` makes the player's kill-box, graze and laser tests
  (`0x44a230`, `0x44a470`, `0x44a6a0`) answer "nothing" on every tick but a frame's last, which
  is when a whole-frame move would have been tested. With collision pinned to where the stock
  game does it, sliced projectiles have to match the trace exactly. The spawn-state test
  `0x449ff0` is not hooked: it only raises a flag, which the frame tick's "animation finished"
  reads in the same call as the stock game does. Log line:
  `TH08 TEST: player collision confined to the last tick of each frame`.
- `TH08_TEST_INVINCIBLE=1` makes the same three tests, and the enemies' bodies
  (`Player::HandleCollisionWithPlayer`, `0x44a360`), answer "nothing" on every tick. A scripted
  run then lasts to the stage's end, and a recording played back at another rate has nothing
  left that finer slicing may change. (Returning from `Player::Die` instead does not work: the
  hit is taken again on every frame and the game falls over within seconds.) `=N` with N ≥ 2
  limits it to the stages below N, so a long run can still end in a game over, which is where
  the game offers to save the replay (quitting from the pause menu does not).
- `TH08_RNG_TRACE=1` hooks `Rng::GetRandomU16` (`0x43ecc0`) and counts, per game frame, who
  drew: the first return address up the frame-pointer chain that is not one of the RNG's own
  wrappers, and that caller's caller. "The RNG parted on frame 571" becomes "`SpawnItem`,
  called from `0x43183c`, ran 14 times in one run and 16 in the other", which names the
  hazard.
- `[hfr] replay_trace=2` dumps the bullets on the ticks between frames too.

## 9. Sub-stepped projectiles (`[hfr] substep=1`, on by default)

`BulletManager::OnUpdate` (items, 1536 bullets, 256 lasers) is called on every tick, with the
engine's multiplier set to the tick's length. The aim is New Classic's promise: at every frame
boundary each bullet is where, and what, the stock game would have it, with its motion in
between sliced and its collisions tested at every slice.

**Parity measured.** With `[hfr] subtick_input=0` and `TH08_TEST_LASTTICK` at 120 Hz, the
demonstration's bullets agree with the 60 Hz run (every slot's state word, every position to
0.02 px, across a bomb cancelling 600 of them) for 1698 frames, and the RNG to frame 571. At
571 two *spawning* bullets are caught mid-frame by the bomb's radius, which the stock game's
once-a-frame test misses. That is the difference the option exists to make, not an error. It
is also the end of what this replay can prove, because the patterns after it are random.

Re-measured with items stepped once a frame and the off-screen and cancelled-spawn changes
below: the RNG, the player and every bullet slot's state agree to frame 535, the item count to
492 -- the same bomb, now seen first in the items, which are exact at every frame instead of
being carried a fraction of a frame by the slices. Item positions agree to 1e-4 px.

A longer check: a whole game on Lunatic recorded at 60 Hz (`TH08_TEST_INVINCIBLE=5`, stages 1,
2, 3 and 4B, then a game over in 5; 120 000 frames, up to 1181 bullets) and played back with
the projectiles sliced at 120 Hz. Every stage starts from the replay's own stage data, so each
is a separate comparison. They agree for 500–2500 frames each and then part on differences of
rounding size (sums of positions off by one quarter-pixel, an item on the edge of the
collection radius collected a frame apart), after which the stage's randomness takes over.
Three causes found this way were real and are fixed: items integrated in slices (below), the
off-screen test made mid-frame (frame 1108 of stage 1), and items from a cancelled spawning
bullet placed where the first slice left it (frame 499 of stage 3). **Lasers**: stages 3 and
4B fire them on about 3500 frames; on 4B, where Marisa's lasers do not depend on the RNG, the
laser state (position, angle, extent and width, to a quarter-pixel) is identical to the 60 Hz
game's on 1716 of 3579 frames even after the run had parted. No laser difference was
isolated; this is weaker evidence than the bullets have.

Four things are required for that parity:

1. **Velocity is pre-multiplied.** `position += velocity`, no multiplier (§4). Around the
   call, every live bullet's velocity is multiplied by the tick's length and afterwards
   restored: exactly, from a saved copy, if the pass left it alone. Outside `OnUpdate` a
   velocity is always what the stock game would hold, so nothing else (the engine's own
   slow-motion rescale, a spell card rewriting directions) sees a sliced one.
2. **The frame a bullet goes live is a frame and a half long** (without this, frame 367: one
   bullet leaves the top of the screen a frame late). A spawning bullet (state 2, 3, 4) moves
   at `velocity/2`, `/2.5`, `/3`. On the call where its spawn animation ends, the stock game
   makes that fractional move and then falls through to the live state's full move, without
   ticking the bullet's timer (`+0xd8c`). Sliced, the fall-through is a tick's worth of each,
   leaving every bullet a fraction of a frame behind and half a frame out of phase for life.
   The animation ends on a frame tick, so `th08_projectiles_pass` gives the fractional move
   the rest of its frame and starts the timer's sub-frame at minus the rest of the frame.
3. **Behaviours must not be sliced** (without this, frame 526: a field of bullets drifts
   sideways by a third of a pixel). The live state's behaviours, `[0x431322, 0x43146f)`, are
   the scheduler `0x42ffc0` and nine flag-gated functions (speed curves
   `0x432210`/`0x432460`/`0x4325a0`/`0x4326e0`, acceleration `0x4322b0`, turning `0x432390`,
   bounce `0x432830`, the two wraps). They re-bake the velocity from speed, angle and the
   multiplier on every call. Stepped in fractions they integrate a changing speed over a
   finer grid and land somewhere else, which is more accurate and not the stock game. The
   block therefore runs on the frame tick only, **on the stock state**:
   `th08_behaviours_enter` puts the bullet's velocity and the multiplier back, the game's own
   code runs once, and `th08_behaviours_leave` slices the result. Velocity is then constant
   across a frame and the slices sum to the stock move. This also removes two hazards:
   `0x4322b0` adds `acceleration × multiplier` to an already-multiplied velocity, which is
   one factor short when sliced; and bounce tests a position threshold, which can fire twice
   when tested more often than the step that carried the bullet out, because Bresenham makes
   two ticks unequal.
4. **Counters that count calls.**
   - Gated to the frame tick with `gate_block`: the off-screen grace (`+0xda8`, site
     `0x43147b`) and the two manager counters (`+0x6ba53c` at `0x432112`, `+0x6ba54c` at
     `0x432137`).
   - The off-screen test itself (`0x4314b3`–`0x43159e`: out of bounds with no grace left is
     deleted, or counts the off-screen count `+0xdba` up or down) is made on the **last** tick
     of each frame only (`th08_bounds_now`), where the bullet stands where the stock game's
     whole-frame move put it when it tested. Made on every tick (as it was until a stage-1
     recording showed it on frame 1108), a bullet fired from just above the screen towards it
     is out of bounds after the first slice of its first frame and is deleted before it has
     come in; the stock game has it in bounds after the whole frame's move and keeps it.
   - The laser's graze test (`0x431f0c`) is `timer % 20 == 0`, true on *every* tick of such a
     frame. It is and-ed with "the timer's integer changed on the last tick" (`previous` at
     laser `+0x588`, `current` at `+0x590`).
   - A spawn or death animation's "finished" is only acted on at a frame tick:
     `ExecuteScript`'s (`0x45ea00`) result is zeroed on the other ticks at its four call
     sites (`0x4317f3`, `0x431904`, `0x431a16`, `0x431ad8`) and the frame tick asks again.
     A spawning bullet the player's bomb or field caught (`+0xdbe`) turns into items at its
     position when its animation ends, so for those the answer waits for the frame's *last*
     tick instead, where the bullet has made the whole frame's move as in the stock game.

Lasers need only the graze test; their growth already reads the multiplier. **Items are not
sliced.** `ItemManager::OnUpdate` does multiply by the multiplier at every use, but a falling
item is `velocity += gravity·m; position += velocity·m` and a collected one re-aims at the
player on every call: sliced, both integrate a changing velocity over a finer grid, and land a
fraction of a pixel elsewhere. In the demonstration that stayed within rounding (the items
there are mostly being collected); in a stage-1 recording the sums drifted on frame 490, and
which items are collected, and the score and power, would follow. So BulletManager's call to
it (`0x43127b`) runs on the frame tick only, on the stock multiplier (`th08_items`), and items
are drawn smoothed like the rest of the playfield. The trace has them (`ni`, `ix`, `iy`).

While the option is on, bullets and lasers are drawn where they are (their pools are
excluded from smoothing) and everything else in the playfield is predicted, as in §5. Testing
collisions more often changes what happens, like §6, and replays carry what is needed to
play that back (§10). All of these patches are inert at one tick per frame.

`tools/test_th08_stubs.py` runs every emitted stub under Unicorn: both paths of the five
gates with flags, registers and stack checked, the laser flag's four cases, the withheld
"finished" at all four call sites, and the behaviour block's skip, enter and leave.

## 10. Replays: the rate, the settings and the input

TH08's `.rpy` is the later games' shape: a header (magic `T8RP`, version 6) whose `+0xc` is the
size of the game's own data, then a trailing block of `USER` chunks that `LoadReplayData`
copies through untouched. `core/replay.c`'s chunks go on the end of it, as on TH10–20: `H`
(the rate as text), `HFRM` (simulation revision, the switches -- `fixed_substep`, flag 4, is
TH08's projectile switch -- the node mask and the logic rate) and `HFRI` (per stage, the
movement and focus bits of every tick between frames, run-length coded). The reader starts at
`+0xc`, so it only ever looks at the trailing block. Stage slots are 16 (`HFR_STAGES`): TH08
numbers its stages 0–8, and the chunk names the stage, so files written with eight slots read
back unchanged.

| Address | Role |
| --- | --- |
| `0x18b8a28` | `g_ReplayManager`: `+0x10` what it was registered for (0 record, 1 play), `+0x14` the file |
| `0x451f90` | `ReplayManager::RegisterChain(action, file)`, fastcall; called at the start of every stage |
| `0x43b3a7`, `0x43b50b` | its two call sites, play and record, in `GameManager::AddedCallback` → `th08_register_chain` |
| `0x4531f0` | `ReplayManager::SaveReplay(path, name)`, fastcall; `(NULL, NULL)` discards |
| `0x457471` | the result screen's save → `th08_replay_save`: the save, then the extension |
| `0x164d2cc` | the current stage |
| `0x164d0b4` | GameManager flags: bit 1 the demonstration, bit 2 "not paused" |
| `0x452310` / `0x452550` | the record (priority 17) and playback (6) nodes; the player is 9 |
| `0x452490` | fast-forward during playback (priority 18): "run the list again" on 2 frames of 3 while a skippable dialogue is up, 4 of 5 in some boss-less stretches |

- **Play or record.** The manager lives from a game's first stage to the save or the title.
  On the first `RegisterChain` of a game, a playback reads the file's extension (the
  demonstration is in `th08.dat`: it is a stock replay, 60 Hz), and a recording clears the
  session's per-stage streams. `replay_playing` is "the manager exists and was registered to
  play", which `replay_check` turns into the recorded settings and rate, as elsewhere. The
  replay menu's peek goes through `LoadReplayData` directly and never registers anything.
- **The first frame of a stage** is the first frame the player's callback runs on after a
  `RegisterChain`. It is the first system that is stepped, so nothing sliced has run on that
  frame yet: the rate is settled there (a playback may have begun on that very tick), the
  sub-step sequence restarts, and the stage's stream starts over. The record node itself runs
  after the player, which is why the replay node is not the marker here.
- **The stream** is one entry per tick between frames that reaches the player with the player
  sliced, whether she can move or not. Recording, the entry is the poll; playing, it is read
  back over the frame's own word. A stream that runs out, or a stock replay played sub-stepped
  (`replay_trace`), leaves the frame's word, which is the stock game's movement.
- **A pause does not shift the slicing.** A pause cuts the list before the player; the ticks
  go on. At a rate that is not a multiple of 60 the frames do not all have the same number of
  ticks, so after a pause the recording would slice its frames differently from the playback,
  which never paused, and the stream would be read into the wrong ticks. The scheduler's
  state after the last tick the player ran on is kept, and the first frame after a gap
  continues from it, as though the pause had taken no ticks (`th08_sched_resume`, logged). The
  same shift is possible on TH10–20 (§12).
- **Fast-forward.** "Run the list again" inside a tick a fraction of a frame long would give
  the stepped systems that fraction for a whole extra frame. With the ticks sliced the answer
  is counted instead, and after the presentation the extra frames are run as the ticks the
  schedule would have run for them (the rest of the frame in progress, then the next whole
  one), up to eight a presentation, so the sequence of ticks is the one a normal-speed
  playback goes through. At one tick per frame the stock restart is left alone.

**Measured under Wine** (the rig of §2, the trace of §8):

- **A recording and its playback agree on every frame**, comparing the RNG, the player, every
  bullet slot's state, the summed bullet and item positions and the lasers:
  - 144 Hz, stage 1 practice on Lunatic, played until the game over (4493 frames, up to 275
    bullets): moving in all eight directions, focused and not, a bomb, deaths and a pause.
    Played back at 144 Hz, and again on a 60 Hz display (the replay keeps its 144 ticks a
    second, the presentation catches up).
  - 120 Hz, the whole of stage 1 with `TH08_TEST_INVINCIBLE` (23769 frames, up to 1181
    bullets, items on 6824 frames): two pauses, and the boss dialogue, which the playback
    fast-forwards (`TH08 replay fast-forward` in the log). Played back at 120 Hz and on a 60 Hz
    display.
- **The replay menu** lists a file with the extension and plays it; the title screen's
  demonstrations play at 60 Hz as stock replays.
- 360 Hz, the same stage-1 practice until the game over (4168 frames, up to 419 bullets, a
  bomb, deaths and a pause), recorded and played back at 360 (the rig manages about 190
  ticks a second, so both ran slow, which the schedule does not care about).
- After the last changes to the bullets (off-screen test, cancelled spawns, items once a
  frame), 144 Hz again: identical on every frame (3197 frames, up to 362 bullets, a pause).

## 11. Profile checks against known cross-game mistakes

Three profile mistakes were copied from game to game before the TH14 work found them
([DEVNOTES_RUNTIME.md](../DEVNOTES_RUNTIME.md)). Each was checked for TH08.

- **`native_size_cycle`** is not set, which is right: F10 is the patch's on TH08, one
  `window: size cycle ->` line per press, confirmed in play.
- **The replay loader's play and peek call sites.** The loader itself (`LoadReplayData`) is not
  hooked; `RegisterChain`'s two call sites are, and only a registration to play reads the
  extension. The menu's peek allocates its own buffer, calls the loader and registers
  nothing, so it cannot start or end a playback (§10).
- **A discrete timer rewind scaled by the speed.** `ZunTimer::operator+=` (`0x41fdf0`) has one
  caller, in the ECL interpreter, which runs at 60 Hz. `operator--` (`0x418110`) is called
  four times in `BulletManager::OnUpdate`, once in each wrap behaviour and once in
  `ItemManager::OnUpdate`, all of them "one per call", which is a rate and is scaled correctly
  by the tick's length. The player's shot cycle is in `Player::OnUpdate`, which is never
  sliced. Nothing of that shape is reachable from a sub-stepped path.

## 12. Not done, and unverified on Windows

- thprac and vpatch for TH08: no conflict sites recorded, no overlap audit. The module-name
  check is all there is.
- `th08e.exe`, other versions, and the Steam release have not been seen.
- Not validated on Windows: texture upscaling, 9Ex, vsync pacing through the bridge,
  exclusive fullscreen, and how prediction feels at 240–480 Hz.
- Lasers under `substep=1` have not been through the parity trace (§9): no stage reachable on
  the rig fires one. Items have.
- Everything in §10 is measured under Wine only.
- The pause shift of §10 applies to the shared runner too (TH10–20): a pause at a rate that is
  not a multiple of 60 (144, 165 Hz) can leave a recording sliced differently from its
  playback after the pause. Not fixed there yet; TH18's and TH20's record-and-playback checks
  did not pause.
