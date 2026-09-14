# TH06 New Classic: feasibility and reverse-engineering notes

Research and prototype date: **2026-09-13**. Status: **experimental x64 prototype,
owner-tested gameplay and F11 menu; broader compatibility validation remains open**.
Initial research reviewed `c33dacc` / `e0f3f91`; implementation continued on the merged
v0.4.12-test checkout (`d09a86e`). Sections 1–12 preserve the original findings;
[section 13](#13-experimental-prototype-2026-09-13) records implementation, validation,
installation and remaining work.

**This document is a running record, so its sections are dated and earlier ones are not
corrected in place** — where a later section overturns an earlier one it says so, and the
earlier one carries a banner pointing forward. Read it back to front if you want the current
state. As of v0.5.2-test that state is: **27 frozen signatures, 16 patch sites, 7 guard
ranges, 3 dimming rules and 2 dimming pools** (`src/games/th06nc.c`); sub-tick player
movement, sub-stepped bullets and sub-stepped lasers, each off by default; items and enemies
deliberately at 60 Hz; and nothing that disables any of it during replay playback. Counts
quoted inside a dated section are the counts of that day.

## 1. Decision

This is an HFR target requiring a new backend, **not a thin adapter for the x86 runtime**.
There is useful structural similarity: separate priority-ordered update and draw lists,
ANM animation scripts, an identifiable bullet pool, and separate player callbacks.
However, this executable is **AMD64/PE32+, uses DxLib with Direct3D 11 in the observed run,
and advances important gameplay and animation timers with integer increments**.
The current x86/D3D9 DLL, code emitters, fractional-speed patches, and replay integration
cannot be installed by substituting addresses.

Recommended first implementation: a separate x64 engine/render backend in the same project,
keeping authoritative simulation at 60 Hz and interpolating visual state at higher refresh
rates. Share settings, menu content, scheduling mathematics and filter algorithms after
isolating their platform dependencies. Treat sub-tick input and fractional gameplay as a
later, separately validated feature. Presentation interpolation alone would not provide
the high-rate gameplay/input behavior of today's TH10–13 patch.

The initial native experiments below observed normal execution with temporary debugger
breakpoints. They did not change the cap or installed executable. The later prototype
in section 13 changes in-memory rendering/pacing behavior and has been owner-tested;
it retains the native simulation and leaves executable files unchanged.

## 2. Product and inspected build

The [Steam product page](https://store.steampowered.com/app/4659620/Touhou_Koumakyou_New_Classic__the_Embodiment_of_Scarlet_Devil/)
lists September 9, 2026 as the release date, 64-bit Windows and DirectX 11 requirements,
and a bundle containing separate **New Classic** and **Classic** versions. It describes
remade music and graphics. This investigation concerns New Classic, not the 2002 executable.

Inspected installation:


| Property | New Classic | Bundled Classic |
| --- | --- | --- |
| Executable size | 5,539,328 bytes | 4,039,168 bytes |
| Machine | `0x8664` AMD64 | `0x8664` AMD64 |
| Optional header | PE32+ | PE32+ |
| Preferred image base | `0x140000000` | `0x140000000` |
| Image size | `0xc6b000` | `0xbb3000` |
| Entry RVA | `0x2bd700` | `0x284290` |
| DLL characteristics | `0x8160` | `0x8160` |
| `.pdata` runtime-function entries | 4,367 | 3,664 |
| Version resource | none found | none found |

SHA-256:

```text
th06nc.exe
07850c8c6e469c0e82c13423e6d0d096a88d693455bdacacbb44c0aa3bcce473

th06c.exe
1e2f280ee8ede3018aabbd72897a46684957ee18442a89c3e2d6194c06b628fd
```

Classic was fingerprinted only. Its 64-bit architecture already rules out reusing an
original-TH06 x86 binary patch, but its gameplay/backend details were not investigated.

### New Classic sections and files

| Section | RVA | Virtual size | Purpose inferred from PE flags/content |
| --- | --- | --- | --- |
| `.text` | `0x1000` | `0x2bea91` | executable code |
| `.rdata` | `0x2c0000` | `0x626ba` | constants, imports, strings |
| `.data` | `0x323000` | `0x906660` | writable globals; extensive zero-initialized storage |
| `.pdata` | `0xc2a000` | `0xccb4` | x64 unwind ranges |
| `.rsrc` | `0xc37000` | `0x322b0` | resources |
| `.reloc` | `0xc6a000` | `0x9a8` | relocations |

The data directory contains `th06CM.dat`, `th06ED.dat`, `th06FN.dat`, `th06IN.dat`,
`th06MD.dat`, `th06ST.dat`, `th06TL.dat`, and two sets of `th06_01.opus` … `th06_18.opus`
under `bgm`/`bgm2`. Config files are `th06.cfg` and `th06.env`. Archive decoding and config
format decoding were not attempted. Embedded strings still name resources such as
`data/etama.anm`, `data/stg1enm.anm`, `data/ecldata1.ecl` and `data/frame.anm`.

## 3. Address convention and evidence quality

**All addresses below are RVAs**, unless explicitly called a VA. At runtime use:

```text
address = actual module base + RVA
```

ASLR is active. One probe loaded at `0x7ff65a7d0000`, not the preferred `0x140000000`.
Do not truncate pointers or reuse the current patch's `0x400000` arithmetic.

Evidence labels:

- **Static:** instructions, PE data, or decompilation checked against the binary.
- **Native:** a short startup/menu run of an isolated copy under an x64 debugger.
- **Candidate/inferred:** a useful semantic label that still needs gameplay validation.

Ghidra 11.3.2 with JDK 23 imported the executable as x86-64, completed auto-analysis in
185 seconds, and the existing `tools/ExportAll.java` exported 2,730 non-external functions;
two exports reported decompilation failure. The function count differs from `.pdata`
because unwind entries can describe fragments, and leaf functions may have no unwind entry.
Ghidra found no PDB information. Automatic names such as `FUN_140010870` are analyst labels,
not recovered source symbols.

**Do not trust decompiler types without checking instructions.** In particular it sometimes
renders a timer as a float array element cast to `int`, then casts the increment back to
`float`. The machine code below loads/increments/stores an integer; there is no float
conversion at those sites.

## 4. Rendering backend: DxLib and D3D11

Static evidence includes UTF-16 `DxLib` at RVA `0x2c4cf0`, DxLib-style graphics API wrappers,
and dynamic resolution of `D3D11CreateDevice` and `CreateDXGIFactory*`. The executable also
contains D3D9/9Ex support strings. Their presence does not identify the active renderer.

The [official DxLib source package](https://dxlib.xsrv.jp/dxdload.html)
(`DxLibMake3_25a.zip`) was downloaded for comparison. Relevant files are
`Windows/DxGraphicsAPIWin.cpp`, `DxGraphicsWin.cpp`, `DxGraphicsWin.h`,
`DxGraphicsD3D11.cpp` and `DxSystemWin.cpp`. The source's API selection constants, dynamic
loading and presentation wrappers match the inspected paths. This identifies the library
family; **the exact statically linked DxLib version has not been established**.

| RVA | Meaning | Evidence |
| --- | --- | --- |
| `0x270020` | wrapper resolving/creating the D3D11 device | static; called once per native probe |
| `0x270089` | reference to ASCII `D3D11CreateDevice` at `0x2cebd0` | static |
| `0x2707b0` | DXGI factory creation wrapper | static; references `CreateDXGIFactory2` |
| `0x8fe20c` | graphics API selector | native value `2`; DxLib's D3D11 enum is `2`, D3D9 is `1` |
| `0x953e0` | thunk to `0x928b0`, matching DxLib `ScreenFlip` path | static; called by frame finalizer |
| `0x23d540` | graphics backend dispatch used by that path | static |
| `0x259a40` | selected D3D11 screen-flip implementation | static dispatch on selector `2` |
| `0x25a1f1` | swap-chain `Present` call site | static and native |
| `0x50a204` | DxLib `NotWaitVSyncFlag` equivalent | static; determines sync interval at that call |
| `0x8fe3b0 + index * 0x118` | swap-chain pointer slots used there | static; output-index lifetime not fully mapped |

At `0x25a1f1`, RCX contains the swap-chain pointer, EDX the sync interval, R8D the flags.
The native trace saw calls returning to `0x25a1f6` with **EDX=1, R8D=0**. Both `d3d11.dll`
and `dxgi.dll` loaded; no `d3d9.dll` load was observed. Along with the selector and device
creation breakpoint, this confirms D3D11 in the tested configuration.

There are **two pacing constraints** to investigate for HFR: this synchronized Present and
the independent software frame wait in section 5. Setting Present's interval to zero would
not remove the software deadline. Conversely, bypassing only the software wait leaves
synchronized presentation. See Microsoft's
[`IDXGISwapChain::Present` documentation](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present)
for the interval's behavior.

### Important trap: shared COM thunks

The call above goes through RVA `0x26fff0`, whose entire body is a slot-8 vtable dispatch
(`mov rax,[rcx]; jmp [rax+0x40]`). **It is not exclusively Present.** Another observed caller
returned to `0x243284` with a pointer in RDX, and other static callers exist. This is consistent
with compiler folding of identical COM wrappers. A global Present-specific hook at this
thunk would also intercept unrelated methods. Hook the verified call site or the actual
swap-chain interface, and establish object lifetime before using a vtable hook.

The existing `src/backends/d3d9.c`, D3D9 state management, additional swap chain, texture
upload hooks and menu renderer cannot operate on these D3D11 interfaces. A D3D9 fallback
was not forced or tested; even if available, it would still be x64 and would not solve the
integer simulation problem.

## 5. Main loop and frame pacing

Normal path in the main-loop region:

```text
0x45d82  call update runner 0x3be80
          conditional fast-forward/audio bookkeeping paths
0x45e91  call draw runner   0x3bf70
0x45e96  call finalizer     0x3c330
0x45e9e  process messages/check termination, then loop
```

The update runner is also called at `0x45dc6` in a conditional path. Thus these are **not**
unconditionally one-update/one-draw semantics in every mode. Preserve or explicitly model
fast-forward and transitions. The exact user-facing trigger of that path is not fully mapped.

`0x3c330` draws edge/letterbox regions as needed, calls `0x953e0` (ScreenFlip), then performs
deadline waiting and FPS bookkeeping. It also performs later game bookkeeping. Replacing
the whole routine with a no-op would drop more than the frame cap.

The pacing setup at `0x3c750` creates a high-resolution waitable timer through
`CreateWaitableTimerExW` (flag `2`), installs clock/sleep function pointers and computes:

```text
target_period_ticks = max(1, int(clock_frequency / 60.0 + 0.5))
spin_margin_ticks   = min(target_period_ticks / 2,
                          int(clock_frequency * 0.00102))
```

| RVA | Meaning |
| --- | --- |
| `0x30ce38` | read-only double `60.0`, used by pacing initialization |
| `0xc220d8` | waitable timer handle |
| `0xc220e0` | current-clock function pointer, initialized to `0x3c270` |
| `0xc220e8` | second clock-related wrapper pointer, initialized to `0x3c280` |
| `0xc220f0` | sleep function pointer, `0x3c290` if timer creation succeeds |
| `0xc220f8` | clock frequency |
| `0xc22100` | rounded target period in clock ticks |
| `0xc22108` | configured rate as double (`60.0` in observed initialization) |
| `0xc22118` | spin margin |
| `0xc22120` | pacing initialized flag |
| `0xc22128` | clock origin |
| `0xc22130` | deadline sequence counter |
| `0x3c290` | sleep helper using SetWaitableTimerEx and WaitForSingleObject |
| `0x3c488`–`0x3c5a1` | deadline calculation, timed waits and final spin |
| `0x3c652` | FPS formatter's string reference (`%.02lffps` at `0x30c9e8`) |

The finalizer derives deadlines from origin plus period times sequence number. When behind
by more than a period it resets its origin/sequence. It sleeps while enough time remains,
then spins with `pause` instructions until the deadline. Initialization has a timer-creation
failure path that leaves the sleep pointer null. FPS formatting samples over roughly
half a second and is distinct from the authoritative gameplay clock.

Native values: frequency **10,000,000**, period **166,667**, rate **60.0**, spin margin
**10,200** ticks. This is direct evidence of an approximately 60 Hz software cap, independent
of the store description and the user's observation.

## 6. Update/draw lists

| RVA | Role |
| --- | --- |
| `0x3be80` | update runner |
| `0x3bf70` | draw runner |
| `0x4f1440` | update-list sentinel |
| `0x4f1400` | draw-list sentinel |
| `0x126e0` | remove/unlink node helper |
| `0x3beb8` | indirect update callback call |
| `0x3c038` | indirect draw callback call |

Nodes allocated by the inspected constructors are **0x40 bytes**:

| Offset | Observed field |
| --- | --- |
| `+0x00` | signed 16-bit priority |
| `+0x02` | flags byte; dynamic-node constructors set bit 0 |
| `+0x08` | 64-bit callback pointer |
| `+0x10` | on-register callback pointer, cleared after invocation |
| `+0x18` | cleanup callback pointer |
| `+0x20` | previous node |
| `+0x28` | next node |
| `+0x30` | self/ownership-related pointer; full semantics not established |
| `+0x38` | callback argument pointer, passed in RCX |

The constructors insert by ascending priority. Update return values observed statically:
`0` removes the node; `1` continues; `2` repeats that callback; `3` ends this walk normally;
`4`/`5` set distinct outer-loop results; `6` restarts at the sentinel. List exhaustion also
depends on the runner's callback count. The draw runner repeats on `2`, removes on `0`,
and stops on `3`/`4`/`5`. These details belong in a backend with dedicated validation, not
an unchecked cast to the existing `UpdateFunc` struct.

The update runner clears a flag at `0xc21970` and calls `0x76a30` on its exit paths. The
latter drains a queued sound list in the inspected code; it should not be mistaken for
an input-poll function or repeated casually for extra rendered frames.

Selected mappings (semantic names inferred from resource use and code):

| System | Registration | Update / priority | Draw / priority | Argument |
| --- | --- | --- | --- | --- |
| ASCII/text | `0x9400` | `0x90d0` / 1 | `0x9260` / 19; `0x92a0` / 13 | static `0x3de620` |
| Main game state | `0x3a810` | `0x3a210` / 4 | `0x3a7f0` / 3 | static `0x4f1e60` |
| Player | `0x67dc0` | `0x68820` / 7 | `0x6a130` / 7; `0x6a210` / 9; `0x6a430` / 11 | static `0x4ff3a0` |
| Enemy manager candidate (`stg*enm.anm`) | `0x37170` | `0x373b0` / 9 | `0x38290` / 10 | static `0xaa1e90` |
| Effects (`eff*.anm`) | `0x2b490` | `0x2b280` / 10 | `0x2b310` / 12 | static `0xa6ecd0` |
| Enemy bullets/lasers | in `0x3a9c0` | `0x10870` / 11 | `0x11940` / 14 | static `0x3ec2a0` |

**Observed at runtime (§20).** The map above was built statically and is incomplete. Read
from the game's own lists while it ran, the title/menu screen registers:

```text
update list: 0@79c70  1@90d0   2@47940  3@75af0
draw   list: 0@54870  13@92a0  18@7a330 19@9260  20@76280
```

`0x79c70` is the input update (the only caller of the device poll `0x12be0`). `0x47940`, a
thunk into `0x479e0`, and `0x54870` are the update and draw of the **screen manager**, both
switching on a screen state at `object+0x168b0`; `0x479e0` is a 22 KB state machine covering
every menu screen. `0x7a330` is a fade/transition overlay. The runtime can print these lists
itself -- see `diag_seconds` in §20 -- which is far more reliable than inferring them.

This is a starting map, not a complete classification of every node or permission to
sub-step any of these callbacks. Pause, menus, replay, stage transitions and object
lifetimes still need native gameplay traces.

## 7. Why the existing fractional-speed approach does not transfer

### Bullets and lasers

In the ordinary active-bullet path of `0x10870`, the instructions at `0x1102c` onward perform
**position += velocity** with SSE scalar additions, without a time multiplier. At `0x11562`:

```text
load integer timer from bullet+0x2c
store previous timer at bullet+0x28
increment integer by one
store current timer at bullet+0x2c
```

Acceleration, turning, state transitions, collisions, graze and animation updates are in
this same callback. Repeating it six times at 360 Hz would repeat those behaviors; scaling
only a position addition would leave its integer events six times too frequent.

> **Superseded by §16.** True as stated -- repeating the callback unchanged does repeat those
> behaviours -- but it is not the obstacle it reads like. Each of those blocks is relocated
> behind a gate and stands aside on a sub-step pass, so the callback can be repeated with only
> motion, culling, grazing and collision live. Bullets and lasers are sub-stepped today.

Useful layout from the update/draw loops:

| Field | Location relative to bullet entry |
| --- | --- |
| Pool start | manager `0x3ec2a0` + `0x08` |
| Capacity / stride | `0x280` (640) / `0x620` bytes |
| Velocity XYZ | `+0x08`, `+0x0c`, `+0x10`, float32 |
| Previous/current age | `+0x28`, `+0x2c`, int32 |
| Position XYZ | `+0x30`, `+0x34`, `+0x38`, float32 |
| Angle | `+0x40`, float32 |
| State | `+0x44`, uint16; zero skipped |
| Embedded animation VMs | selected by state: `+0x50`, `+0x170`, `+0x290`, `+0x3b0`, `+0x4d0` |

The same manager has a 64-entry laser loop with a `0x298` byte stride. Its age/state and
geometry also advance inside `0x10870`. A complete laser layout has not been validated.

The draw callback at `0x11940` copies bullet positions into the selected VM's `+0xc8` and
`+0xcc` fields, sets Z/rotation/color, and calls sprite drawing. This is a promising place
to supply interpolated render coordinates without changing authoritative collision positions.
It also handles lasers and item-related visuals and **writes state**, including conditional
VM initialization. Re-running the draw list cannot yet be assumed side-effect-free.

### Player and animation

Player state is at RVA `0x4ff3a0`; its update is `0x68820`:

- Position XYZ: player `+0x7730/+0x7734/+0x7738` (absolute RVAs `0x506ad0/4/8`).
- Previous/current player-state timer: `+0x7854/+0x7858`, int32.
- Player-facing current/previous input words: globals `0xa6ec60/0xa6ec64`.
  The update tests focus bit `0x04` and a rising bomb bit `0x02`. The complete input producer,
  replay override path and edge semantics remain to be mapped.

The ANM interpreter/update candidate at `0x69b0` advances integer timers directly. At
`0x7451` it increments VM `+0x28`; at `0x7459`–`0x7469` it copies VM `+0xb8` to `+0xb4`
and increments `+0xb8`. These are different from the later engines' fractional timers.
Generic sprite draw entry points include `0x67f0`, `0x4dc0` **and `0x36c0`** (§20 -- the
third one draws the menus and was missed for a long time precisely because this sentence
said "include" and nobody checked); their full state mutation
and batching contracts are still unverified.

No common fractional-speed global was established. That is a scoped negative finding,
not proof that the entire executable has no speed controls. The verified direct increments
are enough to rule out applying the current timer/speed-site table unchanged.

## 8. Replay findings

The existing TH10–13 `t10r`/`t11r`/`t12r`/`t13r` handling does not describe this format.

| RVA | Finding |
| --- | --- |
| `0xa6ec38` | replay-manager pointer |
| `0x6b220` | replay setup/registration; mode-specific callbacks |
| `0x6bfd0` | recording initialization; allocates a `0x78` byte header |
| `0x6c1b0` | playback initialization and stage state restore |
| `0x6af60` | file load, decoding, validation and offset relocation |
| `0x6b110` | checksum helper called by loader |

The initialized header magic is **`T6RP`** (little-endian `0x50523654`), with a 16-bit
version **`0x010f`** at `+0x04`. The loader checks both. It decodes bytes from file offset
`0x13` onward by subtracting a rolling byte initially read at `+0x12`, increasing the rolling
value by seven each byte. It checks a checksum at `+0x0c`, then relocates seven nonzero
64-bit stage offsets beginning at `+0x40` into pointers. The on-disk save path and complete
record layout have not been audited; these observations are not a finished format spec.

Resource strings use `replay/th6_00.rpy` and `replay/th6_%.2d.rpy`. Reusing this filename
pattern does not establish compatibility with original TH06 replays. No cross-version or
HFR replay playback was tested. Do not append the current patch's USER metadata or alter
recording cadence based on the filename. Keeping native simulation/input at 60 Hz is the
best initial way to preserve replay semantics, subject to determinism testing.

## 9. Native experiments and limits

A copy of the New Classic directory was made under `../analysis/nc/native/`. An x64 MinGW
debugger helper launched that copy with `DEBUG_ONLY_THIS_PROCESS`, obtained the relocated
base from `CREATE_PROCESS_DEBUG_EVENT`, counted temporary software breakpoints, restored
each displaced byte for a single step, and rearmed it. It ended only its own child after
approximately ten seconds of frame activity. No keyboard/gameplay input was sent.

| Observation | Probe 1 | Probe 2 (additional COM dispatch breakpoint) |
| --- | --- | --- |
| Update runner hits | 601 | 602 |
| Draw runner hits | 601 | 601 |
| Frame-finalizer hits | 601 | 601 |
| First-to-last finalizer span | 10,000 ms | 9,984 ms |
| D3D11 creation wrapper hits | 1 | 1 |
| Shared COM slot-8 thunk hits | not instrumented | 620, including unrelated calls |
| Graphics API selector | not sampled | 2 (D3D11) |

The count includes both endpoints: probe 1 spans 600 frame intervals in ten seconds.
Probe 2's extra update at cutoff is not evidence of a steady 602:601 ratio. These are
startup/menu observations under debugger overhead, not a frame-time benchmark. Neither
probe reported a second-chance exception; a first-chance `0x406d1388` thread-name exception
was passed to the application normally. Both child processes were stopped after observation.

**Not tested:** actual stages, all shot types, bosses, bombs, pause/resume, unlimited-lives
mode, replay determinism, alt-tab/fullscreen/resizing, forced D3D9, higher presentation rates,
or any fractional gameplay patch. The installed game directory was not patched. Original
executable hashes were rechecked after the experiments.

## 10. How to maximize reuse

Keep one repository and user-facing launcher, with architecture-specific runtime DLLs and
capability-specific backends. A launcher may select an x86 or x64 helper after PE inspection;
one x86 DLL cannot serve both processes.

| Existing component | Reuse assessment |
| --- | --- |
| `core/timing.c`, `limiter.c` | Reuse the scheduling/phase and pacing mathematics; separate them from current globals and fractional-simulation assumptions. A 60 Hz simulation / R Hz presentation mode needs its own history/phase policy. |
| `core/interpolation.c` | Reuse interpolation policy, not its x86 call shim or enemy linked-list traversal. Supply game-specific snapshot/draw accessors and object identity. |
| Configuration, logging, filter registry | Share after auditing pointer formatting and backend-specific settings. |
| `ui/menu.cpp` and `ui_api.h` | Share menu content and settings interface; split D3D9 types/init/render calls out of the interface and use a D3D11 renderer. Expose only supported capabilities. |
| Shader/filter mathematics, `texscale.c` algorithms | Potentially share kernel/source generation and CPU filters. D3D11 shaders, resource bindings, compilation targets and texture hooks need separate implementations. |
| `backends/update_runner.c` | Add an engine backend for the x64 node layout and return/cleanup rules; do not add game-ID branches throughout the shared runner. |
| `backends/d3d9.c`, scaler state/resource handling | Add D3D11/DXGI implementation, with resize/device-loss/resource-lifetime handling. |
| `core/x86.c`, speed/site helpers | Architecture/engine-specific; x87 and x86 stubs are not reusable machine code. |
| `identity.h`, `launcher.c`, `core/patch.c` | Add PE32+/ASLR support, architecture-matched injection, pointer-width handling and x64-safe detours. Preserve signature preflight/transaction behavior. |
| `core/replay.c` | Keep common policy separate from a new format parser; existing USER/chunk offsets do not transfer. |
| Dimming classifications | Callback-priority concept can carry over; rules, batch boundaries and sprite classification require new validation. |

Concrete existing blockers include `identity.h` requiring PE32/i386 and image base `0x400000`,
`node_class.func` and callback comparisons using `uint32_t`, the enemy list traversal using
32-bit pointers, D3D9 types in the UI boundary, and relative branches cast to `int32_t`
without a reach check. Recompiling the existing unity build as x64 would not fix these.

For x64 hooks, preserve the actual ABI: register arguments, shadow space, stack alignment
and nonvolatile registers. Relocate RIP-relative instructions correctly; use checked rel32
branches with nearby relays or an appropriate longer detour. Account for unwind metadata
when introducing non-leaf generated code. See Microsoft's
[x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170).
Dear ImGui already supplies an [upstream D3D11 backend](https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_dx11.cpp),
so the F11 menu need not be rewritten from scratch; use a version matching the vendored ImGui.

## 11. Next implementation steps and acceptance criteria

> **Status, as of §20.** 1 and 2 are done (§13). 3 is done and then some: the player and the
> projectiles are not interpolated but genuinely sub-stepped (§14, §16, §18). 4 is partly done
> -- position, rotation and scale are smoothed for every sprite, across all three draw entry
> points (§19, §20) -- with animation frames, colour fades and 3D backgrounds still untreated.
> 5 has the menu but none of the video features (see `TH06NC_VS_TH10_13.md`). 6 was attempted
> and its conclusion is §18: the systems whose discrete effects are pinned to 60 Hz cannot gain
> from sub-stepping, so "fractional gameplay" is complete for the systems where it means
> anything. The acceptance criteria below have **not** been met -- in particular no
> native-versus-patched comparison over identical replays has been run.


1. **Confirm the release build and build an x64 observation runtime.** Freeze verified
   instruction signatures by RVA. Log native update/draw/Present counts and callback
   membership through stage entry, pause and replay. Establish actual device/context,
   swap-chain and output lifetimes. Keep the supported TH10–13 runtime passing its checks.
2. **Prove independent presentation.** Keep native updates at exactly 60 Hz; account for
   the conditional extra-update path; replace only the appropriate wait behavior and
   control DXGI synchronization. First count 120/144/240/360 presents with 60 simulation
   steps per second. Duplicate images are an instrumentation milestone, not finished HFR.
3. **Interpolate player and ordinary bullets at draw submission.** Capture positions at
   simulation boundaries, identify slots across births/deaths/reuse, and use a well-defined
   history phase. Interpolation between two known states introduces up to one simulation
   frame of visual delay; prediction reduces that delay but introduces errors around turns,
   collisions and teleports. Make that choice explicit. Never feed visual positions back
   into collision, item collection, aim calculations, RNG or replay recording.
4. **Audit and cover remaining visuals.** Enemies, lasers, player shots, options, items,
   effects, ANM motion, scrolling background and UI need treatment. Gate draw-triggered
   initialization, snapshot/restore only understood state, and reset histories at scene
   transitions, teleports and slot reuse. Verify repeated draws do not advance gameplay.
5. **Integrate the shared menu/video features.** Add D3D11 rendering and capability-aware
   settings; test native scaling interactions before reusing window/texture hooks. Validate
   F11, resize, fullscreen, alt-tab and device/resource recreation independently of HFR.
6. **Consider fractional gameplay only after that works.** Map every integer timer family,
   scripted events, collision update, input edge, shot/bomb cadence and replay tick. The
   current dyadic scheduler is useful mathematics, but changing all affected update
   semantics is substantially larger than a presentation port.

Before claiming support: run stages with every character/shot type, compare fixed-input
gameplay state and RNG/replay results against native 60 Hz, test bosses/lasers/bombs/item
collection/pause/deaths, and cover awkward display rates such as 144 Hz as well as integer
multiples of 60. Test recording/playback and both normal/unlimited-lives modes. A numeric
FPS counter or a smooth title screen is insufficient evidence.

The most useful later assistance from the owner is a Steam-verified executable fingerprint,
short stock replays for determinism checks, and gameplay testing of the first interpolation
prototype. The first testable build is now described in section 13; replay equivalence
testing remains outstanding.

## 12. Reproducing and continuing the investigation

The read-only tool supports both i386 and AMD64 and requires Python, `pefile`, and `capstone`.
From the repository root:

```powershell
$ncExe = 'C:\Program Files (x86)\Steam\steamapps\common\th06nc\'
python tools/porting/inspect_pe.py $ncExe info --imports
python tools/porting/inspect_pe.py $ncExe strings 'DxLib|fps|D3D11CreateDevice|replay'
python tools/porting/inspect_pe.py $ncExe xrefs rva:0x30c9e8 rva:0x3c330
python tools/porting/inspect_pe.py $ncExe xrefs rva:0x4f1440 rva:0x4f1400
python tools/porting/inspect_pe.py $ncExe disasm rva:0x1102c rva:0x11060
python tools/porting/inspect_pe.py $ncExe disasm rva:0x11562 rva:0x1156d
python tools/porting/inspect_pe.py $ncExe disasm rva:0x25a1d1 rva:0x25a1fe
```

The import inventory shows the waitable-timer APIs in KERNEL32; their IAT RVAs are
`0x2c0058` (SetWaitableTimerEx), `0x2c0060` (CreateWaitableTimerExW), and `0x2c0050`
(WaitForSingleObject). Graphics creation uses dynamic loading, so the absence of a D3D11
import descriptor is not evidence against D3D11.

Ghidra reproduction, using your own Ghidra/JDK paths and a private output directory:

```powershell
$env:JAVA_HOME = 'C:\Program Files\Java\jdk-23'
& '..\toolchain\ghidra_11.3.2_PUBLIC\support\analyzeHeadless.bat' `
    '..\analysis\nc' nc -import $ncExe -scriptPath "$PWD\tools" `
    -postScript ExportAll.java "$PWD\..\analysis\nc\decomp.c"
```

That import command assumes a fresh project; for an existing project use Ghidra's process
workflow instead of overwriting research. No original-game x86 function-repair script was
applied. Use AMD64 decoding, `.pdata`, cross-references, and caller context; do not assume
an unwind fragment is a complete source function or a folded COM thunk is one API.

For native reproduction, use a **copy** of the game and an x64 debugger. Resolve the module
base, break/count at `+0x3be80`, `+0x3bf70`, `+0x3c330` and `+0x270020`, and inspect the
pacing globals after initialization. For presentation, use call site `+0x25a1f1`, or filter
the shared `+0x26fff0` thunk by return address `base+0x25a1f6`. Record RCX/EDX/R8D and the
API selector at `base+0x8fe20c`. Avoid interpreting all shared-thunk hits as presents.

Local working files retained outside git, relative to this checkout:

```text
../analysis/nc/pe_inventory.json     initial inventory of both executables
../analysis/nc/nc.gpr + nc.rep/      saved Ghidra project
../analysis/nc/decomp.c             private decompilation export
../analysis/nc/headless.log         analysis/export completion record
../analysis/nc/ghidra.log
../analysis/nc/strings.txt           initial broad scan; includes large shader blobs
../analysis/nc/dxlib/               selected official source files for comparison
../analysis/nc/probe.c + probe.exe  local x64 debugging experiment, not a patcher
../analysis/nc/native-probe.log     initial cadence observation
../analysis/nc/native-probe-2.log   API selector and shared-thunk experiment
../analysis/nc/native/             disposable copied game used for observations
../toolchain/DxLibMake3_25a.zip      official source archive used for comparison
```

Additional small function extracts are retained alongside these files. Paths are conveniences
for this machine, not required inputs to the released patch. The hash/RVA maps and commands
above are the portable record. **Do not commit game executables, copied game assets, full
disassembly, or decompilation exports to this repository.**

Tool validation performed: PE32+ inventory and unchanged hashes for both New Classic and
Classic; PE32 inventory against the installed TH12; bounded ASCII/UTF-16 string results;
the known FPS-string and finalizer-call cross-references; equivalent RVA/VA disassembly
of the integer bullet timer; and rejection of a range in non-file-backed zero-initialized
storage. Python syntax, document links/code fences, and `git diff --check` passed. No
production runtime changed during that research-only phase; prototype validation follows.

## 13. Experimental prototype (2026-09-13)

### Result and scope

> **This section describes the state at v0.4.13-test and is kept as the record of the first
> prototype. It is no longer current.** Sub-tick player movement (§14), sub-stepped bullets
> and lasers (§16, §18), rotation and scale smoothing (§19) and the third sprite entry point
> (§20) all came later. Where this section says the prototype "does not implement fractional
> gameplay or sub-tick input", read §14 onwards.

Implemented `touhou_hfr64.dll` and `touhou_hfr64.exe` in this repository. The normal
`touhou_hfr.exe` detects the verified New Classic executable and dispatches to the
x64 helper. No separate source fork or second menu is needed. Executable files are
never patched on disk; the launcher injects into its own suspended child process.

The prototype provides high-rate presentation, the original update callbacks at
60 Hz, position interpolation for persistent VMs submitted through `+67f0` / `+4dc0`,
and the existing F11 settings menu rendered with D3D11. Presentation-rate,
interpolation and D3D11 VSync controls save into the common INI. Diagnostics count
actual render/update calls and check selected gameplay fields around native drawing.

This is **presentation interpolation**, with up to one native frame of additional
visual delay. Input, shooting decisions, collisions, script events, RNG and native
replay processing remain in the original update callbacks. It does not implement
fractional gameplay or sub-tick input. Animation-frame selection, rotation, lasers,
scrolling/3D backgrounds and paths bypassing the two sprite entry points do not have
complete interpolation coverage. (There turned out to be *three* such entry points, not two
-- §20.) Pixel-art scaling, sharpening, internal resolution,
texture magnification, dimming and custom window controls are not ported. Use the
game's native display settings for now.

### Code reuse and source map

| File | Responsibility |
| --- | --- |
| `src/fixed_identity.h` | One verified-build registry shared by the x86 dispatcher, x64 helper and DLL |
| `src/games/th06nc.c` | Hash, image size, 14 frozen signatures, RVAs, VM offsets and draw-guard ranges |
| `src/backends/fixed_game.h` | Fixed-clock engine profile schema; RVAs are relocated at runtime |
| `src/hfr64.c` | x64 installation, native relays, draw submission, pacing and diagnostics |
| `src/backends/fixed_clock.h` | Elapsed-time 60 Hz scheduling independent of the graphics API |
| `src/backends/fixed_history.h` | Renderer-independent position histories and discontinuity handling |
| `src/core/patch.c` | Existing transactional code-patch implementation, compiled by both runtimes |
| `src/core/file_hash.h` | SHA-256 verification shared by the registry's callers |
| `src/ui/menu.cpp`, `ui_api.h` | The same menu content and runtime interface for both architectures |
| `src/ui/menu_key.h` | Existing dual-source F11 handling extracted from the D3D9 window code |
| `src/ui/menu_renderer.h`, `menu_dx9.cpp`, `menu_dx11.cpp` | Small renderer adapters around matching ImGui backends |
| `src/ui/overlay_dx11.cpp` | DxLib swap-chain overlay and temporary render-target binding |
| `src/ui/overlay_fixed.c` | Fixed-clock settings/status and capability responses |
| `src/launcher64.c` | Same-architecture injection and initialization outside the loader lock |
| `build64.ps1`, `test64.ps1` | Separate x64 objects, build and regression entry points |

The TH10–13 scheduler and gameplay/replay code remain as they were; those semantics
cannot be shared by changing pointer widths. New clock/history components can be
reused for other fixed-clock ports. Native loop relays still describe this engine
family's calling/register contract: a different engine may need different relays
while reusing the scheduler, history, menu and renderer. Adding an executable to
this family takes a profile and an entry in `fixed_identity.h`, not three independent
launcher/runtime detection lists.

### Exact pacing hooks and newly discovered bookkeeping

All addresses in this section are RVAs relative to the ASLR module base.

| Site | Treatment |
| --- | --- |
| `45d82` | First update call: execute `3be80` on a 60 Hz boundary, otherwise return `0x100` |
| `45dc6` | Preserve the native extra-update call when replay fast-forward reaches it |
| `45d8b..45d97` | Relocate post-update instructions; skip audio/fast-forward bookkeeping on extra render iterations |
| `45e91` | Draw wrapper calls original `3bf70` and compares selected gameplay fields |
| `3c45c`, 22 bytes | Replace software wait with the patch's pacer and two continuation branches |
| `3c5a1` | Resume native FPS/statistics/pending-ANM bookkeeping on a real update iteration |
| `3c71d` | Resume the native epilogue directly on a presentation-only iteration |
| `25a1f1` | Hook the verified Present call site, never the shared COM slot-8 thunk |
| `50a204` | DxLib `NotWaitVSyncFlag`, kept consistent with the selected Present interval |

A correction to a naive two-call-site gate: code after the first update also examines
replay fast-forward state and advances/seeks an audio cursor. Suppressing only calls
to `3be80` would repeat that bookkeeping at presentation rate. The post-update relay
checks AH after the original AL exit test. `AH=1` branches straight to the draw call.
On a real update, the C wrapper clears upper return bits and the relay reproduces
`mov ebx,[base+509690]`, `xor sil,sil`, `mov edi,r13d`, then resumes at `45d97`.
The native path can execute up to seven extra updates when fast-forwarding; those
still advance the logical history generation. Native first-update exit codes retain
their original AL behavior.

The scheduler uses QPC elapsed time, not a ratio based only on requested FPS.
Otherwise requesting 360 FPS while VSync presents 60 would reduce simulation to
10 updates/sec. One native update is permitted per outer iteration; debt exceeding
four native frames is discarded after a long stall/loading operation. Severe stalls
therefore retain slowdown rather than running an unbounded update burst. Changing
presentation rate preserves the clock's next simulation deadline.

The finalizer's original frame counters, slow-percentage accounting, pending ANM
loads and `c21974` countdown execute on real update iterations only. Its native FPS
display should remain near 60. The patch log reports presentation FPS. The pacer uses
QPC and a high-resolution waitable timer when available, with a short final spin.
D3D11 VSync is off by default in `[fixed60]`, independently of x86 `[hfr] vsync`.
Auto currently reads the primary display's refresh rate; an explicit rate is available.

### Sprite history and lifetime

Each entry is keyed by VM address and script base (`VM+f8`), recording logical tick,
integer age (`VM+b8`) and previous/current XYZ (`VM+c8`). A bounded 16,384-entry table
probes up to eight slots; exhaustion skips interpolation. Histories reset on script
replacement/restart, absent logical ticks, address reuse, nonfinite values, or motion
exceeding 64 game units in one native tick. Temporary VMs on the current thread's
stack are excluded. Different positions submitted through one VM in one logical
tick invalidate its history to avoid treating text/layout scratch objects as entities.

Two additional verified entry hooks invalidate histories at ANM script starts:

- `2760`: start script by index; resets VM position/timers and assigns `f0/f8`.
- `2a20`: start script by pointer; assigns `f0/f8` and resets integer age.

`2980` is **sprite selection/matrix setup**, not the script-lifetime initializer.
Do not use it as a spawn-generation hook: ANM selects sprite frames without creating
a new entity. The two script-start hooks also cover reinitialization to the same
script/age at the same address, which pointer/age comparison alone cannot detect.

Draw hooks capture current native position once per logical generation, interpolate
previous→current using elapsed phase, call the original function, and restore all
twelve position bytes when the patch temporarily changed them. A nesting guard prevents
`67f0`→`4dc0` from interpolating twice. Authoritative bullet/player world positions are
never replaced. Rotation/scale/color history and full render-path coverage remain open.

### Installation and x64 mechanics

Both launcher and DLL require the exact supplied SHA-256. The DLL additionally checks
AMD64, image size and every frozen code signature before installing anything. Unknown
builds and pre-existing instruction changes fail closed. Section 2's limitation about
the local installation's provenance still applies; no Steam API files are changed.

[MinHook v1.3.4](https://github.com/TsudaKageyu/minhook/releases/tag/v1.3.4) supplies
instruction relocation for four function-entry detours. Unmodified source and license
are under `third_party/minhook`; `UPSTREAM.md` records the archive URL and SHA-256.
The added ImGui D3D11 backend is from the same
[v1.91.8 tag](https://github.com/ocornut/imgui/tree/v1.91.8/backends) as the vendored core.

Call-site relays are allocated within rel32 reach and changed from RW to RX before
use. Their tail jumps do not alter RSP or nonvolatile registers. Native call sites
supply shadow space and existing unwind frames for compiled C callbacks. Every
relative displacement is range-checked. The shared patch queue verifies code writes
and memory protections before committing six call-site/block patches. Entry detours
are enabled only after preflight; a later failure removes them, and the launcher
terminates its suspended child on any startup failure. No partially initialized game
is resumed. The overwritten wait block's trailing partial instruction is unreachable;
all new continuations land on verified instruction boundaries.

`DllMain` only remembers the module and disables thread notifications. The helper
injects `LoadLibraryA`, waits, obtains the **full 64-bit** DLL base through a module
snapshot, then calls exported `hfr_start` on a separate remote thread. Do not treat
`GetExitCodeThread` as an HMODULE: its result is only 32 bits. Snapshots retry
`ERROR_BAD_LENGTH`; the first startup attempt exposed that race. The game's primary
thread remains suspended until initialization reports success.

### Shared F11 menu and cursor fix

The overlay obtains device/window from the actual swap chain, creates a backbuffer
view for the current presentation, then releases both view and backbuffer. It saves
all output-merger targets/depth view; ImGui preserves the remaining pipeline state.
No retained backbuffer prevents native resizing. Device/window changes rebuild the
overlay. Input uses the shared key state machine to avoid double toggles from both
messages and polling. The Display tab explains which features are unavailable.

The owner's first test found F11 functional but the mouse invisible: DxLib hid the OS
cursor despite `SetCursor`. The fixed backend now requests ImGui's software cursor
only while the menu is open (`UI_SOFTWARE_CURSOR`). D3D9 retains its existing behavior.
The owner confirmed that the cursor is visible/usable and interpolation seems to work
well after the fix.

### Build, package and configuration

```powershell
.\build.ps1       # common launcher and existing x86 games
.\build64.ps1     # x64 DLL/helper, separate build/obj64 directory
.\test64.ps1 -GameExe 'C:\Program Files (x86)\Steam\steamapps\common\th06nc\'
.\package.ps1 -Version '0.4.12-th06nc-prototype' -IncludeExperimental64
```

The optional package contains both architectures, source, shaders and third-party
licenses. Copy `touhou_hfr.exe`, `touhou_hfr64.exe`, `touhou_hfr64.dll` and
`touhou_hfr.ini` beside `th06nc.exe`, preserving an existing INI, and run
`touhou_hfr.exe`. Do not copy the x86 `dinput8.dll` proxy into New Classic. The x64
build has no proxy-loading path; launching the native executable directly runs the
unmodified game. Bundled Classic is unsupported.

```ini
[hfr]
fps=0
[fixed60]
interpolate=1
vsync=0
[video]
menu_key=122
```

`fps=0` follows the primary display; explicit targets clamp to 60–1000. F11 → Timing
changes presentation/interpolation; F11 → Presentation changes D3D11 VSync. Save writes
these keys without replacing unrelated settings. Simulation-substep controls did not exist at
this point; §14 and §16 added `[fixed60] subtick` and `substep`, and the Timing tab offers
both.

### Validation and limits of evidence

- Copied-game runs measured approximately **144 presentations/sec / 60 updates/sec**.
  The owner changed settings during a later run that measured approximately **360/60**.
  Logs contain moving-sprite submissions and nonzero interpolation counts. The owner
  reported that the game runs fine, F11 works, the cursor fix works and interpolation
  seems to work well. This is useful gameplay feedback, not an exhaustive stage audit.
- The draw guard stayed `ok` in the observed runs. It hashes bullet-manager header,
  the first `0x50` bytes of all 640 bullets, player position/state/shot timers, RNG and
  current/previous input. VM render caches are excluded. A mismatch logs the failure
  and switches to 60 FPS without interpolation. This covers **selected state**, not
  every field, and does not establish deterministic replay equivalence.
- `test64.ps1` checks 60/120/144/165/240/360/480/1000 scheduling, blocked presentation,
  phase bounds, long stalls, births/reuse/teleports/gaps/multiple submissions, and the
  actual patch transaction against an inert synthetic image. Unicorn executes both
  branches of emitted AMD64 post-update/wait relays, checking registers/stack balance.
- All 14 signatures match the supplied file. Both launchers accept its hash and reject
  a non-PE and temporary copy with a changed update-call byte. The source executable
  is hashed again afterward. Regression tests do not launch a game process.
- TH10/11/12/13 native harnesses, machine-code regressions and launcher checks passed
  after the shared-menu extraction. Builds emit existing ImGui array-bound warnings
  associated with this repository's nonterminating assertion handler; the new runtime
  compiles successfully.

Private local evidence in `../analysis/nc/` includes native baseline logs,
`prototype-144-startup.log`, `prototype-144-first-menu.log` and subsequent cursor-fix
logs. They are observations, not build fixtures. Do not distribute copied game assets.

Before a full supported release, compare native versus patched gameplay state using
the same inputs/replays; cover all characters, shots, bosses, lasers, bombs, deaths,
items, pause, replay recording/playback and fast-forward. Explicitly test resizing,
fullscreen, alt-tab and multiple monitors. Expand the state guard and audit
draw-triggered changes. Next extend visual coverage and port D3D11 video using shared
shader/menu code. Fractional gameplay: the player's motion and input went to the display
rate in §14, bullets in §16 and lasers in §18, and §18 explains why the systems left at
60 Hz cannot gain from following them.

## 14. Sub-tick player movement (2026-09-13)

### Why the player, and only the player, first

Section 7 established that this engine has no fractional-speed float: motion and discrete
events share one callback, and every timer is an integer increment. Running the update list
N times with a scaled speed — the TH10–13 design — would therefore run scripts, collisions,
spawns and RNG N times as well. That remains true and is still the blocker for general
sub-stepping.

The player is the one object where that does not apply. Its per-frame displacement is
produced by a single site, is derived only from the input word and four speed constants,
and nothing else in the frame depends on *when* within the frame it happened — the hit test,
the shot origin and the item magnet all read the player position once, at the native tick,
and they still do. So the player's motion can be taken over completely without touching a
single discrete event, which is what this section implements.

What it buys, concretely: input is sampled once per drawn frame rather than once per 60 Hz
frame, and a direction change is acted on within the frame it is made. At 360 Hz that is
six samples per frame and up to 2.8 ms of input latency instead of 16.7 ms. Holding one
direction still covers exactly the stock distance per 60 Hz frame.

What it does not buy: enemies, bullets, lasers, items, effects, scripts, collisions, graze,
shot cadence and RNG all still run at exactly 60 Hz, unchanged. This is not general
high-rate gameplay, and it is not a step toward it that can be extended object by object —
see "Why bullets cannot follow" below. **(That last clause was wrong: bullets went to the
display rate in §16 and lasers in §18, by gating the callback rather than adding slices on
top of it. Enemies, shots, items and effects do still run at 60 Hz, and §18 says why that is
deliberate.)**

### The site and the accounting

| RVA | Meaning | Evidence |
| --- | --- | --- |
| `0x69388`, 24 bytes | `mulss xmm6,[rdi+0x7710]; movss [rdi+0x78a0],xmm7; mulss xmm7,[rdi+0x7714]` — the two multiplies that turn a held direction into this frame's step | static; relocated whole |
| `0x693a0` | resume: `addss` of the products into the position, then the native clamp | static |
| `0x4ff3a0` | player object | section 7 |
| `+0x7710`, `+0x7714` | per-axis step scale the game applies to the chosen speed | static |
| `+0x7730`, `+0x7734` | position X, Y | section 7 |
| `+0x789c`, `+0x78a0` | facing, stored *before* both multiplies and consumed by the animation triggers | static |
| `+0x7860`, `+0x7864` | straight speed, unfocused and focused | static |
| `+0x7868`, `+0x786c` | diagonal speed, unfocused and focused | static |
| `0x4ff0e0` | playfield clamp: min X, min Y, width, height as four floats | static |
| `0x12be0` | the device input poll, returning the input word in EAX | static; sole caller `0x79c70` |
| `0x4f27b2` | ~~non-zero while a replay is driving the input word~~ **WRONG, see §17**: it is set during ordinary play, so it cannot mean playback in progress | was: static, at `0x6ba40`, which §17 wrongly called dead code |

Input bits confirmed at the movement switch: `0x04` focus, `0x10` up, `0x20` down,
`0x40` left, `0x80` right. Right beats left and up beats down, exactly as the native
switch orders them; focus and diagonal select one of the four speeds above.

The relocated site gains `mulss xmm<n>,[rip+factor]` after each of the two multiplies, plus
a `mov byte [rip+ran],1`. Both operands live within rel32 reach of the image, because the
DLL's own globals may be further away than that. The reservation near the image is therefore
two pages, not one: the first holds emitted code and is dropped to `PAGE_EXECUTE_READ` once
installation finishes, the second holds these two words and stays `PAGE_READWRITE`. Putting
them on the code page instead is what crashed the first build of this feature — the factor is
written on every native tick and the ran byte by the relay itself, so the first tick after
install faulted, with the log ending right after the D3D11 menu came up. Neither the Unicorn
test nor the transaction test caught it: Unicorn maps everything writable and has no notion of
page protection, and the transaction test checked the emitted bytes without ever writing
through the pointers or executing the relay. The transaction test now reads back the
protections `VirtualProtect` actually applied and writes through both pointers. The
relay is a leaf: it stores, it never touches RSP or a nonvolatile register, and it jumps
back to `0x693a0`. **With the feature off the factor is `1.0f`, so the relocated site is
arithmetically identical to the original instruction stream and the game is bit-identical
to stock.** That is the property the default depends on.

Time is counted in frames as `tau = completed native ticks + phase`. On every presented
iteration the pass applies `(tau_now - tau_last) x displacement(input polled now)` and
advances `tau_last`, whether or not it is in charge; when it is in charge the native factor
is `0.0f` and the native step applies nothing. Between two native ticks the slices
therefore sum to exactly `1.0` frames of time, and the remainder of a frame is applied at
the next native tick, after the native update, with input polled there. Turning the feature
on or off mid-play costs at most one frame of player movement and cannot double-apply,
because `tau_last` tracks the clock in both states.

The pass stands aside, leaving the native step at `1.0f`, whenever its premises fail:
the feature is off, the presentation rate is 60, a replay is driving the input word, the
draw guard has already failed, or the movement site did not run on the previous native
tick. That last condition is what makes pause, menus, dialogue, death and stage
transitions safe without enumerating them: the byte the relay stores is set only when the
game itself decided to move the player, so if the game stops moving the player, so does
the pass, one frame later.

### Sprites are predicted, not interpolated, while it is on

Interpolation draws every sprite one native frame behind, which is self-consistent while
everything on screen is equally late. Once the player is authoritative and current, a bullet
drawn a frame behind is a frame of bullet travel away from where it really is, exactly at
the distance where that matters. So while sub-tick movement is on, `fixed_pose` continues
past the current native position instead of approaching it (`predict`), which for an object
whose velocity only changes at native ticks is the position sub-stepping would have
produced. The player's own VM is unaffected either way: its position changes within a
native tick, which the history already treats as a reason to leave it alone.

This is not offered as a separate user setting. It is the partner of sub-tick movement and
follows it; with sub-tick movement off, the interpolation path is exactly as section 13
left it.

### Replays

The native replay stores one input word per 60 Hz frame. Sub-tick movement puts the player
somewhere that word cannot describe, so a replay recorded with it on will not play back
faithfully — the divergence is real, not cosmetic. Recording a sub-tick run honestly would
mean carrying the sub-frame input stream in a sidecar and reading it back on playback,
which the replay format work in section 8 has not reached. Until then the setting is off by
default, the menu says so, and score runs should leave it off.

> **Correction (§17).** This section originally said the feature "disables itself while
> `0x4f27b2` is set, so native and previously recorded replays play back correctly". That was
> wrong twice over: the byte does not mean what it was thought to mean, and because it is
> non-zero during ordinary play the guard silently disabled the whole feature instead. There
> is **no automatic guard against replay playback**; both features must be turned off by hand
> before watching a replay.

### Why bullets cannot follow

> **Superseded by §16.** The argument below is sound about the design it describes -- adding
> slices on top of an unchanged callback -- but its conclusion is wrong. Bullets *are*
> sub-stepped now: the callback is called again with the motion scaled and the 60 Hz blocks
> gated, so collision follows the motion instead of fighting it. Read §16 before acting on
> anything here.

The obvious next step — sub-step bullets the same way — does not work, and the reason is
worth recording so it is not attempted twice. In `0x10870` the per-bullet motion is
immediately followed, in the same loop body, by the off-screen cull, the player hit test
and the graze test, all reading the position the motion just produced. Scaling the native
motion to a fraction so a sub-step pass can own the rest would run those tests against a
bullet that has only partly moved, which changes when bullets kill and graze. Keeping the
native motion whole and adding sub-steps on top would double the travel. The only correct
version sub-steps the collision too, which is a gameplay change that needs its own design
and its own validation, not an extension of this one. The same argument applies to lasers
and to the item magnet.

Recorded for a future attempt: bullet states and their per-frame motion are
1 (`pos += vel`), 2, 3 and 4 (`pos += vel/2`, `/2.5`, `/3`, at `0x110f0` onward) and 5
(`pos += vel*0.5`); the pool is 640 entries of `0x620` bytes at manager `0x3ec2a0 + 8`,
velocity `+0x08`, position `+0x30`, state `+0x44`, and the 64-entry laser loop follows it.

### Configuration and validation

```ini
[fixed60]
subtick=0     ; sub-tick player movement; inert at 60 Hz. NOTHING disables it during
              ; replay playback -- see §17 -- so turn it off by hand before recording or
              ; watching one.
```

F11 → Timing toggles it live and explains the replay consequence; the interpolation toggle
is disabled while it is on, because sub-tick movement supplies the smoothing. The stats
line gains `subtick on|standing by: N input polls (rate), longest X ms, M player moves` —
the poll cost is logged because the device poll is the game's own and is called once per
drawn frame; if it turns out to be expensive at high rates, that line is where it will show.

`test64.sh` / `test64.ps1` cover: slices summing to one frame and standing aside when
unarmed, stalled or moving backwards; the full direction table including right-over-left,
up-over-down, focus and both diagonals; the clamp; prediction against interpolation in the
pose history; and the emitted movement relay executed in Unicorn, checking that the factor
scales both axes, that the facing store survives, that the ran byte is set and that no
register or stack slot is disturbed. The patch transaction test now commits seven patches.
There is now a POSIX `build64.sh`/`test64.sh` as well, and the profile test runs the
launchers under Wine when the host is not Windows.

**Not validated:** any of this in the running game. It has never been played. The first
things to check are that the player still stops at the playfield edges, that focus and
diagonal speeds feel stock, that holding a direction for a second covers the same distance
as with the setting off, that pause/unpause/death/bombs do not displace the player, and
that a replay recorded with the setting *off* still plays back byte-for-byte.

## 15. Toward high-rate simulation: anatomy and schedule (2026-09-13)

Section 14 took the player to the display rate and said the same approach would not extend
to bullets. That is still true of *that* approach, but it was an argument about one design,
not about the engine. This section is the map for the design that can work, the scheduler
it needs, and an honest account of what is and is not yet established.

### The shape of the job

TH10-13 run the whole update list N times per displayed frame and keep the 60 Hz behaviour
by two mechanisms: continuous quantities are multiplied by the sub-step's duration, and
discrete blocks are skipped on ticks that are not frame boundaries. New Classic needs the
same two mechanisms. What it lacks is the single game-speed float the later engines hang
the first one off -- confirmed again here: the `xmm15` that multiplies so much of the
projectile update is loaded once at `0x108f8` from `.rdata` `0x30cd58`, and it is the
constant `0.5`, not a speed. So every motion site needs its own factor and every discrete
block its own gate, exactly as TH10 needed (`DEVNOTES_RUNTIME.md` §7).

### The dispatch hook, which makes this per-system instead of all-or-nothing

The update runner `0x3be80` is small and regular. Its per-node body is:

```text
0x3beb0  mov rax,[rbx+8]        ; the node's callback
0x3beb4  mov rcx,[rbx+0x38]     ; its argument
0x3beb8  call rax
0x3beba  cmp eax,2              ; 2 repeats, 0 removes, 3/4/5 end the walk
```

Ten bytes from `0x3beb0` to `0x3beba`, straight-line, with the node in `rbx`. Relocating
them gives a per-callback dispatch gate: on a tick that is not a frame boundary, run only
the callbacks that have been converted to sub-step, and report `1` (continue) for the rest.
Systems can then be converted one at a time instead of the engine having to be correct all
at once. This is the single most useful structural finding in this section.

Two cautions for whoever wires it. The runner's exit path clears `0xc21970` and calls
`0x76a30`, which drains a queued sound list; running the runner N times a frame runs that
N times. And `0x3beb8` is `call rax` -- two bytes -- so the patch has to take the whole
ten-byte range and reproduce the two loads, not just the call.

### The projectile callback, `0x10870`

One callback (`0x10870`, priority 11, argument `0x3ec2a0`) updates bullets and lasers.
`.pdata` splits it into fragments -- `0x10893`, `0x11610`, `0x117b1` are all entries for
what is one function -- so do not trust an entry there as a function start.

Its ordinary per-bullet path is, in order:

| RVA | What happens |
| --- | --- |
| `0x10940`-`0x11021` | the state machine: writes angle `+0x40`, speed `+0x24`, velocity `+0x08/0c/10`, flags `+0x14`, counter `+0x1c`, state `+0x44`. Converges on exactly two points, `0x11021` and `0x1102c` |
| `0x1102c`-`0x1105f` | **the motion**: `pos += vel` on all three axes, no time factor. 52 bytes |
| `0x11060`-`0x110b8` | the off-screen cull, against half-extents from the bullet's own descriptor |
| `0x1113b` | `call 0x6a8c0` -- a pure AABB-vs-16-boxes test on `player+0x7754`; on overlap the bullet goes to state 5 and spawns a cancel effect. No side effects of its own |
| `0x1115a`-`0x111df` | **graze**: circle test against player position `0x506ad0/4` with radius `bullet*0.5 + 20.0 + 0x506aec`, then the counters at `0x4ff0cc` (capped 99999) and `0x4ff0d0` (capped 999999) |
| `0x11562` | the timer: `[+0x28] = t; [+0x2c] = t+1` |

**Graze is once per bullet.** `+0x618` is set at `0x11277`/`0x112d2` and tested at
`0x110ba` before the graze test runs. Repeating the callback within a frame therefore
cannot inflate graze, which removes the hazard that looked worst from the outside.

**A coarse gate does not work, and it is worth saying why.** The state machine has a single
entry and converges on two points, which invites "on a minor tick, jump from the entry to
`0x11021`". It does not survive contact: the later code reads `xmm6`, `xmm7` and `xmm8` --
the bullet's radius and position -- which the skipped region establishes. Gating has to be
per-site, with the register effects of each skipped block accounted for.

### Where the player actually dies, and an open question

All writes of the dying state `0x506c38 = 2` go through one function, `0x6aba0`, which is
called from exactly one site, `0x11883`, inside this same callback. `0x6aba0` rotates the
player's position into the projectile's frame (`sin`/`cos` at `0x2be07d`/`0x2be071`) and
tests the player's radius `0x506aec` against a rotated box: it is the **laser** test.

That leaves an open question this session did not settle (**answered in §16: it is
`0x6a980`, called from `0x11418`, and the address scan missed it because it reads the
player's hitbox radius through a register**): the death test for ordinary bullets was not
found. The player's hitbox radius `0x506aec` has only two readers -- the
graze test and this laser test -- so ordinary bullets must reach the player through some
other comparison. Until that is found, nobody should claim to know what sub-stepping does
to bullet collision. Finding it is the first task of the next session, and the most likely
places are the box list at `player+0x7754` (16 entries of `{w,h,cx,cy}`, filled
dynamically, no static writer) and the part of the callback between `0x11610` and
`0x117b1` that this session did not read.

### The schedule (`src/backends/substep.h`), which is done and tested

The sub-step schedule is the piece that could be finished and verified now, because it is
pure arithmetic with no game state in it.

The x86 runtime's schedule lets a tick straddle a frame boundary: the engine's float timers
accumulate the step and cross the integer frame count somewhere inside a tick, and "major"
just means the first tick that started in a new frame. That is wrong here. This runtime
decides itself when the 60 Hz logic runs, so a straddling step would apply part of the next
frame's motion before that frame's logic had run. The test caught this on the first run --
the steps between two boundary ticks summed to 1.25 frames at 144 Hz, not 1.

So the partition is exact instead. One Bresenham deals the second's R ticks out to its 60
frames; a second deals each frame's 256 units (1/256 of a frame, so every step and every
partial sum is exact in float32) out to that frame's ticks, ceiling-first so the last tick
lands exactly on the boundary. At 144 Hz frames get 2 or 3 ticks; at 60 every frame gets
one tick of exactly one frame, which makes the whole mechanism inert there.

`test64.sh` checks, at 60/120/144/165/240/360/480/1000: exactly 60 boundary ticks and
exactly R ticks per second, every step a whole number of 1/256 frames, every frame's steps
summing to exactly 1.0, phase strictly increasing within a frame and 0 on a boundary, and
no drift over a whole second.

### What is not done

No new code patch was added for any of this. The dispatch gate is designed and its hook
point verified, but it is not emitted, because an eighth patch that no system yet uses is
risk without benefit. Nothing in the shipped runtime behaves differently from section 14.

The order of work from here: find the ordinary-bullet death test; emit the dispatch gate;
convert the projectile callback site by site (motion at `0x1102c` scaled, then each discrete
block gated, with the register effects of each accounted for); then the enemy, item and
effect callbacks. Each conversion needs its own frozen signatures and its own Unicorn test,
and none of it should be enabled by default until a native-versus-patched comparison over
the same replay shows the same outcomes.

## 16. Sub-stepped bullets (2026-09-13)

Section 15 left the projectile work mapped but not done, and one question open. Both are
answered here: bullets now advance a fraction of a frame at a time, with their culling,
grazing and collision evaluated at every step.

### The open question, answered

The death test for ordinary bullets is `0x6a980`, called from `0x11418` inside the
projectile callback. Section 15 could not find it because it reads the player's hitbox
radius as `[rbx+0x774c]` off the player pointer in `rcx`, not through the absolute address
`0x506aec` the xref scan was looking for. **A register-relative access to a known global is
invisible to an address xref scan** -- worth remembering, because the same scan is what
produced the (wrong) conclusion that only two places read that radius.

`0x6a980` takes the player, the bullet's position and its size, and returns non-zero on
contact: first `0x6a8c0` (the bullet-cancel boxes at `player+0x7754`, returning 2), then a
circle test of `min(w,h)*0.5 + player+0x774c` against the distance to `player+0x7730`. It
runs only for bullets whose graze flag `+0x618` is already set, which is a neat
optimisation -- nothing can hit you that has not first come close enough to graze.

Because that test lives inside the callback, **sub-stepping the callback sub-steps player
death**. This is the part that makes the feature a gameplay change rather than a smoothing
trick: a bullet fast enough to jump from one side of the player to the other between two
60 Hz frames is not tested against the player at all in the stock game, and now is. It
makes the game harder.

### How it runs

The callback is not re-entered through the update runner; the dispatch gate designed in
section 15 was not needed and is not emitted. On a sub-step pass the runtime calls
`0x10870` directly with the manager `0x3ec2a0`, having set a flag that five relocated
blocks test. Everything that must stay at 60 Hz stands aside:

| RVA | Block | On a sub-step pass |
| --- | --- | --- |
| `0x10982` | the state switch, and with it the whole state machine to `0x11021` | skipped: jumps to `0x11021` |
| `0x11298` | the off-screen frame counter `+0x604` | not incremented |
| `0x11562` | the per-bullet timer `+0x28`/`+0x2c` | not advanced |
| `0x11604` | the laser loop's head | skipped: jumps to the epilogue at `0x11740` |
| `0x11796` | the manager's own frame counter, and the byte that records a full pass | not advanced |

What still runs is the motion at `0x1102c`, the off-screen test, the cancel-box test, graze
and the hit test. The motion block is rewritten rather than relocated: each axis loads the
velocity, multiplies it by the step's length and adds it to the position. With the feature
off the length is `1.0`, and multiplying a float by one is exact, so the result is
bit-identical to the original three adds -- checked against awkward values (2^24, 0.1+0.2,
denormals) in the Unicorn test, not just asserted.

The accounting is the same τ bookkeeping as the player's: the slices of a frame sum to
exactly one frame, and the native pass contributes none of it (its factor is 0). So at
every 60 Hz boundary the bullets are exactly where the unmodified game would have put them,
and the state machine, the spawner and everything downstream read stock positions. The
finishing slice is applied at the top of the next boundary, before that frame's logic, so
the last position tested each frame is the true end-of-frame position.

Three properties made this far safer than it first looked:

- **Graze is once per bullet** (`+0x618`), so testing it six times a frame cannot inflate
  the counter, the score or the effect spawns.
- **No animation advances on the ordinary path.** The VM work at `0x11434`-`0x11543` is the
  cancel effect, reached only when the hit test fires and the bullet's state changes, so it
  cannot repeat.
- **Nothing branches into any of the six patched ranges.** That was checked by decoding
  every branch in `.text` before a single byte was written, and is what makes relocating
  them legitimate.

Only the first five bytes of the 52-byte motion block are replaced, because one patch
carries at most 32 bytes; the remaining 47 are unreachable for the same reason. Both halves
are frozen so the whole block is still verified before anything is patched.

### What it does not do

Lasers, enemies, items, effects, the player's own shots and every script still run at
60 Hz. Bullets fired by an enemy still appear on frame boundaries; they just fly smoothly
and hit accurately once they exist.

Interpolation needs no special handling: `fixed_pose` already invalidates a VM whose
position changes within a native tick, which is exactly what a sub-stepped bullet does, so
those sprites are drawn where they are rather than smoothed from stale poses.

Toggling the setting off midway through a frame gives that one frame the slices it had
already taken plus a whole native step -- at most one extra frame of bullet travel, once.
Toggling it on costs nothing, because the accounting is reset so the frame in question
keeps the whole-frame step it already had.

Positions accumulate across slices instead of being computed in one multiply, so a bullet's
position can differ from stock by a few units in the last place. It is far below any hitbox
and unbiased, but it is not bit-identity, and a replay recorded with the feature on will not
play back faithfully in any case. (This section originally added "the setting disables itself
during playback"; it does not -- see §17.)

### Validation

`test64.sh` executes every emitted relay in Unicorn: the motion at step lengths 1.0, 0.25
and 0 (positions, the descriptor load the cull depends on, the `xmm2`/`xmm1` the cull reads,
and that nothing else is clobbered); the state switch in both directions including that the
zero flag from `sub ecx,r12d` survives to the `je` at its resume; the off-screen counter,
the per-bullet timer and the manager counter each incrementing only on a full pass; the
laser head reaching the loop or the epilogue; and the full thirteen-patch transaction.

**Not validated in the running game.** The things to watch for are bullets moving at the
right speed overall (a wrong factor would show as everything flying at six times speed or
crawling), graze counts matching a stock run roughly, animations not running fast, lasers
behaving normally, and dying to things that used to miss -- that last one is the feature
working, not a bug.

## 17. Why neither feature had ever run, and what the fps readout counts (2026-09-13)

The owner asked whether the in-game fps counter still reading 60 with sub-stepping on was
normal. It is. Checking why turned up something that was not: **sub-tick movement and
sub-stepped bullets had never executed once**, in any build, on any run.

### The fps readout

The counter lives in the frame function at `0x3c5ec`, immediately after `wait_resume`
(`0x3c5a1`):

```text
0x3c5f3  mov ecx,[0xc2232c] ; inc ecx ; store     -- the frame count
0x3c604  rdx = now - [0xc29648]                   -- elapsed since the last recompute
0x3c60b  cmp rdx, 0x7a120 ; jl ...                -- only recompute every 500000 units
0x3c660  call sprintf with "%.02lffps" (0x30c9e8)
```

The patch's wait relay jumps to `wait_resume` on a native tick and to `frame_epilogue`
(`0x3c71d`) on a presentation-only one, so that increment is reached exactly 60 times a
second by construction. The game's readout is therefore a simulation-rate readout and will
say 60 at every presentation rate, whatever the sub-step settings do. Nothing is wrong with
it, and nothing should try to "fix" it: it is the one number on screen that tells you the
60 Hz logic is still keeping time. F11 → Timing now prints the measured presentation and
simulation rates next to it, and says as much.

### The bug the log gave away

The stats block only prints its sub-tick and sub-step lines when those counters are
non-zero. Across 42 stats windows, ~40 seconds of play with `subtick=1, substep=1` and
`guard=ok`, presenting at 360 and 480 -- neither line ever appeared. Both features were
inert, and had been since they were written.

Every term of the two activation tests is provable from the log except one. The rate was
360; the guard was ok; the relay pointers are non-null whenever the patch installed at all,
and it had. That leaves `!*(base + replay_playing)`, the byte at `0x4f27b2` recorded in
section 14 as "non-zero while a replay is driving the input word".

That identification was wrong, and the way it was wrong is worth keeping. It came from four
apparent readers found by `inspect_pe.py xrefs`, three of which are at `0x6b970`, `0x6b9f0`
and `0x6ba40` and do look exactly like replay input handling -- one appends an entry when
the input word changes, one walks a table by frame number and writes the input global.
At the time this section was written it claimed **nothing in the binary reaches any of
them** -- no call, no jump, no `lea`, no image-relative switch-table entry -- and concluded
they were dead code carried over from the original game. *That claim is false, and it was
made with the very tool this section was warning about.* The search for references was
itself a linear decode. Repeating it with `tools/porting/xrefs64.py`, which decodes each
function from its own `.pdata` BeginAddress, finds all three taken by address:

```text
0x6b970  <- lea at 0x6b307, inside 0x6b24f
0x6ba40  <- lea at 0x6b401, inside 0x6b24f
0x6b9f0  <- lea at 0x6b521, inside 0x6b50f
```

Those are node registrations, and the update list dumped from a running stage contains
`15@6b970`, so one of the three executes every frame of ordinary play. They are the replay
recorder and player, both live, exactly as they looked.

The correction does not rescue the original identification; it sharpens the conclusion
below. `0x4f27b2` is set during ordinary play *because* TH06 records every run, so whatever
it marks, it is not playback in progress.

The other reader is the fps display above, where a non-zero value adds an offset to a
drawing position. No writer is visible to an address scan either, which by section 16's
lesson means it is written through a register.

**The A/B settled the rest.** Removing that one term was the only functional change between
the two builds, and it took both features from never running to running at exactly 6.00
sub-steps per frame at 360 Hz. So the byte is non-zero while a stage is running. The
diagnostic then caught it at zero five times -- every one of those from a menu or the title
screen, where `player_ran` and `proj_ran` are also zero because nothing is moving, which is
the features correctly standing aside rather than a fault. Zero in menus, non-zero in a
stage: `0x4f27b2` marks gameplay being in progress, which is also why the fps readout shifts
its drawing position when it is set. It never had anything to do with replays.

Both gates now test only what is verified: the setting, the rate, the relay pointers and
the draw guard. The byte is kept in the profile as `replay_suspect` and printed by a new
diagnostic line, which fires whenever a feature is switched on but did nothing for a whole
stats window and names every term's live value. A feature that is enabled and idle is a
bug, and the log should say so rather than staying silent.

### What this costs, and what it asks for next

Replay playback is no longer guarded, because the flag that was guarding it is set during
ordinary play and so cannot mean playback. Watching a replay with either feature on will now drive the player from
the device as well as from the file, which looks wrong; it cannot damage the replay, since
playback does not write, and both features remain off by default. **Finding the real
playback flag is still open** -- and this time it wants a live check, not an address scan:
the scan is what produced this error, section 16's, and the dead-code claim corrected above.
That the three functions are live is the way in: whichever of them writes the input global
runs only during playback, so the byte its caller tests is the flag.

The lesson for the whole project: a reference found by linear decoding is a candidate, not
a fact, and so is an *absence* of references found the same way. Before a byte is allowed to
gate anything, something reachable has to read it -- and before a function is called dead,
the search for its callers has to have been done with `xrefs64.py`, not `xrefs`.

## 18. Lasers, and where the parity with TH10-13 actually is (2026-09-13)

### Lasers

A laser is a beam from a fixed origin whose head advances and whose tail follows. The head
is at `[rbx]` and grows by the speed at `[rbx+0x258]` (`0x1161d`); the tail at `[rbx-4]`
follows once the gap exceeds the maximum at `[rbx+0x274]`, by the game's own rule, so
scaling that one add sub-steps the entire beam. Its per-laser timer and animation step
(`0x11714`: the timer pair at `[rbx-0xc]`/`[rbx-8]`, then the VM call at `0x1172a`) are
gated like every other 60 Hz block.

The laser loop is no longer skipped wholesale on a sub-step pass -- the gate at `0x11604`
that did so is gone, replaced by these two. Its state machine needs no gate of its own:
every transition is driven by the timer reaching a duration and resets that timer, so with
the timer frozen between native ticks a transition cannot fire twice.

### What that completes

Both ways the player can die are now evaluated at the display rate, and this is the first
time that can be stated with the evidence in hand:

- **Bullets.** `0x6a980`'s circle test, on contact, spawns two effects and writes
  `player+0x7898 = 2` -- the dying state. That field is absolute `0x506c38`, the byte
  section 16 saw being read elsewhere; the write is register-relative off the player
  pointer, which is exactly why no address scan ever found a writer for it. It is guarded
  by `player+0x7898 == 0`, so a second contact in the same frame cannot kill twice, which
  is what makes running the test six times a frame safe.
- **Lasers.** `0x6aba0` writes the same field at `0x6ad3e`, and its loop now runs on every
  sub-step too.

So the set of things that can kill you, graze you or cancel a bullet is fully sub-tick.

### The honest parity check

The remaining systems are enemies, the player's shots, items and effects, and it is worth
recording why sub-stepping them was **not** done rather than leaving it as an open task
someone repeats the analysis for.

TH10-13 interpolate enemy sprites rather than sub-stepping enemies, and this runtime already
interpolates (or predicts) every sprite. For the others, the decisive point is where the
discrete effect lands:

- **The player's shots** are boxes on the player object (`player+0x7754`, 16 of them); their
  motion is at `0x69a36` (`[rbx+0x13c] += [rbx+8]`). The damage they do is applied on the
  *enemy* side, at `0x37a6c`, as `add [enemy+0x234], -0xa` for each overlapping box. Nothing
  consumes the shot there, so that test must stay at 60 Hz -- running it per sub-step would
  multiply damage by the sub-step count. With the damage fixed at 60 Hz and the slices
  summing to exactly one frame, sub-stepping the shot's motion cannot change a single
  outcome. It would move the drawn sprite, which interpolation already does.
- **Enemies** move at `0x374e9` (`[+0xb4] += [+0x1088]`), pool `0xaa1e98`, 256 entries of
  `0x10b0`, active while `[+0xbc]` is negative. Same conclusion, plus a cost: the motion is
  followed by an optional clamp to per-enemy bounds, so a slice pass would have to reproduce
  that clamp or let bounded enemies overshoot and snap back once a frame -- a new visual
  artefact in exchange for nothing.
- **Items and effects** are the same argument again.

So the gameplay-relevant work is complete, and the remaining sub-stepping would buy exact
drawn positions in place of interpolated ones. That is a rendering improvement, worth doing
one day, but it is not what "high tick rate gameplay" means and it should not be sold as
such.

### The one real functional gap

TH10-13 record their sub-tick input into the replay and reproduce it on playback. This
runtime does not: the native format stores one input word per 60 Hz frame, so a run recorded
with sub-tick movement or sub-stepped projectiles on will not replay faithfully, and there
is no guard left that disables the features during playback (section 17). Closing that --
a sidecar carrying the sub-frame input stream, read back on playback -- is the next piece of
real work, and it is what would make these features usable for anything scored.

## 19. The fps readout, and smoothing what is not position (2026-09-13)

### The readout now counts what it claims to

Section 17 explained why the game's on-screen fps stays at 60: its counter at `0xc2232c` is
incremented at `0x3c5f3`, just past `wait_resume`, which a presentation-only iteration never
reaches. Explaining it is not the same as it being useful, so the runtime now increments that
counter itself on every iteration the game does not see, and the readout becomes the
presentation rate.

This is safe because of what the counter is, and that was checked rather than assumed:
`0xc2232c` is read once and written twice, all three inside the frame function `0x3c330`, and
nothing else in the binary touches it. The same check finally confirmed section 17's conclusion
about `0x4f27b2`: **one** reference in the whole executable, the fps display's.

Both facts came from a new tool, `tools/porting/xrefs64.py`, which decodes each function from
its own `.pdata` start instead of decoding sections linearly. Linear decoding misaligns wherever
data or padding sits between functions, which is how it both invented the three dead "replay"
functions of section 17 and missed the real writers here. Any address claim in these notes that
rests on `inspect_pe.py xrefs` alone is worth re-checking with it.

### Rotation and scale are smoothed too

Interpolation only ever moved sprites. Menus, the HUD and a good deal of the game animate by
spinning and scaling instead, so they still stepped at 60 Hz however high the presentation rate
was. The pose history now carries rotation (`+0x9c/a0/a4`) and scale (`+0xe4/e8`) alongside
position, smoothed on the same validity decision, with prediction when sub-tick movement is on.

Two details that matter:

- **Rotation takes the short way round.** A sprite crossing the wrap would otherwise spin
  backwards through a whole turn in a single frame.
- **A sprite that is not rotating comes back bit-identical.** The game picks its rotated draw
  path by testing the angle against zero (`0x6816`, `0x6830`, `0x6894`), so a stray non-zero
  angle would silently move sprites onto a different renderer. With previous equal to current
  the delta is exactly zero and the value is unchanged, which the test asserts.

The field offsets were read off the game rather than taken from section 14's note: `0x67f0`
tests `+0x9c`, `+0xa0` and `+0xa4` against zero to choose the rotated path, and `0x4f41`/`0x5170`
multiply the quad by `+0xe4`/`+0xe8`.

Still not smoothed: animation frames, colour fades and 3D backgrounds. Colour is the next one
worth doing — menu fades are the remaining visibly stepped thing.

## 20. The third sprite entry point, and why menus were never smoothed (2026-09-13)

Section 19 added rotation and scale to the pose history and it made no difference to the
menus. The reason was not the fields. **The menus never reached the sprite hook at all.**

The owner's log said so plainly -- `samples=0` across three windows at the title screen --
and that should have been checked before section 19 shipped rather than after.

### What the game actually registers on the title screen

Read from the game's own lists while it was running (a read-only walk of the two sentinels,
each node validated with `VirtualQuery` before being dereferenced):

```text
update list: 0@79c70  1@90d0   2@47940  3@75af0
draw   list: 0@54870  13@92a0  18@7a330 19@9260  20@76280
```

`0x47940` (a thunk into `0x479e0`) and `0x54870` are the update and draw of the **screen
manager**, both switching on a screen state at `object+0x168b0`. `0x79c70` is the input
update -- the only caller of the device poll at `0x12be0`. `0x90d0`/`0x92a0`/`0x9260` are the
ASCII/text system.

### The find

`0x54870` calls `0x36c0` more than anything else, and `0x36c0` opens by reading `[rdx+0xa4]`
and `[rdx+0xc4]` -- the VM's Z rotation and its flags. **It is a third VM draw entry point**,
taking the same `(manager, vm, flags)` in `rcx`/`rdx`/`r8` as the two the profile already
knew about, and it was never hooked. Section 13 recorded that "generic sprite draw entry
points include `0x67f0` and `0x4dc0`" -- "include" was doing a lot of work in that sentence,
and nobody went back to check whether the list was complete.

Hooking it is a one-line change, because everything else was already right: the pose history
keys on the VM address, and menu VMs are ordinary heap objects. With `sprite_draw_menu`
added, the same title screen goes from **0 sprite calls to about 112 per frame**, and blends
appear exactly where something is animating.

The `depth` guard already handles the nesting: `0x36c0` dispatches into the lower-level
draws, so a sprite can pass through two hooked functions, and the inner one steps aside.

### What would actually finish the job

Hooking the third entry point makes menu sprites smooth in position, rotation and scale. It
cannot make a colour fade or an animation-frame change smooth, because those are decided by the
ANM interpreter at 60 Hz and there is nothing to interpolate between.

TH10–13 solve this differently, and the answer was sitting in their own profiles the whole time:
in all four games `AnmManagerWorld` and `AnmManagerUI` are `MODE_SUB` — the animation
interpreter itself is sub-stepped. Their menus are not smooth because the rendering is
interpolated; they are smooth because the animation genuinely advances a fraction of a frame at
a time.

The equivalent here is New Classic's ANM VM update at `0x69b0`, which §7 recorded as advancing
integer timers directly (`+0x28`, and `+0xb8` copied to `+0xb4`). Sub-stepping it would need the
same treatment as the projectile callback: scale what is continuous, gate what is discrete. It
is the most promising remaining rendering work, and unlike the gameplay systems in §18 it would
actually change what is on screen.

### The lesson, again

This is the third time in this file that a confident-looking record turned out to be a
partial one: live code read as dead (§17), a register-relative access invisible to an address
scan (§16, §18), and now an entry-point list that was never complete. The common thread is
that each was believed because it was written down, not because it had been checked against
the running game.

The two diagnostics added here are the cheap general answer: a per-window sprite accounting
line (calls, skipped, sampled, blended) that makes "this code never runs" impossible to miss,
and the node-list walk that says what the game is actually running right now. Both are
read-only. There is also a `[fixed60] diag_seconds=N` key that runs the game for N seconds
and quits, which is what let this be diagnosed and fixed without the owner at the machine.
It defaults to 0 and should stay there.

## 21. Dimming: the machinery, and the map it still needs (2026-09-13)

Dimming fades what competes with the bullets — background, items, effects, the player's own
shots — so the bullets are the most visible thing on screen. The x86 runtime's implementation
is described at the top of `src/core/dimming.c`; this is the New Classic half.

### What the two runtimes have to do differently

The x86 backend fades at the **Direct3D** level: the sprite manager batches quads and flushes
them as one `DrawPrimitiveUP`, so a draw call on its own says nothing about which object it
belongs to. It therefore wraps the draw dispatch *and* the VM draw, flushes the batch around
each classified VM so its quads are a draw call of their own, and fades the vertex colours (or
`D3DRS_TEXTUREFACTOR`) of that call.

New Classic needs none of that, because this runtime already wraps **every** VM draw — all
three entry points (§20). Fading the VM's own colour before the draw and putting it back
afterwards is the same pattern the position, rotation and scale smoothing already uses, and it
changes the *source* rather than the emitted geometry, so batching is irrelevant. No D3D11
hooks, no flushing.

### The colour, and which byte is alpha

The VM's packed draw colour is the dword at **`+0xec`**. Every draw path copies it byte for
byte into a global draw colour at `0xa6eaf0`: `+0xef`→`0xa6eaf3` and so on down. Twenty-one
writers, no reader that an address scan can see (the consumer takes it through a register, the
§16 trap again).

Which byte is alpha was settled from the writers rather than assumed: the bullet draw does
`or dword [vm+0xec], 0xffffff`, which forces the low three bytes to `0xff` and leaves the
fourth alone, and the ANM interpreter writes `[vm+0xef]` on its own at `0x6b1e`. So **alpha is
the top byte at `+0xef`** and the three colour channels are below it — ordinary `0xAARRGGBB`.

Fading scales alpha only. The x86 runtime scales alpha for most classes and the colour where
alpha would do nothing (additive blending, and the background class, which fades towards
black); this backend cannot yet tell a VM's blend mode, so **additive effects will not fade**
until it can. That is a known gap, not an oversight.

### Knowing what is being drawn

The draw runner's per-node dispatch is the same ten straight-line bytes as the update runner's:

```text
0x3c030  mov rax,[rbx+8]      ; the node's callback
0x3c034  mov rcx,[rbx+0x38]   ; its argument
0x3c038  call rax
0x3c03a  cmp eax,2            ; the return value decides what the runner does next
```

It is relocated to a relay that records the node before the call and forgets it after. The
relay pushes nothing, so the callback sees the stack it always saw, and `mov` sets no flags, so
the `cmp eax,2` still reads the callback's own result. The Unicorn test checks all four of
those: the node recorded during the call, the argument still in `rcx`, one return address on
the stack, and the result preserved.

Nothing branches into the interior of that range. That was checked with the `.pdata`-based
scanner rather than the linear one — and doing so caught that the linear scan had been missing
the `je 0x3c030` at `0x3c03d` entirely. **Every site the runtime already patches was re-checked
the same way; all fourteen are clean**, with branches only to their first byte.

### What is missing: the map

> **Filled in by [§22](#22-items-fell-at-the-tick-rate-and-the-finished-dimming-map-2026-09-13),
> which also replaced the background plan below.** The rule table now has three callback rules
> and two address-range pools, and the background fades by colour rather than by a D3D11 quad.
> What follows is the state at the end of §21 and the reasoning that produced the census.

A class is decided by which draw callback is running, and the rule table currently has exactly
one entry: `0x2b310` → effects, from the registration scan in §6. That is enough to prove the
machinery but it is not the feature. The other classes need the **in-game** draw list, and §6's
static map is not a substitute — §20 is what happens when a static map is trusted.

So the runtime now logs, every stats window, how many sprites each draw callback drew:

```text
sprites by callback: 12@2b310=1840 14@11940=9021 10@38290=402 ...
```

One ordinary play session produces that for a stage, and the node-list log now prints whenever
the registered set *changes* rather than only for the first few seconds, so the same session
also yields the list for every screen it passes through. Filling in items, player shots and the
background is then a table edit, not an investigation.

The menu reflects this honestly: `UI_DIM_CLASSES` is a new read-only bitmask of the classes a
game can actually fade, and the shared menu disables the sliders a game has no rule for and
says why. The x86 runtime returns all of them. Background dimming looks like it will need more
than a rule in any case — the x86 blends a black quad over the viewport before the first
world-priority callback, which here would mean drawing a quad in D3D11 at the right point in
the draw list. (§22: it did not. Scaling the background's colour instead of its alpha does the
same job in one branch, because the background is drawn over the playfield's own fill.)

## 22. Items fell at the tick rate, and the finished dimming map (2026-09-13)

The owner reported the first substantive gameplay fault of the sub-step work: *"the items are
falling very fast, it seems like item gravity is not scaled by delta time."* They were right
about the symptom and, as it turned out, about the cause -- but not about where it lived.

### Looking in the wrong place first

The obvious suspect was the item manager in the update list, `12@3cc50`, which is gated by the
pause flag and calls `0x3fca0` and `0x3eee0`. Both have exactly one caller, `0x3cc50` runs from
the 60 Hz update only, and `projectiles_slice` calls nothing but `0x10870` -- so by that route
items cannot be stepped more than once a frame. `0x3fca0` even *contains* a plausible-looking
fall integration (a 0x120-stride loop clamping `[rbx+8]` toward `[rbx+4]` by ±0.01/0.02), which
is exactly the kind of near-miss that costs an hour. It is the HUD: the 0x120 stride is an ANM
VM, and the surrounding code formats `"BONUS %8d"`.

What settled it was asking the decompilation a question instead of reading it. Items fall, so
some float is accumulated by a small constant every frame; over the whole binary there are only
two places where a float field is incremented by a constant smaller than 0.5:

```text
0x373b0  += 0.3     (the enemy manager, an option's approach speed)
0x42980  += 0.03    <-- one line, and the whole bug
```

`0x42980` is the item pool: 1024 entries of 0x160 at `0xbaf0d8`, position at `+0x10`, velocity
at `+0x1c`, type at `+0x34`, and a sprite VM embedded at `+0x38`. Its motion is

```c
pos += vel;                               /* x, y, z */
if (vel.y >= 3.0) vel.y = 3.0;            /* terminal */
else               vel.y += 0.03;         /* gravity  */
if (pos.y >= 464.0) despawn;
```

with an auto-collect branch that re-aims the velocity at the player at a flat speed of 8, and a
`switch` on the type that awards power, points, lives and bombs. And its single caller is

```text
0x108e7  call 0x42980        -- inside the projectile manager, before it touches one bullet
```

That is the function `projectiles_slice` calls once per sub-step. At 360 Hz items were being
stepped six times a frame: six gravity accumulations, six position steps, six collection tests.
Nothing was wrong with the item code; it was being run five extra times.

### The fix

Items are the "gate the discrete" half of the rule, not the "scale the continuous" half. Their
motion is inseparable from the scoring and collection the same pass performs, and running that
six times a frame is not something a `dt` multiply makes safe. So the whole call stands aside:
`item_call` (`0x108e7`) is redirected to a runtime function that calls `item_update`
(`0x42980`) only when the pass flag is clear. On a native tick items behave exactly as the
unmodified game's do, and `substep ...: N item updates held back` in the log says so.

They do not look 60 Hz, either: each item's sprite is an ordinary VM, so the sprite hook
interpolates its position like everything else.

A guard range over the pool's position and velocity (`{0xbaf0e8, 24, 1024, 0x160}`) was written
and then deliberately removed. The draw guard's job is to prove *rendering* advances no
gameplay state, and nothing in the draw path was shown to leave the item pool alone -- a spawn
from a draw callback would trip it and silently drop the whole patch to 60 Hz. An untested
guard that can disable the mod is worse than no guard. It belongs in a session that can watch a
stage run with it armed.

### What this says about the sub-step audit

§16 enumerated what `0x10870` does and gated each 60 Hz block inside it. Every one of those was
a block *within* the bullet loops. The item call is three instructions into the prologue, before
the loops start, and it was never considered because the function had already been labelled
"the projectile manager". **A callback is not the thing it is named after.** The remaining
sub-step surface should be re-read for the same mistake: what else does a gated function do
before it reaches the thing it was gated for?

### The dimming map, finished

§21 shipped the machinery with one provisional rule. The in-game draw list and the per-callback
sprite census from the owner's play session, read against the decompilation, settle the rest:

| callback | priority | what it draws | class |
| --- | --- | --- | --- |
| `0x78290` | 5 | stage background layers 0 and 1 | background |
| `0x78390` | 6 | stage background layers 2 and 3 | background |
| `0x6a130` | 7 | the bomb/death screen darkener — a filled rect, no sprites | — |
| `0x6a210` | 9 | the player's shot pool, entries of type 1 | player shots |
| `0x6a430` | 11 | the same pool, entries of type 2 | player shots |
| `0x38290` | 10 | enemies | — |
| `0x2b310` | 12 | the 512-entry effect pool | effects |
| `0x11940` | 14 | bullets, lasers **and items** | — |
| `0x3cd20` | 15 | the HUD's digits | — |

The player's shots are one pool of 80 entries at `player+0x420`, stride 0x170, with the VM at
`+0x08` and a type word at `+0x00`; the two callbacks walk it twice, filtering on that word, so
the shots draw at two different depths.

**This was first shipped as two callback rules, and the owner caught it in a minute: it faded
the player.** `0x6a210` does not stop when its loop ends. It goes on to draw the player himself
-- `mov eax,[rdi+0x7730]` (the position `pl_position` names) into `[rdi+0x78c8 + 0xc8]`, the
player's own sprite VM -- and then the focus sprite at `player+0x7898`. Reading a function's
loop is not reading the function. So player shots are a pool too:

```text
0x4ff7c8 + n * 0x170,  n < 80      (player 0x4ff3a0, + 0x420 element, + 0x08 VM)
```

which ends at `0x506ac8`, eight bytes before `pl_position` at `0x506ad0` and well below the
player's own VMs at `0x506c38` and `0x506c68`. That is exactly the fault the owner had already
reported in TH10-12's x86 rules, where fading effects also faded the hitbox, arrived at by a
different route. The lesson is now in `fixed_game.h` where the rule table is declared: a
callback rule is worth only as much as a reading of the *whole* function that says it draws
one thing, and neither of New Classic's two interesting callbacks does.

Two things did not fit the callback-keyed rule table, and both are now handled:

**Items share a callback with bullets.** Fading `0x11940` would fade the thing everything else
is being faded *for*. But the item pool is a fixed array at a fixed address, so its VMs are
identifiable by arithmetic: a new `DimPool` rule matches a VM whose address is `0xbaf110` plus
an exact multiple of `0x160`, under 1024 of them. Pools are checked before callbacks. The same
mechanism is what any future class that shares a callback should use. (`0x11940` reads
`0xbaf1d8` -- entry 0's VM position field, `0xbaf110 + 0xc8` -- which is independent
confirmation of both the pool base and the VM offset.)

**The background will not fade by alpha.** It is drawn over the playfield's own fill, so
lowering its alpha changes nothing visible. The x86 backend has the same problem and solves it
by scaling the *colour* for this one class rather than the alpha, and the x64 runtime now does
the same. This replaces §21's stated plan of blending a D3D11 quad before the first
world-priority callback: no quad, no D3D11 work, one branch in `dim_fade_colour`. If a stage
turns out to draw its background through a blend mode where colour scaling is also inert, the
game's own rect filler at `0x75be0` -- which `0x6a130` uses for the bomb darkener, with the
playfield rect `(236,16)-(620,464)` and `alpha<<24` -- is the ready-made fallback.

New Classic gets no `DIM_SPECIAL`: it has no extra class of its own that competes with bullets
the way TH11's or TH13's do, so the menu shows that slider disabled.

`tools/test_fixed_profile.py` now requires every dimming rule to name a real function entry
(one with a `.pdata` record) and every pool to lie inside the image, so a rule that could never
match cannot be committed silently.

### Postscript: the x64 test suite was never failing

`test64.sh` had looked broken for several sessions -- the harness and both launcher checks
died with `Bad EXE format`, and the checks were skipped on the assumption that the
environment could not run PE32+. It can. The default `wine` on this box is the 32-bit
loader; `/usr/lib/wine/wine64` with its own prefix runs everything. The script now finds it,
and `tools/test_fixed_profile.py` honours `RUN64`, so the full suite -- clock, history,
slices, schedule, the patch transaction, Unicorn execution of every emitted relay, and the
signature and launcher checks -- runs again.

It immediately earned its keep: `test_patches` asserts the exact number of patches the
profile installs, and the item fix made it 16. A count that has to be updated by hand is
precisely the point; a patch appearing without anyone noticing is what it is there to stop.

### Postscript 2: the sliders were built but never shown

The owner opened F11 → Display on the new build and found the same sentence as before:
*"Scaling, filters, dimming and window controls are not available in this experimental
graphics backend yet."* The rule table, the pool, the settings and the INI keys were all
working; `draw_display_section` simply began with

```cpp
if (!hfr_ui_get(UI_VIDEO_AVAILABLE)) { ImGui::TextWrapped(...); return; }
```

and dimming is drawn at the bottom of that function. `UI_VIDEO_AVAILABLE` means *the scaler
and window controls have a backend*, which the x64 runtime does not have and does not need:
dimming changes the colours the game draws with, so it never touches the scaler. One blanket
early-out for "video" therefore hid the one video feature this backend has.

The dimming block is now its own function, called from both paths, and the message names
only what is actually missing. `UI_DIM_AVAILABLE` also counts pools, so a game whose only
rule is a pool is not reported as having no dimming at all.

The wider point is that the menu is the only way any of this work reaches the owner, and it
had no test that could see a missing control: `tools/test_menu.c` renders the overlay on a
real Direct3D 9 device and proves it does not crash, which a control that is never drawn
passes trivially. `tools/test_menu_logic.cpp` now runs the sections headlessly -- ImGui with
no backend at all, just a built font atlas and a display size -- against a stub that records
which settings each section reads. A control that is drawn reads its own value, so the set of
settings queried is the set of controls offered. It asserts every dimming control appears
with *and* without a video backend, that the scaler's appear only with one, and that a game
with no dimming rules still gets the sliders (disabled, saying why) rather than nothing.
Reinstating the early-out makes it fail with ten named lines.
