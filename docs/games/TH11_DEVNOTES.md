# TH11 (Subterranean Animism) developer notes

Shared design: [ARCHITECTURE.md](../../ARCHITECTURE.md); installation:
[README.md](../../README.md); runtime findings: [DEVNOTES_RUNTIME.md](../DEVNOTES_RUNTIME.md).
The TH11 profile is `src/games/th11.c` with `src/games/th11_signatures.h` and
`src/games/th11_conflicts.h`; where this file disagrees with them, the code is right.

Origin: the per-game build TH11 **0.1.0-test** (September 2026), a port of `th12_hfr` commit
`57b0821d7f50daaa1c1a4ee408db8ae0d74b999a` (TH12 v0.11; its shared design is the original
[DEVNOTES.md](https://github.com/vittorioromeo/th12_hfr/blob/57b0821d7f50daaa1c1a4ee408db8ae0d74b999a/DEVNOTES.md)).
That build kept TH11 in separate files (`src/hfr11.c`, `src/th11_sites.h`,
`src/th11_install.h`) so an early port could not disturb the working TH12 build. Addresses,
layouts and hooks below are checked against the unified `src/games/th11.c`; §8 is the per-game
build's own build and test record.

## 1. Supported images

Both executables are x86 PE32, image base `0x400000`, file size 688128.

| Executable | SHA256 |
| --- | --- |
| Japanese `th11.exe`, v1.00a | `2978b17f6184d100d249d4311348dd30c5c32ec75c014b667a525b797d3d8813` |
| English `th11e.exe`, static patch v1.0 | `18555e5055909570dbf46ca2a7cb796c50174fcdffb863a83357110d7f3f770b` |

The loader recognises the reviewed code layout by frozen instruction signatures, not a
full-file hash: 66 in the per-game build, 67 now (`0x446901`, the screenshot call site, was
added as an identity anchor with the dimming work). All match both images. All are checked
before any game hook is installed; a mismatch leaves HFR disabled while the DirectInput proxy
still forwards to the system DLL.

`tools/th11_signatures.json` is the reviewable manifest; `src/games/th11_signatures.h`
(`src/th11_signatures.h` in the per-game build) holds the same bytes for runtime validation.
The manifest also records selected called-function prefixes, not only overwritten
instructions. Do not regenerate expected bytes from an arbitrary executable and call it
supported: a new layout needs a new instruction and calling-convention audit first.

Analysis used Ghidra 11.3.2, Capstone, PE inspection and comparison with the TH12
decompilation. `touhouworldcup/thprac`'s TH11 source served as an independent address
reference. No game executable, data archive, decompilation or process memory dump belongs in a
release.

Identity: magic `t11r`, legacy INI `th11_hfr.ini`, image size `0xcd000`.

## 2. Engine map

Absolute addresses for the v1.00a image.

| Symbol | TH11 address / layout |
| --- | --- |
| Game speed | `0x4a7948`, float |
| Runner pointer | `0x4c3234`; update list `+0x18`, ending flag `+0x48` |
| Update / draw runner | `0x456cb0` / `0x456e10`; the update runner's `ret` at `0x456deb` is `runner_ret` (see DEVNOTES_RUNTIME) |
| Remove update node | `0x457080`, ECX=node, EDX=runner |
| Vsync frame function | `0x446650`, stdcall(context) |
| Frame call sites | `0x44587e`, `0x44589b`, `0x4458a7` |
| Present | `0x446790`; latency-sleep comparison at `0x446799` (`latency_cmp`) |
| Frame context / flag | `0x4c37cc` / `0x4c37d0`; context value `0x4c359c` |
| Runner critical section / depth | `0x4c3a90` / `0x4c3bb0`; enabled by `0x4c3810 & 0x8000` (`misc_flags`, `critical_flag_mask`) |
| Scene cleanup | `0x459430`, ESI=`0x4c3a70` |
| D3D device / present parameters | `0x4c3288` / `0x4c3374` |
| Window flags / active | `0x4c3dc0` / `0x4c3d94` |
| Frame duration | `0x4c3c38`, double |
| GameManager | `0x4a8e88`; callback `0x420840`; pause flags at `+0x60`, mask `0x70` |
| Raw input poll | `0x4576b0`; read its output global, not EAX |
| Raw input block | `0x4c92a8`, size `0x130`, 32-bit words; raw pressed `0x4c92b4` |
| Game input | `0x4c93c0`; pressed `0x4c93cc`, released `0x4c93d0` |
| Autofocus counter / option flags | `0x4c93bc` / `0x4c3480` |
| Timer add / tick / ftol | `0x459210` / `0x459270` / `0x4864e0` |
| MotionState step | `0x459590`; Cartesian displacement at `0x45959c` |
| Screenshot routine / call site | `0x429ca0` / `0x446901` |
| D3DX | `d3dx9_37.dll` |

UpdateFunc layout: priority `+0`, flags `+4`, thiscall callback `+8`, cleanup `+0x10`, embedded
list node `+0x14`, argument `+0x20`. The original runner receives its object in EBX. Its return
protocol is preserved: remove, continue, repeat, stop, scene end, restart-list and cleanup. The
draw runner is left intact.

TH11 has no F10 size cycle of its own (`native_size_cycle = 0`): its window procedure only
swallows `SC_KEYMENU`, and nothing in the executable compares against `VK_F10`.

vpatch conflict sites are in `src/games/th11_conflicts.h`; the three call sites there target
the timing routine `0x446920`.

### Node classification

| Callback | System | Cadence |
| --- | --- | --- |
| `0x408ec0` | Bullets | Sub-step |
| `0x431c50` | Player | Sub-step |
| `0x424090` | Items | Sub-step |
| `0x424d70` | Lasers | Sub-step |
| `0x403900` | Stage | Sub-step |
| `0x455120` / `0x455130` | World / UI ANM lists | Sub-step |
| `0x4064d0` | Bomb | Frame |
| `0x41cfb0` | GUI | Frame |
| `0x40e300` | Spellcard | Frame |
| `0x4111a0` | EnemyManager | Frame |
| `0x421b20` | `PlayerBomb?` (name unconfirmed) | Frame |
| `0x420840` | GameManager | Frame |
| Everything else | Supervisor, replay, menus, etc. | Frame |

## 3. Time, input and speed writes

The schedule is a 1/256-frame Bresenham sub-step sequence. Integer rates from 60 through 1000
produce exactly 60 game frames per second. `g_major` marks the first tick starting within each
new integer frame. Stage starts restart the sequence and clear the two fixed-point movement
residuals.

All speed sites are 6-byte `fstp [0x4a7948]` with `pop_float = 1`.

| Operation | Sites |
| --- | --- |
| Permanent 1.0 (`SPEED_ONE_PERM`) | `0x41f963`, `0x41fe23`, `0x42956d`, `0x4314b8`, `0x459b0c` |
| Temporary 1.0 (`SPEED_ONE_TEMP`) | `0x402b7c`, `0x44b4f3` |
| ECL speed store (`SPEED_ECL`) | `0x4169d0` |
| Pause save/set (`SPEED_PAUSE_SET`) | `0x42c73f`, `0x42c85d`, `0x42d696`, `0x42d7db` |
| Pause restore (`SPEED_PAUSE_RESTORE`) | `0x42c8b6`, `0x42d845`, `0x42e6a5` |

Paired stores that restore an already effective speed stay intact, as does the laser manager's
temporary zero speed. Constants passed to `Timer::add` are not rates: `-14` at `0x4343fc` is
the shot-cycle subtraction and `0x4355cd` adds an ANM wait offset. Both use logical speed
without the sub-step factor.

Input between frame ticks: the poll's raw-state writes are saved and restored, and only the
movement and focus bits are merged into the game word. EAX is not a valid input return from
TH11's poll. Joystick polling uses the between-frame cache. During a minor Player callback the
pressed/released words and the held bomb bit are masked and then restored
(`mask_minor_player_edges = 1`); this prevents repeated Marisa B formation switches and keeps
bombs and deathbombs frame sampled.

Behaviour added with the TH11 port:

- `substep=0` selects 60 simulation ticks, independently of presentation rate.
- Software pacing uses presentation slots, so a 60 Hz replay can still present at a higher rate.
- Device resets preserve an active replay's logic rate and sequence when the rate is unchanged.
- Duplicate presents do not advance enemy interpolation history.
- Stock-rate Player updates bypass the remainder-carry ftol wrapper.
- The duplicate-load mutex is process scoped.

## 4. Player and shots

Player pointer: `0x4a8eb4`. The update wrapper `0x431c50` calls `0x431070` with its player
argument on the stack. Movement is `0x430290`.

| Field | Offset |
| --- | --- |
| Float position | `+0x87c`, `+0x880`, `+0x884` |
| Fixed-point X/Y | `+0x888`, `+0x88c`, **1/128 pixel** |
| Velocity X/Y | `+0x8a0`, `+0x8a4` |
| Player state | `+0x928` |
| State timer prev / integer / float | `+0x944` / `+0x948` / `+0x94c` |
| Shot-cycle timer | `+0x930` / `+0x934` / `+0x938` |
| Focus | `+0x8d20` |
| Shot array | `+0x96c`, 256 entries, stride `0x6c` |

- **Movement**: the ftol calls at `0x430722` / `0x430735` carry the truncated remainder during
  sub-steps. The displacement format is 1/128 pixel; do not assume 16.16.
- **MotionState** (`0x45959c`, 27 bytes): the shared Cartesian path scales displacement. The
  polar branch computes an absolute position from a centre and radius; it is not a velocity
  addition and is not multiplied.
- **Player damage sources and shots**: acceleration at `0x4315a5` and `0x434562`, displacement
  at `0x4315d0` (20 bytes), and turn at `0x4315af` / `0x43456c` are scaled by the factor.
- **Reimu C**: the warmup at `0x43065f` is frame gated. The particle block at `0x43066e` spawns
  at frame cadence while its doubled X/Y displacement applies on every sub-tick.
- **Frame gated**: Reimu B's collection warmup at `0x43078f`, Yukari's warp function
  `0x430e50`, and the option-loss counter `0x430b48`.
- **Options**: the loop `0x430b4e`–`0x430d91` stays at 60 Hz. It contains integer easing and
  per-call orbit/aim callbacks (`0x433690`, `0x4337a0`). Options are a known remaining source of
  60 Hz motion.
- **Shot callbacks**: table `0x4a3a3c` contains homing `0x434e30`, gravity `0x4352a0` and
  attached-laser positioning `0x435330`. Homing and gravity run only when the shot's integer
  timer changes (`EDX+0` vs `EDX+4`); laser anchoring is unconditional. The shot's positional
  integration continues on sub-ticks.
- **Once-per-frame guards**: the player death-drop equality at `0x4312c6` and the periodic
  counter at `0x431ab6` (timer `ESI+0x944`).
- **Enemy hit-test guard** at `0x434814`: enemy shot-hit processing stays at enemy frame
  cadence. The guard compares saved float timer samples (`g_ptf_prev` / `g_ptf_cur`) instead of
  requiring the last sub-tick to have crossed an integer timer value.

## 5. Bullets, items, lasers, stage, ANM and enemies

- **Bullets**: stride `0x910`, manager array `+0x64`, position `+0x43c`, timer
  `+0x464/+0x468/+0x46c`. The lifetime/grace block at `0x408cbc` is gated on the bullet's timer.
- **Items**: stride `0x478`, manager array `+0x14`. The state-5 countdown at `0x4235af` has
  **five live x87 values**: its minor path must keep the original five pops and select the
  non-expired signed branch. Collection acceleration at `0x4238f9` / `0x4239a1` scales the
  double constant at `0x498130` (0.2) by game speed.
- **Lasers**: TH11's classes differ from TH12's. Line vtable `0x494a64`, update `0x425cc0`;
  beam vtable `0x494abc`, update `0x4272c0`. The line wait at `0x425de0` and the graze blocks
  `0x426097` / `0x427580` are gated on the relevant object timers. TH12's curved-laser and
  waiting-beam offsets do not apply.
- **Stage** update `0x402aa0`: the distortion/RNG block at `0x402bc5` and the frame counter at
  `0x403121` are frame gated; the surrounding saved-speed restores stay intact.
- **ANM**: manager `0x4c3268`, VM lookup `0x4561e0` (EDX manager, ID on stack, callee cleanup),
  VM size `0x434`. Timer at `+0x5c/+0x60/+0x64`; position `+0x3e8/+0x3ec/+0x3f0`. Lists at
  manager `+0x7b562c` / `+0x7b5634`. Mesh callbacks `0x408070` and `0x452420` are gated on their
  VM timers.
- **Enemies**: EnemyManager `0x4a8d7c`, list `+0x68`. Enemy position `+0x1070`, sprite IDs
  `+0x111c` (eight entries), flags `+0x25bc`. TH12's per-slot offset/parent arrays do not
  exist here. Interpolation (`th11_place_enemy`) follows the TH11 position helper: add
  `(224,16,0)` and propagate to child VMs when the parent VM's `+0x18` is zero.
  Background-relative enemies (`flags & 0x400000`) keep the game's positioning.

## 6. Draw list, sprite VMs and dimming data

Mechanism: DEVNOTES_RUNTIME (dimming). Rules: `th11_dim_rules` in `src/games/th11.c`.

| Item | Value |
| --- | --- |
| Draw runner / dispatch | `0x456e1d` / `0x456e81` (`mov ecx,[esi+0x20]; mov eax,[esi+8]; call eax`) |
| Sprite batch flush | `0x44fd10` (ESI = AnmManager, pointer at `0x4c3268`, pending count at manager+0x435620) |
| Sprite VM draw | `0x451ef0` (VM in EAX, 0x434 bytes) |
| VM fields | loaded-ANM pointer +0x3b0, layer +0x20, `slot << 16 \| sprite` at +0x39c, script index +0x3a2 |
| ANM slots seen | 0 text, 4 stage01, 5 front, 6 bullet, 7 the player, 8 enemy, 27 st01logo |

Draw priorities (`debug=1` trace, stage 1): 1 `0x4290e0` binds the offscreen stage target, 2
`0x403910` the 3D stage (VB draws), 4..10 sprite layers 0..3, **11 `0x429220` binds the world
target (`world_prio`)**, 13 `0x4293d0` copies the stage into it, 12/15..19 layers 4..9, 20
EnemyManager (`0x4111b0`, draws nothing itself: the enemies are sprite-layer VMs of enemy.anm,
layer 10 → priority 21, unlike TH13 where they sit on layers 8/9), 21 layer 10 (enemies and the
player's shots together; the rules tell them apart by ANM), 22 Player body, 23/24 layers 11/12,
**25 ItemManager `0x4240d0`**, 27 LaserManager, 29 BulletManager, 31 Spellcard (the name text),
32..34 layers, 35 `0x4292b0` back to the stage target, 37 `0x429420` copy, 42/43 Gui, 46
`0x429340` back buffer, 47 `0x429470` the final copy, 48 on the interface.

- **bullet.anm**: items on layer 9. Bullets are the BulletManager's (29), their VMs' layer left
  at 0. Layer 15 (priority 32) holds the enemy death bursts (scripts 73-94 and 115-188, the
  ones setting `ins_68(15)`; 95-114 between them are bullet scripts) and other effects, so the
  whole file bar its item layer falls through to the effects class. In `th11_dim_rules`,
  scripts 73-74 on layer 15 are the player's hitbox and are never dimmed.
- **enemy.anm**: layer 6 is spawn flashes and auras, 7/8 the enemies.
- **pl0X.anm**: shots on layer 10.
- **Stage-enemy ANM**: layer-5 scripts are the spell-card portraits (never faded). Its card
  backgrounds set no layer; they are drawn under the world and dimmed by the quad.

## 7. Replay map

ReplayManager pointer `0x4a8eb8`:

| Field / function | TH11 location |
| --- | --- |
| Mode (0 record, 1 playback) | `+0x10` |
| Stage pointers | `+0x1c + 4*s` |
| Stage frame / stage number | `+0x1cc` / `+0x1d4` |
| Flags | `+0x1d8` |
| Record / playback node | `0x436d20` / `0x436d30` |
| Stage start | `0x436da0` |
| Save | `0x436420`, fastcall(filename, name, stack argument), ret 4 |
| Load | `0x436b60`, stdcall(manager, filename), ret 8 |
| Save call sites | `0x42d2ae`, `0x42e21b`, `0x42ef4b`, `0x4403c5` |
| Playback load calls | `0x435a0d`, `0x435c2e` (play, modes 1 and 2) |

The two info-only replay loads stay unhooked. Magic is `t11r`; the USER offset is at file
header `+0xc`. Chunk types `0x48` and `0x49` carry the rate and the `HFRI` version-1 RLE input
stream. The reader bounds chunk arithmetic, validates RLE lengths, run sums, stage uniqueness
and input bits, and limits allocation to 3.6 million ticks per stage.

## 8. Per-game build: build and validation record

Built with 32-bit MSYS2 MinGW GCC 16.2.0. The PowerShell build resolves the compiler directory
and temporarily adds it to PATH, which GCC's subprocesses need. The DLL exports undecorated
`DirectInput8Create`; its imports use standard Windows DLLs, with no libgcc/libwinpthread
runtime dependency. TH11 built without warnings under the supplied flags, alongside the
unchanged TH12 target.

`test_th11.ps1` ran a native harness against each supplied executable and emulated the emitted
stubs with Unicorn (the unified runtime's equivalent is `tools/test_th11_stubs.py`). Passed:

- 10 seconds of the actual C scheduler at every integer rate 60–1000, all selected
  logic/presentation-rate combinations, stock mode, stage reset, replay/device reset.
- Valid RLE decode; rejection of malformed sizes, runs and input bits.
- All 66 signatures in both supported images.
- Both paths of 12 ordinary guards, including preserved flags, registers and stack.
- Mesh/shot callback return paths; the item countdown's five-pop x87 cleanup; Reimu C
  minor-tick displacement; Cartesian/linear/angular scaling.
- Positive/negative fixed-point remainder carry using both actual game ftol paths (SSE and
  x87), stock truncation, and constant timer subtraction with ret-4 cleanup.

The harness reserves an inert `.fixture` section at `0x400000`; its linker padding assumes the
GCC build's section layout. If another toolchain changes the layout, adjust the padding or link
addresses (or supply an equivalent reserved fixture image); a Windows loader failure there is
not a mod failure. Generated `.game` files contain copyrighted game code and stay in ignored
`build/tests/`; they are never packaged.

Live, on an isolated copy of the English game with an existing D3D9 wrapper: D3D9Ex creation
succeeded, maximum frame latency 1 was confirmed, menus measured 360.00 presents/s, and stage 1
measured 359.80–360.00 with 300 extra input polls/s. The owner briefly tested stage 1 with
Reimu (partner not specified) and reported correct behaviour at a glance.

## 9. Open items

- The live test in §8 preceded the homing/gravity gating, the edge-input masking and the
  replay/pacing safeguards. Those passed the offline tests but had no further controlled live
  pass in the per-game build.
- Not tested in depth: all partners, full runs, pause/bombs/deathbombs, stage transitions,
  fullscreen/wrappers, end-to-end replays. Runtime replay round-trip determinism is unverified.
- Options move at 60 Hz (§4).
