# Touhou 20 — Fossilized Wonders

The first game after TH15 to be ported, and not a delta against it: five games of engine work
lie between them and the executable is built differently. What carries over is the *model* —
an update list of prioritised callbacks, timers that advance by a global game speed, sprite
VMs drawn in layers — and therefore the whole approach. What does not carry over is any
address, any struct layout, or the way the hooks are attached.

Read [TH15_DEVNOTES.md](TH15_DEVNOTES.md) for the kinds of fix; this file says where TH20 needs
them and what it needs that no earlier game did.

## 1. Identity

| | |
| --- | --- |
| Executable | `th20.exe`, Steam release, v1.00c |
| SHA-256 | `a274b45fe6ec5351…` |
| SizeOfImage / entry RVA / timestamp | `0x1f9000` / `0x1435e0` / `0x68897804` |
| DllCharacteristics | `0x8140`: **relocatable (ASLR)** |
| D3DX | `d3dx9_43.dll` |
| Replay magic / directory | `t20r` / `%APPDATA%\ShanghaiAlice\th20\` |
| Frozen signatures | see `src/games/th20_signatures.h` (generated) |

## 2. State

Bullets, lasers, items, the player (with her shots and stones) and sprite animation step at the
display's rate. Enemies are interpolated; options are carried with the player between frames.
Sub-tick input, replays that carry their rate and per-tick input, and dimming all work.
Everything else — the stage, enemies' logic, bombs, effects, the interface — runs once a frame.

Tested under Wine only: a recording and its playback agree on every frame of the desync trace
at 120 Hz and at 144 Hz, with and without sub-tick input, and a copy of the executable rebased
to `0x1400000` installs and plays. Not yet run on Windows by the author of this port.

## 3. What is new for the runtime

### The executable is relocatable

Every earlier supported game is a fixed-base image and the runtime used absolute addresses
everywhere. TH20 may load anywhere, so:

- `g_image_base` is the module's real base; `HFR_VA(a)` turns a preferred-base address into a
  live one. Profiles and signature tables are still written at the preferred base (`0x400000`).
- `identify_image` compares signatures relocation-aware: `reloc_adjust` walks the base
  relocation table and adds the load delta to every HIGHLOW dword inside a signature before
  comparing. A signature range may therefore not cut a fixup in two; the generator refuses one
  that does.
- `relocate_profile()` builds a moved copy of the identity, the whole `addr` block, the class
  table and the draw addresses, once, at install.
- Adapter code uses `V(a)` (= `HFR_VA(a)`) for every address it emits or calls.

With no ASLR (Wine, or a fixed-base load) the delta is zero and all of this is the identity.

### The build is unoptimised

Source file names are embedded, code is laid out in source order, every local lives on the
stack and every accessor is a real function (`0x40f160` "node of an iterator", `0x414580`
"element of an array", `0x4292e0` `Float::get`...). Two consequences:

- Function matching against TH15 is useless; the port was made from landmarks (strings, import
  calls, the update registrations) and by reading.
- Hook sites are easy: instructions reference `[ebp-N]` slots, so almost any few instructions
  are position-independent, and nothing is held in a register across them.

Source map (code ranges): sprtlib `0x44a994–0x44edb7`, bullet `~0x47b000–0x487000`, enemy
`0x4a70b2…`, item `0x4c5014`, laser `0x4c9b1c–0x4d7e44`, mother `0x4d9220…`, pause `0x4e6b84`,
player `0x4ffff4`, plyshot `0x50627b`, replay `0x5084da–0x50a934`, weapon `0x5305f3–0x5389b3`.

### The update runner is wrapped, not replaced (`runner_wrap`)

The runner (`0x412810`, `this` = `[0x5b66d8]`) walks a container through iterator objects and
handles nine return codes; re-implementing it would mean re-implementing the container. Instead
three hooks leave the game's loop in charge and ask the shared code the same questions the
replacement runner asks itself:

| Site | Hook |
| --- | --- |
| `0x412810` entry | `hfr_wrap_begin`: a skipped pass returns 1 at once |
| `0x412913` the node call (`push [ebp-0x34]; call [ebp-0x38]`) | `th20_node` → `hfr_wrap_node(fn, arg)`: call it, or answer 1 (skip) or 3 (the list was cut here on the frame's first tick) |
| `0x412a4b` the single exit | `hfr_wrap_end` |

Nodes are cdecl with one argument. The frame function (`0x419de0`, `this` = `0x5b6758`) has a
60 Hz limiter of its own, patched out at `0x419e64` (`jb` → `jmp`) and `0x419e85` (NOPs). The
catch-up tick is the adapter's (`th20_update_only`): flush the sprite batch, select the render
target set, run the runner, run the end-of-pass cleanup `0x4d9e30`.

### The game speed is an object

`0x5aefe4` is a `Float` with `get` (`0x4292e0`) and `set` (`0x4292a0`); the rate table at
`0x5aefe0` has it as entry 0 and a timer's flags hold the index. Every logical write is a call
to `Float::set` with the speed in ECX: twelve are hooked with `th20_speed_set` (logical speed
= value, speed = value × factor). `Bullet::update` sets a temporary per-bullet speed at four
sites and restores what it read; the four are hooked with `th20_speed_temp` (× factor), the
restores left alone. `AnmVm::run` forces 1.0 for slowdown-immune sprites at `0x42b628`; its
operand becomes `&g_factor`. The profile says `speed_sites_own`.

## 4. Sub-stepping hazards

### The game gates on "did this timer change" — and that crosses classes

ZUN's own slow-motion support: `Timer::changed()` (`0x45d030`, `cur != prev`) guards things
that must happen once per game frame. Player shots fire on it (`0x505b7e`), which makes the
shot cycle sub-step-safe for free. But `0x4c0ce0` asks it of the **player's** state timer from
the **enemy's** damage query (`0x4c0480`): enemies run on the frame's first tick, when a
sub-stepped player has advanced her timer by 1/k — never an integer change at k = 2, and no
enemy takes any damage. `th20_player_ticked` (site `0x4c04bf`) answers over the whole frame:
the integer after her update on this frame's first tick against the same a frame earlier.

### Timer jumps scale with the speed

`Timer::operator+=(int)` (`0x45c300`), `-=(int)` (`0x4c9170`) and `-=(float)` (`0x535770`)
multiply by the game speed like a tick. Right for a rate; wrong for a jump:

| Site | Jump |
| --- | --- |
| `0x505c5b`, `0x505d23` | the shot timers wrap, `-= 14` and `-= 119`. Scaled, the 14-frame cycle wraps to 7. The wrap also *replaces* that frame's tick, so `th20_timer_wrap` takes back the k−1 ticks that follow as well. |
| `0x505fb1` | a shot's own timer `+= 1` on a hit |
| `0x534fb5` | a stone's gauge `-=` its cost |
| `0x4ce008` | a curvy laser's timer `-=` the nodes it dropped |

(The game's own slow-motion has the same flaw. That is left alone: with no sub-stepping the
original function is called.)

### Per-call work, gated to the frame's first tick

| Site | What |
| --- | --- |
| `0x48620c` | two bullet countdowns `[+0x18]`, `[+0x30]` |
| `0x485cd1` | a spawning bullet (state 2) moves velocity / 2.0 per call: the divisor becomes 2.0 / factor |
| `0x4d6eb0`, `0x4d737d`, `0x4d799d` | laser countdown `[+0x6cc]`, one per laser kind |
| `0x4d2970`, `0x4d2c21`, `0x4d2fdb` | laser graze on `timer % 8 == 0` (`th20_timer_every`) |
| `0x4f748b` | `Player::update`'s state switch: only state 1 (alive) runs on every tick |
| `0x4ffaa2`, `0x4ffab6` | `Player::move` truncation: residual carried (`movement_cvttss`) |
| `0x4fa66d` | the position history the options trail along |
| `0x4fa762` | the focus counter `[+0x20e8]` |
| `0x4fab38` | an option's 30%-per-call approach and its snap countdown `[+0xfc]` |
| `0x4fac3f` | the option's drawn (and firing) position: frame position + the player's movement since (`th20_option_display`) |
| `0x502c67` | a player-side effect with random numbers on `timer % 30 == 0` |
| `0x504547`, `0x504566` | a shot's per-call turn and acceleration × factor |
| `0x53645d`, `0x53674f`, `0x53790d`, `0x537bdf` | a stone's bullet-eating field: countdown `[+0x58]` and the cancel |

## 5. Addresses

| | |
| --- | --- |
| Managers ("side" table at `0x5ba568`) | bullets `+0`, **player `+4`**, enemies `+8`, items `+0xc`, lasers `+0x14` |
| Game manager / sprite manager / input manager | `[0x5ba828]` / `[0x5c0028]` / `[0x5b8898]` |
| Replay manager | `[0x5c60fc]`: mode `+0x10`, frame `+0x250`, stage `+0x258`, per-stage flags `+0x20` |
| Replay save / load | `0x509280` (thiscall, 4 args; calls `0x4e3dea`, `0x52754f`) / `0x508b90` (call `0x508882`; the peek at `0x508ad8` is left alone) |
| Record / playback nodes | `0x509f60` / `0x509f50` |
| Player | int position `+0x620`, float `+0x614`, state timer `+0x644`, shots manager `+0x22b4` (shot timers `+0x12400`, `+0x12410`) |
| Bullet | list at manager `+0x286d64`; position `+0x64`, state `+0x50` |
| Item | list at manager `+0x49c818`; position `+0xbe4`, state `+0xc24` |
| Enemy | list at manager `+0x108`; flags `+0x354`, data `+0x88`: position `+0x110`, sprite slots `+0xc` |
| Input | four 0x2c0-byte objects at `0x5b88b0`: raw `+0`, held `+0x29c`, previous `+0x2a0`, pressed `+0x2a8`, released `+0x2ac`, hold counters `+0x118`, `+0x218` |
| Timer | `{prev, cur, float, flags}`; tick `0x423540`, set `0x423520`, `%` `0x472040`, changed `0x45d030` |

Update callbacks by priority: 1 Mother, 7 Loading, 8 Ascii, 11 Title, 14 SpritesEarly,
15 Pause, 19 Game, 21 replay, 25 SmallScore, 27 StoneMenu/WeaponStone, 28 Hit, 29 Player,
33 Bomb, 36 Enemy, 37 Laser, 38 Bullet, 39 Item, 40 Card, 41 Effect, 42 Front, 44 SpritesLate,
46 the bullet-count slowdown.

## 6. Sub-tick input

The poll (`0x41fe80`, a method of the input manager) reads every device, backs the four input
objects up, shifts each raw word to "previous", rebuilds hold counters and counts a frame. It
cannot be called the TH10–15 way, so the profile supplies `poll_raw`: save the objects, their
backups and the manager (its devices' key states are inside it), call the poll, take the raw
word, put everything back. Devices are polled state (`GetKeyboardState`, XInput, `joyGetPosEx`),
so reading twice loses nothing. "Hold shot to focus" is bit `0x100` of `[0x5c4f8c]` and asks
for 10 frames of the shot's hold counter (`0x50807e`).

## 7. Dimming

The draw runner (`0x412aa0`) dispatches at `0x412b96`; the node is behind an iterator at
`[ebp-0x28]`, so the profile emits the load (`emit_node`). Batch flush `0x4455c0` on
`[0x5c0028]`, VM draw `0x443880` (VM on the stack). A VM holds its layer at `+0x14`, its ANM
slot at `+0x18`, its script at `+0x24`, its position at `+0x5bc`; the manager keeps 42 record
pointers at `+0x6000730`. This is a debug build of the C++ runtime: a record's `std::string`
name starts with an iterator-proxy pointer, so the text is at `+8` — in place up to 15
characters, behind a pointer beyond (`anm_name`).

The draw table: stage 3, `enemy.anm` layer 8 at 20 (the first world object, `world_prio`), the
player's band 28–34 (`pl*.anm` layers 13–15), items 35 and bullets 41 (both `bullet.anm`),
`effect.anm` at 37, 46, 49, 50, the interface from 60.

## 8. Verification

```
tools/test_th20_stubs.py      the emitted stubs, emulated
tools/test_hfr.c              wrapped-runner test (test_runner_wrapped), patch plan, identity
```

Under Wine, with `replay_trace=1` (a per-frame line: player position, a quantised bullet
position sum, item count and position sum, bullet count):

- `fps=60`: the title demo's trace is identical to `substep=0`, frame for frame.
- 120 Hz with only the sprite passes sub-stepped: identical to stock.
- 120 Hz, everything on, against the stock demo: bullet and item counts differ by one for a
  frame or two at a time (an object leaves a sub-tick earlier) until the first enemy dies a
  frame apart, after which the run is a different run. Damage dealt tracks stock within a few
  percent up to there. A stock replay is always played back at 60 Hz for this reason.
- Record, save, restart, play back: 120 Hz 1299/1299 frames, 144 Hz 1246/1246, with sub-tick
  input.
- A copy rebased to `0x1400000`: installs (runner hook at `0x01412b96`) and plays.

## 9. Open

- Windows, real hardware, D3D9Ex: untested. TH20 does not import
  `D3DXCreateTextureFromFileInMemoryEx` (it creates with `D3DXCreateTexture` and fills with
  `D3DXLoadSurfaceFromFileInMemory`), so the D3D9Ex path now accepts that import's absence.
- `pp`, the screenshot routine and the window-flags word are not described: resizing with
  scaling off leaves the chain alone, and the game's screenshots are not corrected for scaling.
- Bombs, effects and the stage run at 60 Hz.
- The English patch and thprac/thcrap are untested.
