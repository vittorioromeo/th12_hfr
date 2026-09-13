# TH06 New Classic: feasibility and reverse-engineering notes

Research and prototype date: **2026-09-13**. Status: **experimental x64 prototype,
owner-tested gameplay and F11 menu; broader compatibility validation remains open**.
Initial research reviewed `c33dacc` / `e0f3f91`; implementation continued on the merged
v0.4.12-test checkout (`d09a86e`). Sections 1–12 preserve the original findings;
[section 13](#13-experimental-prototype-2026-09-13) records implementation, validation,
installation and remaining work.

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

```text
<the game's install folder>\
    th06nc\th06nc.exe
    th06c\th06c.exe
```

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

These are fingerprints of the local files as installed, **not verified official Steam depot
hashes**. Recheck the executable against a Steam-verified build before treating these RVAs as release signatures.
The readme contains legacy `ver 1.03` text from the original game; that is not a reliable
version identifier for this executable. The replay format version below is also a separate
identifier.

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
Generic sprite draw entry points include `0x67f0` and `0x4dc0`; their full state mutation
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
$ncExe = 'C:\Program Files (x86)\Steam\steamapps\common\th06nc\th06nc.exe'
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
complete interpolation coverage. Pixel-art scaling, sharpening, internal resolution,
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
.\test64.ps1 -GameExe 'C:\Program Files (x86)\Steam\steamapps\common\th06nc\th06nc.exe'
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
these keys without replacing unrelated settings. Simulation-substep controls are not
offered by this backend.

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
shader/menu code. Fractional gameplay of the world at large remains a separate project;
the player's own motion and input were taken to the display rate in section 14, which
also records why the same approach does not extend to bullets.

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
see "Why bullets cannot follow" below.

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
| `0x4f27b2` | non-zero while a replay is driving the input word | static, at `0x6ba40` |

Input bits confirmed at the movement switch: `0x04` focus, `0x10` up, `0x20` down,
`0x40` left, `0x80` right. Right beats left and up beats down, exactly as the native
switch orders them; focus and diagonal select one of the four speeds above.

The relocated site gains `mulss xmm<n>,[rip+factor]` after each of the two multiplies, plus
a `mov byte [rip+ran],1`. Both operands live in the relay page, which is allocated within
rel32 reach of the image, because the DLL's own globals may be further away than that. The
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
faithfully — the divergence is real, not cosmetic. The feature disables itself while
`0x4f27b2` is set, so native and previously recorded replays play back correctly; it does
not and cannot make its own recordings faithful. Recording a sub-tick run honestly would
mean carrying the sub-frame input stream in a sidecar and reading it back on playback,
which the replay format work in section 8 has not reached. Until then the setting is off by
default, the menu says so, and score runs should leave it off.

### Why bullets cannot follow

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
subtick=0     ; sub-tick player movement; inert at 60 Hz and during replay playback
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
