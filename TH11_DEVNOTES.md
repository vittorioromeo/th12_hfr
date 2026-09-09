> Historical notes for the original per-game build. The current unified runtime and source locations are documented in [ARCHITECTURE.md](ARCHITECTURE.md); installation instructions are in [README.md](README.md).

# TH11 HFR port development notes

Port baseline: `th12_hfr` commit `57b0821d7f50daaa1c1a4ee408db8ae0d74b999a`
(TH12 v0.11). TH11 build: **0.1.0-test**, September 2026.

Read the original [DEVNOTES.md](https://github.com/vittorioromeo/th12_hfr/blob/57b0821d7f50daaa1c1a4ee408db8ae0d74b999a/DEVNOTES.md) for the shared design. TH11 is
implemented separately in `src/hfr11.c`, `src/th11_sites.h`, and
`src/th11_install.h`; the TH12 code and shell build/package scripts remain intact.
This deliberately avoids turning an early reverse-engineered port into an
unverified change to a working TH12 build.

## Evidence and supported images

Both supplied executables are x86 PE32, image base `0x400000`, file size 688128.

| Executable | SHA256 |
| --- | --- |
| Japanese `th11.exe`, v1.00a | `2978b17f6184d100d249d4311348dd30c5c32ec75c014b667a525b797d3d8813` |
| English `th11e.exe`, static patch v1.0 | `18555e5055909570dbf46ca2a7cb796c50174fcdffb863a83357110d7f3f770b` |

The loader uses 63 frozen instruction signatures, rather than a full-file hash,
to recognize the reviewed code layout. All signatures match both supplied images.
It checks all of them before installing any game hooks. A mismatch leaves HFR
disabled while the DirectInput proxy still forwards to the system DLL.

`tools/th11_signatures.json` is the reviewable manifest; `src/th11_signatures.h`
contains the same bytes for runtime validation. Do not regenerate expected bytes
from an arbitrary executable and call it supported. A new layout needs a new
instruction/calling-convention audit first. The manifest also records selected
called-function prefixes, not just overwritten instructions.

Analysis used Ghidra 11.3.2, Capstone, PE inspection, and comparison with the TH12
decompilation. `touhouworldcup/thprac`'s TH11 source was used as an independent
address reference. No game executable, data archive, decompilation, or process
memory dump belongs in a release.

## Engine map

All addresses below are absolute addresses for the supported v1.00a image.

| Symbol | TH11 address / layout |
| --- | --- |
| Game speed | `0x4a7948`, float |
| Runner pointer | `0x4c3234`; update list `+0x18`, ending flag `+0x48` |
| Update / draw runner | `0x456cb0` / `0x456e10` |
| Remove update node | `0x457080`, ECX=node, EDX=runner |
| Vsync frame function | `0x446650`, stdcall(context) |
| Frame call sites | `0x44587e`, `0x44589b`, `0x4458a7` |
| Present | `0x446790`; latency-sleep comparison at `0x446799` |
| Frame context / flag | `0x4c37cc` / `0x4c37d0`; context value `0x4c359c` |
| Runner critical section / depth | `0x4c3a90` / `0x4c3bb0`; enabled by `0x4c3810 & 0x8000` |
| Scene cleanup | `0x459430`, ESI=`0x4c3a70` |
| D3D device / present parameters | `0x4c3288` / `0x4c3374` |
| Window flags / active | `0x4c3dc0` / `0x4c3d94` |
| Frame duration | `0x4c3c38`, double |
| GameManager | `0x4a8e88`; pause flags at `+0x60`, mask `0x70` |
| Raw input poll | `0x4576b0`; read its output global, not EAX |
| Raw input block | `0x4c92a8`, size `0x130` |
| Game input | `0x4c93c0`; pressed `0x4c93cc`, released `0x4c93d0` |
| Autofocus counter / option flags | `0x4c93bc` / `0x4c3480` |
| Timer add / tick / ftol | `0x459210` / `0x459270` / `0x4864e0` |
| MotionState step | `0x459590`; Cartesian displacement at `0x45959c` |

UpdateFunc layout is priority `+0`, flags `+4`, thiscall callback `+8`, cleanup
`+0x10`, embedded list node `+0x14`, argument `+0x20`. The original runner receives
its object in EBX. Its return protocol is preserved: remove, continue, repeat,
stop, scene end, restart-list, and cleanup. The draw runner is left intact.

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
| `0x420840` | GameManager | Frame |
| Everything else | Supervisor, replay, menus, etc. | Frame |

## Time, input, and speed writes

The port retains the 1/256-frame Bresenham sub-step schedule. Integer rates from
60 through 1000 produce exactly 60 game frames per second. The `g_major` flag
marks the first tick starting within each new integer frame. Stage starts restart
the sequence and clear the two fixed-point movement residuals.

Permanent 1.0 speed stores: `0x41f963`, `0x41fe23`, `0x42956d`, `0x4314b8`,
`0x459b0c`. Temporary 1.0 stores: `0x402b7c`, `0x44b4f3`. ECL speed store:
`0x4169d0`. Pause save/set: `0x42c73f`, `0x42c85d`, `0x42d696`, `0x42d7db`;
pause restore: `0x42c8b6`, `0x42d845`, `0x42e6a5`.

Paired stores restoring an already effective speed stay intact, as does the
laser manager's temporary zero speed. Constants passed to Timer::add need
separate handling: `-14` at `0x4343fc` is shot-cycle subtraction, and `0x4355cd`
adds an ANM wait offset. These use logical speed without the sub-step factor.

Between frame ticks, the input poll's raw-state writes are saved/restored and
only movement/focus bits are merged into the game word. EAX is not a valid input
return from TH11's poll. Joystick polling uses the inherited between-frame cache.
During a minor Player callback, pressed/released words and the held bomb bit are
masked and then restored. This prevents repeated Marisa B formation switches and
keeps bombs/deathbombs frame sampled.

TH11-specific fixes beyond the original shared runtime:

- `substep=0` selects 60 simulation ticks, independently of presentation rate.
- Software pacing uses presentation slots, so a 60 Hz replay can still present
  at a higher rate.
- Device resets preserve an active replay's logic rate and sequence when the
  rate is unchanged.
- Duplicate presents do not advance enemy interpolation history.
- Stock-rate Player updates bypass the remainder-carry ftol wrapper.
- The duplicate-load mutex is process scoped.

## Player and shots

Player pointer: `0x4a8eb4`; update wrapper `0x431c50` calls `0x431070` with its
player argument on the stack. Movement is `0x430290`.

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

Movement ftol calls `0x430722` / `0x430735` carry the truncated remainder during
sub-steps. TH11's displacement format must not be treated as a guessed 16.16
format. The shared Cartesian MotionState path scales displacement; player damage
sources and shot angular/linear acceleration have additional local corrections.
The MotionState polar branch computes an absolute position from a center/radius;
it is not a plain velocity addition and is not multiplied wholesale.

Reimu C warmup at `0x43065f` is frame gated. The particle block at `0x43066e`
spawns at frame cadence while its doubled X/Y displacement applies on every
sub-tick. Reimu B's collection warmup at `0x43078f`, Yukari's warp function
`0x430e50`, and the option-loss counter `0x430b48` are also frame gated.

The option loop `0x430b4e`–`0x430d91` retains 60 Hz behavior: it contains integer
easing and per-call orbit/aim callbacks (`0x433690`, `0x4337a0`). Thus options are
a known remaining source of 60 Hz motion.

Shot callback table `0x4a3a3c` contains homing `0x434e30`, gravity `0x4352a0`,
and attached-laser positioning `0x435330`. Homing and gravity run only when the
shot's integer timer changes (`EDX+0` vs `EDX+4`); laser anchoring remains
unconditional. The shot's positional integration continues on sub-ticks.

Player death-drop equality at `0x4312c6` and the periodic counter at `0x431ab6`
are guarded against repeated execution. Enemy shot-hit processing still happens
at enemy frame cadence; its guard at `0x434814` compares saved float timer samples
instead of requiring the last sub-tick to have crossed an integer timer value.

## Bullets, items, lasers, stage, and ANM

- Bullet stride `0x910`, manager array `+0x64`, position `+0x43c`, timer
  `+0x464/+0x468/+0x46c`. The local lifetime/grace block at `0x408cbc` is guarded.
- Item stride `0x478`, manager array `+0x14`. The state-5 countdown at `0x4235af`
  has **five live x87 values**: its minor path must retain the original five
  pops and select the non-expired signed branch. Collection acceleration at
  `0x4238f9` / `0x4239a1` scales the double constant at `0x498130` by game speed.
- TH11's laser classes differ from TH12: line vtable `0x494a64`, update
  `0x425cc0`; beam vtable `0x494abc`, update `0x4272c0`. Line wait at `0x425de0`
  and graze blocks `0x426097` / `0x427580` use the relevant object timers.
  Do not copy TH12 curved-laser or waiting-beam offsets here.
- Stage update `0x402aa0`: distortion/RNG block at `0x402bc5` and frame counter
  at `0x403121` are gated; surrounding saved-speed restores stay intact.
- ANM manager `0x4c3268`, VM lookup `0x4561e0` (EDX manager, ID on stack, callee
  cleanup), VM size `0x434`. Timer at `+0x5c/+0x60/+0x64`; position
  `+0x3e8/+0x3ec/+0x3f0`. Lists at manager `+0x7b562c` / `+0x7b5634`.
  Mesh callbacks `0x408070` and `0x452420` are gated on their VM timers.

EnemyManager `0x4a8d7c` has list `+0x68`. Enemy position is `+0x1070`, sprite
IDs `+0x111c` (eight entries), and flags `+0x25bc`. TH12's per-slot offset/parent
arrays must not be transplanted. Interpolation follows the TH11 position helper:
add `(224,16,0)` and propagate to child VMs when the parent VM's `+0x18` is zero.
Background-relative enemies (`flags & 0x400000`) retain the game's positioning.

## Replay map

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
| Playback load call | `0x435a0d` |

The two info-only replay loads remain unhooked. TH11 magic is `t11r`; the USER
offset is at file header `+0xc`. Chunk types `0x48` and `0x49` carry the rate and
`HFRI` version-1 RLE input stream. The reader bounds chunk arithmetic, validates
RLE lengths/run sums/stage uniqueness and input bits, and limits allocation to
3.6 million ticks per stage. Runtime round-trip determinism remains unverified.

## Build and validation record

Built with 32-bit MSYS2 MinGW GCC 16.2.0. The PowerShell build resolves the compiler
directory and temporarily adds it to PATH, which is needed by GCC's subprocesses.
The DLL exports undecorated `DirectInput8Create`; its imports use standard Windows
DLLs, with no libgcc/libwinpthread runtime dependency. TH11 builds without warnings
under the supplied flags. Both TH11 and the unchanged TH12 target build.

`test_th11.ps1` runs a native harness against each supplied executable and emulates
the emitted stubs with Unicorn. Passed:

- 10 seconds of the actual C scheduler at every integer rate 60–1000, all selected
  logic/presentation-rate combinations, stock mode, stage reset, replay/device reset.
- Valid RLE decode and malformed sizes, runs, and input-bit rejection.
- All 63 signatures in both supported images.
- Both paths of 12 ordinary guards, including preserved flags, registers, and stack.
- Mesh/shot callback return paths; the item countdown's five-pop x87 cleanup;
  Reimu C minor-tick displacement; Cartesian/linear/angular scaling.
- Positive/negative fixed-point remainder carry using both actual game ftol paths
  (SSE and x87), stock truncation, and constant timer subtraction/ret-4 cleanup.

The harness reserves an inert `.fixture` section at `0x400000`; its linker padding
assumes the present GCC build's section layout. If another toolchain changes the
layout, adjust the padding/link addresses (or supply an equivalent reserved
fixture image), rather than interpreting a Windows loader failure as a mod failure.
Generated `.game` files contain copyrighted game code and stay in ignored
`build/tests/`; they are never packaged.

Live observations from an isolated copy of the English game with the existing
D3D9 wrapper: D3D9Ex creation succeeded, maximum frame latency 1 was confirmed,
menus measured 360.00 presents/s, and stage 1 measured 359.80–360.00 with 300 extra
input polls/s. The owner briefly tested stage 1 with Reimu and reported correct
behavior at a glance. Partner was not specified. Computer control was stopped by
the owner; no further automated gameplay input was sent.

That live test preceded the final homing/gravity, edge-input, and replay/pacing
safeguards. Those subsequent changes passed the offline tests, but have not had
another controlled live pass. All partners, full runs, pause/bombs/deathbombs,
stage transitions, fullscreen/wrappers, and end-to-end replays need deeper testing.
