# TH08 — Imperishable Night (experimental)

The first game in the runtime from the older engine family: Direct3D 8, the TH06-era `Chain`
of update and draw callbacks, and a simulation that was written to run at one speed. It is in
the x86 runtime and shares the scheduler, the picture, the window and the menu with TH10–14;
what it does not share is how the extra frames get their content. TH10–14 run the simulation
faster. TH08 draws a 60 Hz simulation at the display's rate, and §5 is why.

## 1. Executable and references

TH08 1.00d, x86, image base `0x400000`, image size `0x14dc000`.
SHA256: `330fbdbf58a710829d65277b4f312cfbb38d5448b3df523e79350b879213d924`.

The large image is mostly static object pools, not a large executable file. The launcher
mapper's former 16 MiB limit rejected this approximately 21 MiB image; the bounded allocation
limit is now 64 MiB and signature validation still applies. The test harness's fixture grew
for the same reason (`tools/test_hfr.c`, 22 MiB).

Engine reference: <https://github.com/GensokyoClub/th08> at
`e874b98e210b3be1ed21c5e03d54b883eac5069c`, whose expected executable hash matches. It names
about 2200 functions and decompiles the framework — `Chain`, `Supervisor`, `GameWindow`,
`AnmManager`, the menus — and almost none of the gameplay: `Player.cpp`, `BulletManager.cpp` and
`EnemyManager.cpp` are stubs. Everything in §4 onward about the player, the bullets and the
draw callbacks was read from the binary (Ghidra 11.3.2 headless with `tools/FixFuncs.java` and
`tools/ExportAll.java`, then Capstone for exact bytes). No game executable, asset or
decompilation dump is distributed here.

## 2. How it was worked on

Under Wine, in a container with no GPU: `wine th08.exe` under `xvfb-run` with `openbox`
running, because without a window manager the game window never gets keyboard focus and
DirectInput reads nothing. `xdotool keydown/keyup` drives it; ImageMagick's `import` reads the
screen. llvmpipe manages about 120–180 presentations a second, which is enough.

Two things from that setup are worth keeping:

- **The title screen's demonstration is a replay**, and it starts by itself after about 25
  seconds of idling. That is 3100 frames of a real stage with no file and no input needed, and
  it is deterministic from the stage's third frame (the first two differ run to run; something
  seeds before the stage does).
- **`[hfr] debug=1` with `replay_trace=1` writes `th08_trace.txt`**: one line per game frame,
  taken at the frame boundary — the RNG's seed and count, the player, and over all 1536 bullet
  slots the live count, a hash of the state words and the summed quantised positions, plus the
  live lasers. `replay_trace_from`/`_to` add one line per live bullet for a window of frames.
  Two runs diff cleanly, and the first differing line is the frame a change stopped agreeing
  with the 60 Hz game. Every claim below of the form "state is identical" is that diff over
  the whole demonstration.

Wine's HLSL compiler rejects all four pixel-art filters, so texture upscaling could not be
exercised there. It has to be checked on Windows.

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

`GameWindow::Render` is taken over by the **shared frame scheduler** (`core/frame.c`), exactly
as TH10–14's frame function is: `patch_jmp(0x441e70, hfr_frame_ecx)`. Three profile callbacks
were added so an engine outside the TH10–14 family can use it — `frame_original` (call the
game's frame function once the entry is ours), `update_only` (the catch-up tick: an update
pass with no frame drawn behind it) and `replay_playing`. The earlier prototype paced itself
from a wall clock of its own; it had no catch-up and drifted with every dropped frame.

A chain element is `{i16 priority, u16 flags, callback, added, deleted, prev, next, unk,
arg}`, 0x20 bytes; callbacks are `fastcall(arg)` and answer 0 remove me, 1 continue, 2 call
again, 3 break (the pass returns 1), 4 exit (0), 5 error (−1), 6 restart from the first
element (calc chain only). `th08_walk` is `RunCalcChain`/`RunDrawChain` re-walked lock for
lock, with three additions: a tick that is not a frame boundary only calls what is classified
`MODE_SUB`; the element that answered *break* on the last frame tick (a pause, a menu) ends
the minor ticks there too; and the draw walk records whose callback is running.

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

Draw callbacks: 0 `0x445bd4`, 1 `0x45bbf0`, 2 `0x445d3e`, 3 `0x47087f` title, 5 `0x43aa03`
GameManager, **6 `0x409200` and 7 `0x409640` background, 8 `0x42e120` and 11 `0x42eb90`
enemies, 9 `0x44d530` and 10 `0x44d630` player (her sprite, options, shots), 12 `0x427f00`
effects, 13 `0x432b50` bullets, lasers and items**, 14 `0x402430` ascii, 15 `0x418030`
spellcard, 16 `0x445bc0` fps, 17 `0x433927` gui, 20 `0x4023d0` ascii, 21 `0x45bb50` screen
effect. The bold ones are "the playfield" in §5.

## 4. The multiplier, and why this is not TH10

`Supervisor+0x188` is the ancestor of TH10's game speed. `ZunTimer::Increment` accumulates it
into a sub-frame and keeps the previous integer, `AnmManager` scales rotation and scale growth
by it, and the player's movement is `position += speed * multiplier`. It reads like an engine
that can be sub-stepped by setting the multiplier to a sixth and calling everything six times,
which is how TH10–14 work.

It cannot, yet, and the demonstration says so by frame 367. With Player, EffectManager and
BulletManager classified `MODE_SUB` at 120 Hz:

- **A bullet's motion is `position += velocity` with no multiplier** (`0x43149b`: the vector
  add `0x410a70`; the spawn states do the same with `velocity / 2`, `/ 2.5`, `/ 3`). The
  multiplier is baked into the velocity when it changes — the two effect callbacks that set it
  (`0x4251b0` to `1/n`, `0x425290` back to 1) walk all 1536 bullets rescaling them — so bullets
  sub-stepped naively fly N times too fast. 68 references to the multiplier in the whole
  executable; TH10 onward have hundreds.
- **The update is full of per-call counters**: the off-screen grace at bullet `+0xda8`, the
  off-screen counter at `+0xdba`, two manager-level counters at `+0x6ba53c`/`+0x6ba54c`, the
  player's focus-transition count at `+8`, and the human/youkai gauge, whose step is
  `ftol(n * multiplier)` and truncates to nothing at a sixth.

None of that is unreachable, and §9 does it for the projectiles. But it is not where the value
is: what follows gets the player's responsiveness and a smooth picture with **no write to game
state at all**, which is why it is the default and §9 is an option.

Bullet layout, for whoever does it: `g_BulletManager` `0xf54e90`, 1536 bullets of `0x10b8` at
`+0x1a880`; VMs at `+0`, `+0x2a4`, `+0x548`, `+0x7ec`, `+0xa90`; position `+0xd44`, velocity
`+0xd50`, speed `+0xd68`, angle `+0xd74`, timer `+0xd8c`, flags `+0xdac`/`+0xdb0`, state word
`+0xdb8` (1 live, 2–4 spawning, 5 dying). 256 lasers of `0x59c` at `+0x660938`, live flag
`+0x584`. Items: `g_ItemManager` `0x1653648`, a linked list through `+0x2dc` from
`+0x17b088`. Player shots: 128 of `0x484` at `Player+0xbe838`.

## 5. Past or present

Smoothing a 60 Hz simulation at 360 Hz can show either of two things.

**Interpolation shows the past**: between the last two 60 Hz states, exact, up to a frame
late. The first prototype interpolated everything, and that made the game *worse* than stock
in the one way a shooter cannot afford: the player was drawn up to 16 ms behind where the
stock game would have drawn her. High refresh with added input lag is not the product.

**Prediction shows the present**: the last state carried forward along its last step. Never
late; wrong for one frame when something turns.

So (`[fixed60] predict=1`, the default; F11 → Timing → *Show the playfield in the present*):

- **The playfield is predicted**, the interface is interpolated. Once the player is shown in
  the present, everything she can collide with has to be shown there too, or every bullet
  looks a frame further away than the collision test will find it. "Playfield" is the bold
  draw callbacks of §3; the rest — menus, HUD, title — is where smoothness matters and
  latency does not.
- **The player is predicted from the keys, not from her last step.** The frame tick that will
  move her has not sampled the input yet, but the input can be read now
  (`Controller::GetInput`, `0x43d970`: cdecl, returns the button word, reads keyboard and pad,
  applies the key configuration, latches nothing), and `position + velocity(keys now) × phase`,
  clamped to the playfield, is where that tick will put her if they are still down. During a
  replay the replay's word (`g_ReplayCurFrameInput`, `0x164d52c`) is used instead; it is the
  only input there is. Her own VM (`Player+0x10`), her four options (`+0x40c`, `0x2f4` apart)
  and the two effects pinned to her (`+0xbe834` the focus ring, `+0xe2b24`) are shifted by that
  lead in the quad hook. Dying or re-entering (`Player[0]` of 1 or 2) and a paused playfield
  get no lead.

  The cost is at input edges: a direction pressed or released part way through a frame moves
  the sprite by that fraction of a frame's travel at once (at most 4.5 px unfocused, 2 px
  focused), because the prediction is always "where the next tick puts her if the keys stay
  as they are". That is a truthful picture of the hit box, which is what it is for.
- **Nothing in the game is written.** Position, collision, graze, RNG and replays are the
  stock game's: the trace of the demonstration is identical line for line with prediction on,
  off, and with the patch's smoothing disabled entirely.

Prediction's visible cost was measured rather than assumed (`debug=1` logs it every five
seconds): over the demonstration, 0.1–0.6 % of bullet sprite-frames miss by more than 4 px for
their one frame, and 2–7 % by more than 1 px — nearly all of the latter in the first half
second of a bullet's life, where its spawn animation shrinks on an ease-out curve. Effects
miss more (30 % over 1 px: particles with random acceleration) and do not matter.

### The quad hook

All 2D drawing — `DrawNoRotation`, the rotated draw, the text draw, everything in
`AnmManager` — funnels through DrawInner and one call site, `0x462df2`. At the hook ECX is the
manager, the stack argument is the vertex array (four vertices of 28 bytes: x, y, z, rhw,
colour, u, v) and `[ebp+8]` is the VM. The relay copies the original argument, pushes the VM,
calls a fastcall helper and returns with `ret 4`. The helper moves the temporary vertices,
calls the native batch copier and puts them back; no VM is written. The VM's script pointer is
at `+0x21c`, its integer script time at `+0x40`, its flags at `+0x1f8` (blend mode in bits
4–5), colour at `+0x1f0`, position at `+0x208`.

History is one entry per VM: the quad it drew on the last two frame ticks.

- **A quad is posed as a rigid shape, not as four points.** The prototype moved each corner
  along its own straight line. That is right for translation and wrong for a sprite that
  spins: interpolated along chords it shrinks between ticks, predicted along tangents it
  swells and snaps back sixty times a second, which on a screen of spinning bullets is a
  shimmer. Now the centre moves along a line and the corners turn about it by the angle the
  quad turned between the ticks (from the first corner's arm; the quad is rigid apart from
  scale) and grow by the ratio their arms grew. A turn over a radian is a flip or a swapped
  sprite, not a rotation, and falls back to straight lines.
- A gap in ticks, a script restart, the script time running backwards, or the centre moving
  64 px shows the quad where it is.
- **A VM that draws twice in one tick with different shapes is standing in for several
  sprites** (text does this per glyph) and is left alone for the next two seconds. Without
  that stickiness its first draw of every tick was smoothed from its *last* draw of the tick
  before — a different glyph's position.

### The background

The stage background is 3D: its quads live in the world and the camera flies through them
(they reach `AddSpriteToDrawBuffer` through call sites of their own, inside the background
renderer `0x40a1b0`). The motion is all in two places — the scroll position at
`Background+0x824` and the camera block at `+0x6394` (six vectors and a field of view;
`g_Background` is `0x4e4030`) — and `0x40a1b0` reads both at draw time. They are captured after
`Background::OnUpdate` on every frame tick and, around the two background draw callbacks only,
replaced by this presentation's value and then restored. A component changing by more than 64
is a cut and is shown as one.

## 6. Sub-tick player movement (`[fixed60] subtick=1`, off by default)

New Classic's option, for parity. `Player::OnUpdate` (`0x44c390`) stays a 60 Hz callback; only
the integration in `HandlePlayerInputs` (`0x44aec0`) is sliced. The two `fmul [multiplier]`
operands are pointed at `th08_move_factor` — the game's own multiplier times the tick's length
— and on the ticks between frame boundaries `th08_player_minor` repeats that integration from
the same fields: direction by the game's priority chain (the four diagonals, then down, up,
left, right), the speed table at `Player+0xe2a74` (unfocused: straight `+0x24`, diagonal
`+0x2c`) or `+0xe2a78` (focused: `+0x28`, `+0x30`), the axis multipliers `+0x404`/`+0x408`, the
bounds at `0x164d2ec`, and the three hit boxes (`+0x38c/+0x398`, `+0x3a4/+0x3b0`,
`+0x3bc/+0x3c8` = position ∓ the half-sizes at `+0x3d4`, `+0x3e0`, `+0x3ec`). The scheduler
leaves 60 Hz for this (`cfg.substep`), and live input is polled on the minor ticks.

It is off by default because it **is not replay-safe and cannot be made so without a rate
stamp in the file**: slicing puts her at `P + v·dt` rather than `P + v` when the frame tick
tests the bullets against her, so graze and hits land on different frames. Measured: with
frame-sampled input and nothing else changed, the demonstration's RNG parts from the 60 Hz run
at frame 505. The slices are therefore switched off while a replay plays. Prediction (§5) gives
the same picture with none of this, which is why it is the default and this is the option.

## 7. The rest of the feature set

- **Dimming.** The quad hook has the VM and the vertex colours, so a sprite is classified by
  where its VM lives and its alpha scaled in place — no batch flushes, no Direct3D hooks.
  Effects are everything EffectManager's callback draws bar what is pinned to the player; items
  are ItemManager's pool; player shots are the shot pool and the options. The engine's additive
  mode is `SRCALPHA/ONE`, so alpha fades it like any other sprite. The background is the shared
  black quad, blended over the (still playfield-sized) viewport after the second background
  callback. The profile's `draw.rules` are declarative only, so the menu offers the sliders.
- **Screenshots.** `TakeSnapshot` asks for the back buffer and locks it; the call is wrapped so
  the back-buffer hook hands over a lockable copy. "Home was just pressed" lasts a 60 Hz frame,
  which is now several presentations, so only the presentation after a frame tick takes one.
- **The draw guard** hashes the RNG and the player's position and velocity around the draw
  walk. It used to fail on the first frame of every stage under Wine: a stage loads on a
  thread of its own and draws random numbers while the loading screen is being presented. It
  now needs eight consecutive failures.
- **The D3D8 bridge** (vendored d3d8to9) no longer insists on `d3dx9_43.dll`: D3DX is wanted
  only for shader assembly, which these games do not use, and one surface-copy fallback, and
  the translation null-checks all three entry points. Any D3DX 9 is loaded if present.
- **Another patch holding `Direct3DCreate8`** is stepped over with a log line rather than
  answered by giving up the whole video path. It is the same trade TH10–13 make when they go
  through `Direct3DCreate9Ex`.
- **9Ex is opt-in** (`[fixed60] d3d9ex=1`): it works under Wine, with the device hooks
  converting the bridge's managed resources, and has not been played on Windows.
- **Texture upscaling**: TH08's statically linked D3DX8 creates every sheet with a full mip
  chain, which `texscale.c` used to treat as ineligible; D3D8 games are now exempt. Untested
  beyond that (§2).
- **The hidden window.** TH08's fullscreen start-up creates its window without `WS_VISIBLE`
  and relies on exclusive device creation to show it. This patch turns that presentation into a
  window, so after a successful D3D8-backed `CreateDevice` it shows a hidden window itself —
  once, at creation, never from the frame loop, so a later minimise is respected. The same
  recreation exposed the vtable hook: `patch_vtable` returned as soon as an original pointer
  was recorded, leaving the second Direct3D object's vtable unhooked; it now accepts another
  vtable with the same original implementation and refuses a different one.
- **The menu recreated per device** (2026-09-20): TH08's first start decides a
  restart-requiring option changed and rebuilds its window and device inside the process;
  Dear ImGui went on drawing into the first device. `hook_CreateDevice` now shuts the menu down
  first.

## 8. Tools that made §9 possible

All debug-only, all behind environment variables so they cost a normal run nothing, and all
worth keeping for the next system:

- `TH08_FORCE_SUB=1` keeps sub-stepping on through a replay. Sub-stepping normally switches
  itself off while one plays (§6); the demonstration is a replay, and it is the test.
- `TH08_TEST_LASTTICK=1` makes the player's kill-box, graze and laser tests
  (`0x44a230`, `0x44a470`, `0x44a6a0`) answer "nothing" on every tick but a frame's last — when
  a whole-frame move would have been tested. With collision pinned to where the stock game
  does it, sliced projectiles have no excuse: the trace has to match.
- `TH08_RNG_TRACE=1` hooks `Rng::GetRandomU16` (`0x43ecc0`) and counts, per game frame, who
  drew — the first return address up the frame-pointer chain that is not one of the RNG's own
  wrappers, and that caller's caller. "The RNG parted on frame 571" becomes "`SpawnItem`, called
  from `0x43183c`, ran 14 times in one run and 16 in the other", which names the hazard.
- `[hfr] replay_trace=2` dumps the bullets on the ticks between frames too.

## 9. Sub-stepped projectiles (`[fixed60] substep=1`, off by default, experimental)

`BulletManager::OnUpdate` — items, 1536 bullets, 256 lasers — called on every tick, with the
engine's multiplier set to the tick's length. The aim is New Classic's promise: at every frame
boundary each bullet is where, and what, the stock game would have it, with its motion in
between sliced and its collisions tested at every slice.

**What the trace holds it to.** With `TH08_FORCE_SUB` and `TH08_TEST_LASTTICK` at 120 Hz, the
demonstration's bullets agree with the 60 Hz run — every slot's state word, every position to
0.02 px, across a bomb cancelling 600 of them — for 1698 frames, and the RNG to frame 571,
where two *spawning* bullets are caught by the bomb's radius mid-frame that the stock game's
once-a-frame test misses. That is the difference this option exists to make, not an error; it
is also the end of what this replay can prove, because the patterns after it are random.
Getting there took four findings, in the order the trace produced them:

1. **Velocity is pre-multiplied.** `position += velocity`, no multiplier (§4). Around the call,
   every live bullet's velocity is multiplied by the tick's length and afterwards restored —
   exactly, from a saved copy, if the pass left it alone. Outside `OnUpdate` a velocity is
   always what the stock game would hold, so nothing else (the engine's own slow-motion
   rescale, a spell card rewriting directions) ever sees a sliced one.
2. **The frame a bullet goes live is a frame and a half long** (frame 367: one bullet left the
   top of the screen a frame late). A spawning bullet (state 2, 3, 4) moves at `velocity/2`,
   `/2.5`, `/3`; on the call where its spawn animation ends, the stock game makes that
   fractional move and then falls through to the live state's full move, without ticking the
   bullet's timer (`+0xd8c`). Sliced, the fall-through was a tick's worth of each, leaving
   every bullet a fraction of a frame behind and half a frame out of phase for life. The
   animation ends on a frame tick, so the pass gives the fractional move the rest of its frame
   and starts the timer's sub-frame at minus the rest of the frame.
3. **Behaviours must not be sliced** (frame 526: a field of bullets drifting sideways by a third
   of a pixel). The live state's behaviours — `[0x431322, 0x43146f)`: the scheduler `0x42ffc0`
   and nine flag-gated functions (speed curves `0x432210`/`0x432460`/`0x4325a0`/`0x4326e0`,
   acceleration `0x4322b0`, turning `0x432390`, bounce `0x432830`, the two wraps) — re-bake the
   velocity from speed, angle and the multiplier on every call. Stepped in fractions they
   integrate a changing speed over a finer grid and land somewhere else, which is more
   accurate and not the stock game. So the block runs on the frame tick only, **on the stock
   state**: `th08_behaviours_enter` puts the bullet's velocity and the multiplier back, the
   game's own code runs once exactly as it always did, and `th08_behaviours_leave` slices the
   result. Velocity is then constant across a frame and the slices sum to the stock move. It
   also disposes of two hazards without touching them: `0x4322b0` adds
   `acceleration × multiplier` to an already-multiplied velocity, which is one factor short
   when sliced; and bounce tests a position threshold, which tested more often than the step
   that carried the bullet out can fire twice when Bresenham makes two ticks unequal.
4. **Counters that count calls**: the off-screen grace (`+0xda8`), the off-screen count
   (`+0xdba`, both directions) and two manager counters are gated to the frame tick
   (`gate_block`); the laser's graze test is `timer % 20 == 0`, true on *every* tick of such a
   frame, and is and-ed with "the timer's integer changed on the last tick" (`previous` at
   laser `+0x588`, `current` at `+0x590`); and a spawn or death animation's "finished" is only
   acted on at a frame tick — `ExecuteScript`'s result is zeroed on the others and the frame
   tick asks again.

Items need nothing: `ItemManager::OnUpdate` multiplies by the multiplier at every use. Lasers
need only the graze test; their growth already reads the multiplier. **Neither has been through
the trace** — the demonstration has no lasers and does not record items.

While it is on, bullets, lasers and items are drawn where they are (their pools are excluded
from smoothing) and everything else in the playfield is predicted, as in §5. It switches itself
off while a replay plays, for §6's reason: testing collisions more often changes what happens.

`tools/test_th08_stubs.py` runs every emitted stub under Unicorn: both paths of the five
gates with flags, registers and stack checked, the laser flag's four cases, the withheld
"finished" at all four call sites, and the behaviour block's skip, enter and leave.

## 10. Three checks from the TH14 work, run against TH08

The TH14 session found three profile mistakes that had been copied from game to game
(`DEVNOTES_RUNTIME.md`); each was checked here rather than assumed.

- **`native_size_cycle`** is not set, which is right: F10 is the patch's on TH08, one
  `window: size cycle ->` line per press, confirmed in play.
- **The replay loader's play and peek call sites** are not hooked at all; TH08 has no replay
  extension (§6, §9). Whoever adds one has to classify every call into the loader first — a
  peek site allocates a throwaway manager to read a header for the menu's list.
- **A discrete timer rewind scaled by the speed.** `ZunTimer::operator+=` (`0x41fdf0`) has one
  caller, in the ECL interpreter, which runs at 60 Hz. `operator--` (`0x418110`) is called four
  times in `BulletManager::OnUpdate`, once in each wrap behaviour and once in
  `ItemManager::OnUpdate`, all of them "one per call", which is a rate and is scaled correctly
  by the tick's length. The player's shot cycle is in `Player::OnUpdate`, which is never
  sliced. Nothing of that shape is reachable from a sub-stepped path.

## 11. Not done

- A replay extension: TH08's `.rpy` takes a trailing user block like the later games', so
  `core/replay.c`'s USER chunks may carry over. Until something stamps the rate, anything that
  writes game state stays off by default.
- thprac and vpatch for TH08: no conflict sites recorded, no overlap audit. The module-name
  check is all there is.
- `th08e.exe`, other versions, and the Steam release have not been seen.
- Validation on Windows of: texture upscaling, 9Ex, vsync pacing through the bridge,
  exclusive fullscreen, and how prediction feels at 240–480 Hz.
