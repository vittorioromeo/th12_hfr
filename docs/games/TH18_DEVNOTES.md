# TH18 v1.00a: porting record

## Status

First playable **experimental** adapter, 2026-09-21. Native gameplay runs at 60 Hz;
the shared HFR scheduler presents at the display rate and ordinary 2D sprite quads are
interpolated. This is not the TH15 sub-stepping adapter with new addresses. Ability cards,
shops, movement, damage, collision, shot cadence, RNG and replay recording/playback remain
in the original native update runner. Replays receive no HFR extension.

No game was launched or driven during development, as requested. Hardware validation is
pending: F11, F10/window/fullscreen transitions, ordinary stage play, several active/passive
cards, the stage-end market, pause/retry, screenshots, replay playback and clean exit.
Non-quad lasers and the 3D stage camera are not interpolated yet. Prediction is available
but defaults off in this adapter; it changes only submitted geometry and may overshoot turns.
Interpolation has the usual one-native-frame visual delay.

## Binary and tools

User installation: `G:\TouhouClean\(TH18) Touhou Kouryuudou ~ Unconnected Marketeers\`.
Original executable SHA256:
`6243e3624ae5100eaa5ded846e2d9b2d9e438ee7c20735e197c9c8170fb9627f`.
PE32 x86, preferred base `0x400000`, image size `0x174000`, entry RVA `0x8e5c9`.
Imports include D3D9, D3DX9_43, DirectInput8, XInput1_3, DirectSound and winmm.
The supplied readme identifies ver 1.00a (2021-05-04).

Static analysis used pefile, Capstone and Ghidra 11.3.2 with `tools/ExportAll.java`
(2,037 functions exported). Local scratch under `build/th18-research/` contains the
inert binary copy, full disassembly, decompilation, callback registration inventory and
build logs; these are not release artifacts. Reproduce with `tools/porting/inspect_pe.py`
and a Ghidra headless import. Copy an executable whose path has parentheses into scratch
before calling Ghidra's Windows batch launcher (its argument handling otherwise fails).

The locally available thprac TH18 source supplied candidate pointers and its update/render
hook sites; the adapter addresses were then checked against the installed binary.
Normalized TH15 function matching alone gave poor/ambiguous matches and was not used as proof.

## Frame path and ABI

| Address | Meaning |
| --- | --- |
| `0x4012e0` / `0x4013f5` | Update runner entry / terminal RET, object in ECX |
| `0x401420` / `0x401510` | Draw runner entry / terminal RET |
| `0x401490` | Draw dispatch: ECX = node+0x24, callback = node+8, node in EDI |
| `0x401180` / `0x401230` | Register update / draw node, two stack arguments |
| `0x4015a0` | Remove node: runner in ECX, node pushed |
| `0x4cf294` | Update runner pointer |
| `0x521660`, `0x5217b0`, `0x5217be` | Critical section, nesting byte, enable byte |
| `0x472fd0` | Unpaced native frame (apart from optional Sleep guard) |
| `0x472dd0` | Alternative frame with a 60 Hz time loop |
| `0x471c4e`, `0x471c5a` | Window-loop calls selecting those frame functions |
| `0x471a9e` | Branch into/out of the inlined automatic-latency frame path |
| `0x4730be` | Optional 60 Hz Sleep guard inside the chosen native frame |
| `0x472ff0` | Native update call within that frame |
| `0x41b330` | Select supervisor viewport/context; ECX object, one pushed integer |
| `0x4ccdf0`, `0x4cd884`, `0x402b30` | Supervisor, cleanup object, cleanup method |
| `0x4ccdf8`, `0x4ccee4` | D3D device pointer, presentation parameters |
| `0x568c30`, `0x56ac70` | Window object (HWND first), window flags |
| `0x4cd00c` | Native frameskip byte |
| `0x473185` → `0x4728a0` | FPS/slowdown and replay-related post-present bookkeeping |
| `0x473367` → `0x453f40` | Screenshot call / stdcall filename routine |

The runner layout remains `next +0x50`, `ending +0x54`, list head `+0x18`,
node argument `+0x24`. Return 8 ends the pass. The native runner is left intact,
including the exit/shutdown call at `0x471fbf` and thprac's terminal-RET hook site.
Only the selected frame's call is replaced by a wrapper that skips updates when the
shared scheduler owes none. Catch-up selects context 2 and runs the native runner,
preserving its 0/-1 exit results and cleanup behavior.

The automatic-latency branch must also be redirected to `0x471c37`: patching the two
ordinary frame calls alone misses an entire default timing path. Both selected calls
then use the shared scheduler and always draw through `0x472fd0`. The optional native
Sleep guard is disabled. Native frameskip is temporarily zero during that frame and
restored afterward. FPS/slowdown bookkeeping and screenshots run only after native ticks,
not once per extra presentation. The shared swap-chain hooks own presentation pacing.

## Geometry and reuse

`0x481210` draws an ANM VM (manager in ECX, VM pushed). `0x47dce0` constructs/clips
ordinary 2D quads. It first writes the **native** corners into VM+`0x4f0`, then calls
`0x47e800` at `0x47e6b5` to copy temporary vertices into the triangle batch.
At that call ECX is the manager, EDI is the VM, and the sole stack argument is the
vertex buffer (`0x570520` in this path). Four vertices have stride 28; position is the
first three floats. The patch changes those temporary positions for the copy and
restores them afterward; the VM's cached native corners and logical object positions
are never replaced by interpolated positions.

This catches embedded player/bullet VMs and manager-owned sprite VMs. The ordinary
bullet draw at `0x424eb0` copies bullet position `+0x638` to its VM at bullet+`0x28`
(VM position `+0x5f0`) before calling `0x481210`. Player draw `0x45cac0` likewise
copies position `+0x620` to its VM at player+`0x14`; its other writes set draw flags.

VM fields checked in the script/draw code:

- script ANM slot `+0x1c`, sprite ANM slot `+0x20`, sprite index `+0x24`;
- script number `+0x28`, instruction offset `+0x2c`, layer `+0x18`;
- script timer integer `+0x550`; reset to zero by script initialization;
- flags `+0x534/+0x538`, native corner cache `+0x4f0`;
- ANM manager `0x51f65c`, 33 file slots at manager+`0x312072c`.

`src/backends/fixed_quad.h` is the geometry/history code extracted from TH08. Both
adapters use the same translation, rotation and scale interpolation, teleport/gap/script
reset handling, and cooldown for VMs reused for several glyphs or sprites. TH08 keeps
its own diagnostic prediction census. TH18 keeps a bounded 16,384-entry history.
No game addresses were added to shared runtime code.

Shared dimming wraps the draw dispatch and VM draw. Initially only background and item
sliders are offered: item passes have priorities 19 and 33; bullets use 38. Other
categories need an ANM/layer census before offering controls that may fade the wrong object.

## Gameplay map for the next phase

| Address / layout | Finding |
| --- | --- |
| `0x4ccbf0` | Native game-speed float (ANM VM slow-motion override references it) |
| `0x4b35c0` | Timer rate pointer table; timers carry an index, as in TH15 |
| `0x4cf298` / `0x4cf2a4` | Ability manager / market pointer |
| `0x4cf2e4` | Game thread; flags at +0xb0, mask 5 pauses bullet update |
| `0x4cf2bc` | Bullet manager |
| `0x423af0` | Bullet-manager allocation/registration |
| `0x424e70` | Bullet update callback, priority 29; skips pause/shop, increments +0xa8 |
| `0x424c50` | Bullet-manager update body, builds per-kind draw lists |
| `0x423e10` | Individual bullet update |
| manager+`0xec`, stride `0xfa0`, 2001 slots | Embedded bullet array; active lists +0xbc/+0xc0 |
| bullet+`0xf68`, `0xf6c/+0xf70/+0xf74` | State; previous/integer/float lifetime timer |
| `0x4cf410` | Player pointer; update `0x45caa0`, priority 23 |
| `0x4cf2d0` | Enemy manager pointer |
| `0x4cf2ec` | Item manager; update `0x446ec0`, priority 30 |
| `0x4cf418` | Replay manager; no extension installed |
| `0x4cf280` / `0x4cf288` | RNG objects (ANM and gameplay call sites respectively) |
| `0x402740`, `0x4027d0` | Integer / floating random-number functions |

A full simulation port still needs an audit of every card-dependent movement/damage/graze
path, integer countdown, shot cycle, timer equality event and RNG consumer. Merely labeling
these callbacks MODE_SUB would change the game. First validate this presentation-only
baseline, then add any gameplay sub-stepping as a separately tested opt-in feature.
The non-quad laser and stage-camera draw paths are the next visual targets.

## Validation

The native harness maps the executable as inert data. It verifies every frozen signature,
refuses modified fixtures, validates the complete transactional patch plan and import hooks,
and runs the shared scheduling/geometry regressions. TH18 clock tests replace native callees
with generated RET stubs and check exactly 60 updates at 60/144/165/240/360 Hz, skip behavior,
and catch-up exit results. Unicorn checks the actual emitted quad thunk's ECX/EDI arguments,
stack cleanup, return value, latency-path redirection and preservation of the native runner.
These checks do not substitute for the pending hardware playtest.

Result: TH18 complete inert suite passed; TH08 and TH15 native harnesses passed. The existing
TH08 Unicorn script stalled and was stopped; that regression check remains unresolved.
The first build is deployed to the requested installation, with automatic display rate and
debug logging enabled. Original EXE hash is unchanged; installed launcher identification passes.
See [TH18_HANDOFF.md](../TH18_HANDOFF.md) for exact resume state and deployed DLL hash.
