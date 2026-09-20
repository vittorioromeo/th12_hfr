# TH06 New Classic: reverse-engineering reference

Touhou Koumakyou New Classic is a 64-bit DxLib/Direct3D 11 game. It is handled by the x64
fixed-clock runtime (`src/hfr64.c`, profile `src/games/th06nc.c`), not by the x86 runtime.
For a feature comparison with the x86 games see [TH06NC_VS_TH10_13.md](TH06NC_VS_TH10_13.md).

## 1. Status and scope

**Experimental.** Gameplay and the F11 menu are owner-tested; broader compatibility validation
is open ([§23](#23-open-items)).

- The native update list runs at exactly 60 Hz. Presentation runs at the display rate. Sprite
  position, rotation and scale are interpolated (or predicted) between native ticks.
- Three optional features, each off by default: sub-tick player movement (`subtick`),
  sub-stepped bullets and lasers (`substep`), dimming.
- Items, enemies, player shots, effects and all scripts stay at 60 Hz by design
  ([§16](#16-systems-left-at-60-hz)).
- Nothing disables `subtick` or `substep` during replay playback ([§18](#18-replays)).
- Executables are never patched on disk. Unknown builds fail closed.

Profile counts in `src/games/th06nc.c`: 29 frozen signatures, 17 code patches, 5 MinHook entry
detours, 7 guard ranges, 3 dimming rules, 2 dimming pools. (v0.5.3-test had 27 signatures and 16
patches; the bullet sprite-step redirect added two signatures and one patch.)

Engine analysis dates from 2026-09-13, the Proton work and the last two sub-step fixes from
2026-09-16. The research reviewed commits `c33dacc` / `e0f3f91`; the first prototype was built
on the v0.4.12-test checkout (`d09a86e`) and shipped as v0.4.13-test.

### Why a separate runtime

The executable is AMD64/PE32+, renders through DxLib on D3D11, and advances gameplay and
animation timers with integer increments. There is no fractional-speed float to scale
([§7](#7-engine-objects)), so the TH10–13 design (run the whole update list N times with a
scaled speed) would run scripts, collisions, spawns and RNG N times. The x86 DLL, its code
emitters, D3D9 backend and replay integration do not transfer. Concrete blockers in the x86
code: `identity.h` requires PE32/i386 and image base `0x400000`; `node_class.func` and callback
comparisons use `uint32_t`; the enemy list traversal uses 32-bit pointers; the UI boundary
carried D3D9 types; relative branches are cast to `int32_t` without a reach check. Recompiling
the unity build as x64 fixes none of these. One x86 DLL cannot serve both processes.

Shared between the two runtimes: the identity registry, `core/patch.c`, SHA-256, the menu
content and `ui_api.h`, the F11 key state machine, configuration and logging conventions. What
the x86 runtime keeps to itself: `core/timing.c`/`limiter.c` globals, `core/interpolation.c`'s
x86 call shim and enemy list walk, `backends/update_runner.c`, `backends/d3d9.c`, `texscale.c`,
`core/x86.c` and the speed/site helpers, `core/replay.c` (the `t10r`/`t11r`/`t12r`/`t13r` and
USER-chunk handling does not describe this format).

## 2. Product and inspected build

The [Steam product page](https://store.steampowered.com/app/4659620/Touhou_Koumakyou_New_Classic__the_Embodiment_of_Scarlet_Devil/)
lists September 9, 2026 as the release date, 64-bit Windows and DirectX 11, and a bundle of
separate **New Classic** and **Classic** versions. This document concerns New Classic, not the
2002 executable. Steam app id `0x48afc6` (4659620).

| Property | New Classic `th06nc.exe` | Bundled Classic `th06c.exe` |
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

Classic was fingerprinted only and is unsupported. It is 64-bit, so an original-TH06 x86 patch
cannot be reused for it either. The fingerprint comes from a local installation; a
Steam-verified fingerprint has not been supplied.

| Section | RVA | Virtual size | Purpose |
| --- | --- | --- | --- |
| `.text` | `0x1000` | `0x2bea91` | executable code |
| `.rdata` | `0x2c0000` | `0x626ba` | constants, imports, strings |
| `.data` | `0x323000` | `0x906660` | writable globals; mostly zero-initialized |
| `.pdata` | `0xc2a000` | `0xccb4` | x64 unwind ranges |
| `.rsrc` | `0xc37000` | `0x322b0` | resources |
| `.reloc` | `0xc6a000` | `0x9a8` | relocations |

Data files: `th06CM.dat`, `th06ED.dat`, `th06FN.dat`, `th06IN.dat`, `th06MD.dat`, `th06ST.dat`,
`th06TL.dat`, and two sets of `th06_01.opus` … `th06_18.opus` under `bgm`/`bgm2`. Config files
are `th06.cfg` and `th06.env`. Archive and config formats are not decoded. Embedded strings
name resources such as `data/etama.anm`, `data/stg1enm.anm`, `data/ecldata1.ecl` and
`data/frame.anm`. The only non-system import is `steam_api64.dll`.

Import-table RVAs in KERNEL32: `0x2c0058` SetWaitableTimerEx, `0x2c0060` CreateWaitableTimerExW,
`0x2c0050` WaitForSingleObject, `0x2c0090` GetProcAddress. Graphics creation uses dynamic
loading, so the absence of a D3D11 import descriptor says nothing about the renderer.

## 3. Address convention and analysis tools

**All addresses are RVAs** unless called a VA. `address = actual module base + RVA`. ASLR is
active: one probe loaded at `0x7ff65a7d0000`, not the preferred `0x140000000`. Never truncate
pointers or reuse the x86 runtime's `0x400000` arithmetic.

Evidence labels used below: **static** (instructions, PE data or decompilation checked against
the binary), **native** (observed in a running copy), **candidate** (semantic label not yet
validated in gameplay).

Ghidra 11.3.2 with JDK 23 imports the executable as x86-64, completes auto-analysis in 185
seconds, and `tools/ExportAll.java` exports 2,730 non-external functions; two exports report
decompilation failure. The count differs from `.pdata` because unwind entries can describe
fragments and leaf functions may have none. There is no PDB. Names such as `FUN_140010870` are
analyst labels.

Traps:

- **Decompiler types.** Ghidra sometimes renders an integer timer as a float array element
  cast to `int`, with the increment cast back to `float`. The machine code loads, increments
  and stores an integer. Check the instructions.
- **`.pdata` entries are not function starts.** `0x10893`, `0x11610` and `0x117b1` are all
  entries inside the one function `0x10870`.
- **Linear xref scans lie in both directions.** `inspect_pe.py xrefs` decodes sections
  linearly and misaligns wherever data or padding sits between functions. It invents
  references and misses real ones, and an absence of references found this way is equally
  unreliable. It reported no callers for `0x6b970`, `0x6b9f0` and `0x6ba40`; all three are
  live ([§18](#18-replays)). It missed the `je 0x3c030` at `0x3c03d`. Use
  `tools/porting/xrefs64.py`, which decodes each function from its own `.pdata` BeginAddress
  and reports read or write. Any address claim resting on `inspect_pe.py xrefs` alone should
  be re-checked with it.
- **Register-relative accesses are invisible to any address scan.** The bullet hit test reads
  the hitbox radius as `[rbx+0x774c]`, not as absolute `0x506aec`; the dying state
  `0x506c38` is written as `player+0x7898`; the draw colour global `0xa6eaf0` has 21 writers
  and no visible reader. "No writer found" means "written through a register".
- **Before a byte gates anything, something reachable must be shown to read or write it, in
  the running game.** A gate built on a misidentified byte kept `subtick` and `substep`
  from ever running ([§18](#18-replays)).
- **Entry-point lists are partial until checked at runtime.** The menu sprite entry `0x36c0`
  was missed because the title screen was never checked for sprite-hook hits
  ([§10](#10-pose-history-and-sprite-smoothing)). The runtime logs the live node lists and a
  per-callback sprite census ([§19](#19-f11-menu-configuration-and-diagnostics)); use them
  instead of a static map.

## 4. Rendering backend: DxLib on Direct3D 11

Static evidence: UTF-16 `DxLib` at `0x2c4cf0`, DxLib-style graphics wrappers, dynamic
resolution of `D3D11CreateDevice` and `CreateDXGIFactory*`. D3D9/9Ex support strings are also
present and do not identify the active renderer. The
[official DxLib source package](https://dxlib.xsrv.jp/dxdload.html) (`DxLibMake3_25a.zip`;
files `Windows/DxGraphicsAPIWin.cpp`, `DxGraphicsWin.cpp`, `DxGraphicsWin.h`,
`DxGraphicsD3D11.cpp`, `DxSystemWin.cpp`) matches the inspected API selection constants,
dynamic loading and presentation wrappers. **The exact statically linked DxLib version is not
established.**

| RVA | Meaning | Evidence |
| --- | --- | --- |
| `0x270e00` | loads `d3d11.dll` by bare name; handle saved at `0x9b1c98` | static |
| `0x270bc0` | loads `dxgi.dll` by bare name (`LoadLibraryW`); handle saved at `0x9b1ca0` | static |
| `0x2707b0` | DXGI factory creation: resolves `CreateDXGIFactory2` from the saved handle through the EXE's `GetProcAddress` import, calls it; falls back to Factory1/Factory | static |
| `0x270020` | wrapper resolving/creating the D3D11 device | static; called once per native probe |
| `0x270089` | reference to ASCII `D3D11CreateDevice` at `0x2cebd0` | static |
| `0x8fe20c` | graphics API selector | native value `2`; DxLib's D3D11 enum is `2`, D3D9 is `1` |
| `0x953e0` | thunk to `0x928b0`, the DxLib `ScreenFlip` path | static; called by the frame finalizer |
| `0x23d540` | graphics backend dispatch used by that path | static |
| `0x259a40` | D3D11 screen-flip implementation | static dispatch on selector `2` |
| `0x25a1f1` | swap-chain `Present` call site | static and native |
| `0x50a204` | DxLib `NotWaitVSyncFlag` equivalent | static; determines the sync interval at that call |
| `0x8fe3b0 + index * 0x118` | swap-chain pointer slots used there | static; output-index lifetime not mapped |

At `0x25a1f1`, RCX is the swap chain, EDX the sync interval, R8D the flags. The native trace
saw calls returning to `0x25a1f6` with **EDX=1, R8D=0**. `d3d11.dll` and `dxgi.dll` loaded; no
`d3d9.dll` load was observed. A D3D9 fallback was not forced or tested.

There are two independent pacing constraints: this synchronized Present and the software frame
wait ([§5](#5-main-loop-frame-pacing-and-the-fps-readout)). Removing one leaves the other. See
[`IDXGISwapChain::Present`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present).

**Trap: shared COM thunk.** The Present call goes through `0x26fff0`, whose whole body is a
slot-8 vtable dispatch (`mov rax,[rcx]; jmp [rax+0x40]`). It is not exclusively Present:
another caller returns to `0x243284` with a pointer in RDX, and more static callers exist
(identical COM wrappers folded by the compiler). A hook on the thunk intercepts unrelated
methods. Hook the call site `0x25a1f1`, or filter by return address `base+0x25a1f6`.

## 5. Main loop, frame pacing and the FPS readout

```text
0x45d82  call update runner 0x3be80
0x45d8b  post-update: replay fast-forward state, audio cursor advance/seek
0x45dc6  call update runner again (conditional: fast-forward; up to seven extra updates)
0x45e91  call draw runner   0x3bf70
0x45e96  call finalizer     0x3c330
0x45e9e  process messages/check termination, then loop
```

Update and draw are not one-to-one in every mode. The exact user-facing trigger of the
`0x45dc6` path is not mapped.

The finalizer `0x3c330` draws edge/letterbox regions, calls `0x953e0` (ScreenFlip), waits for
the deadline, then does FPS, slow-percentage and pending-ANM-load bookkeeping and the
`0xc21974` countdown. Replacing it with a no-op drops more than the frame cap.

Pacing setup at `0x3c750` creates a high-resolution waitable timer with
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
| `0xc220f0` | sleep function pointer, `0x3c290` if timer creation succeeds, else null |
| `0xc220f8` | clock frequency |
| `0xc22100` | rounded target period in clock ticks |
| `0xc22108` | configured rate as double (`60.0` observed) |
| `0xc22118` | spin margin |
| `0xc22120` | pacing initialized flag |
| `0xc22128` | clock origin |
| `0xc22130` | deadline sequence counter |
| `0x3c290` | sleep helper using SetWaitableTimerEx and WaitForSingleObject |
| `0x3c45c` | start of the software wait (22 bytes replaced, [§9](#9-clock-and-presentation-hooks)) |
| `0x3c488`–`0x3c5a1` | deadline calculation, timed waits and final `pause` spin |
| `0x3c5a1` | first instruction after the wait (`wait_resume`) |
| `0x3c5ec` | FPS counter block |
| `0x3c71d` | finalizer epilogue (`frame_epilogue`) |

Deadlines are origin + period × sequence. When more than a period behind, the finalizer resets
origin and sequence. Native values: frequency **10,000,000**, period **166,667**, rate
**60.0**, spin margin **10,200** ticks.

FPS readout:

```text
0x3c5f3  mov ecx,[0xc2232c] ; inc ecx ; store     -- the frame count
0x3c604  rdx = now - [0xc29648]                   -- elapsed since the last recompute
0x3c60b  cmp rdx, 0x7a120 ; jl ...                -- only recompute every 500000 units
0x3c652  string reference for the formatter
0x3c660  call sprintf with "%.02lffps" (0x30c9e8)
```

`0xc2232c` is read once and written twice, all inside `0x3c330` (`xrefs64.py`). The display
also reads `0x4f27b2` and offsets its drawing position when it is non-zero; that is the only
reference to that byte `xrefs64.py` finds ([§18](#18-replays)).

## 6. Update and draw lists

| RVA | Role |
| --- | --- |
| `0x3be80` | update runner |
| `0x3bf70` | draw runner |
| `0x4f1440` | update-list sentinel |
| `0x4f1400` | draw-list sentinel |
| `0x126e0` | remove/unlink node helper |
| `0x3beb0`–`0x3beba` | update dispatch: `mov rax,[rbx+8]` (`0x3beb0`); `mov rcx,[rbx+0x38]` (`0x3beb4`); `call rax` (`0x3beb8`); `cmp eax,2` (`0x3beba`) |
| `0x3c030`–`0x3c03a` | draw dispatch, the same ten bytes at `0x3c030`, `0x3c034`, `0x3c038`, `0x3c03a`; `je 0x3c030` at `0x3c03d` |

Nodes are **0x40 bytes**:

| Offset | Field |
| --- | --- |
| `+0x00` | signed 16-bit priority |
| `+0x02` | flags byte; dynamic-node constructors set bit 0 |
| `+0x08` | callback pointer |
| `+0x10` | on-register callback pointer, cleared after invocation |
| `+0x18` | cleanup callback pointer |
| `+0x20` | previous node |
| `+0x28` | next node |
| `+0x30` | self/ownership-related pointer; semantics not established |
| `+0x38` | callback argument, passed in RCX |

Constructors insert by ascending priority. Update return values (static): `0` removes the
node; `1` continues; `2` repeats the callback; `3` ends the walk normally; `4`/`5` set distinct
outer-loop results; `6` restarts at the sentinel. List exhaustion also depends on the runner's
callback count. The draw runner repeats on `2`, removes on `0`, stops on `3`/`4`/`5`. Do not
cast nodes to the x86 `UpdateFunc` struct.

On its exit paths the update runner clears a flag at `0xc21970` and calls `0x76a30`, which
drains a queued sound list. It is not an input poll, and running the runner N times a frame
runs it N times.

The update dispatch at `0x3beb0` is a usable per-callback gate point (relocate all ten bytes;
`call rax` alone is two bytes): on a minor tick run only converted callbacks and report `1`
for the rest. It is designed and verified but **not emitted**; the projectile sub-step calls
its callback directly instead ([§13](#13-sub-stepped-bullets)).

Callbacks (registration from static analysis; priorities confirmed from the live lists):

| System | Registration | Update / priority | Draw / priority | Argument |
| --- | --- | --- | --- | --- |
| Input | | `0x79c70` / 0 (sole caller of the device poll `0x12be0`) | | |
| ASCII/text | `0x9400` | `0x90d0` / 1 | `0x9260` / 19; `0x92a0` / 13 | `0x3de620` |
| Screen manager (menus) | | `0x47940` / 2, a thunk into `0x479e0` (22 KB state machine for every menu screen) | `0x54870` / 0 | both switch on a screen state at `object+0x168b0` |
| (unidentified) | | `0x75af0` / 3 | | |
| (unidentified) | | | `0x76280` / 20 | |
| Fade/transition overlay | | | `0x7a330` / 18 | |
| Main game state | `0x3a810` | `0x3a210` / 4 | `0x3a7f0` / 3 | `0x4f1e60` |
| Stage background | | | `0x78290` / 5 (layers 0, 1); `0x78390` / 6 (layers 2, 3) | |
| Player | `0x67dc0` | `0x68820` / 7 | `0x6a130` / 7; `0x6a210` / 9; `0x6a430` / 11 | `0x4ff3a0` |
| Enemy manager (`stg*enm.anm`) | `0x37170` | `0x373b0` / 9 | `0x38290` / 10 | `0xaa1e90` |
| Effects (`eff*.anm`) | `0x2b490` | `0x2b280` / 10 | `0x2b310` / 12 | `0xa6ecd0` |
| Enemy bullets, lasers, items | in `0x3a9c0` | `0x10870` / 11 | `0x11940` / 14 | `0x3ec2a0` |
| HUD update (not the item physics, [§15](#15-items)) | | `0x3cc50` / 12 | | |
| HUD digits | | | `0x3cd20` / 15 | |
| Replay recorder ([§18](#18-replays)) | | `0x6b970` / 15 | | |

Title/menu screen lists, read from the running game:

```text
update list: 0@79c70  1@90d0   2@47940  3@75af0
draw   list: 0@54870  13@92a0  18@7a330 19@9260  20@76280
```

What each draw callback draws is in [§17](#17-dimming). The map is not a complete
classification of every node.

## 7. Engine objects

No common fractional-speed global exists. The `xmm15` that multiplies much of the projectile
update is loaded once at `0x108f8` from `.rdata` `0x30cd58` and is the constant `0.5`. Every
motion site needs its own factor and every discrete block its own gate, as TH10 needed
(`DEVNOTES_RUNTIME.md` §7).

### ANM VM (0x120 bytes)

| Offset | Field |
| --- | --- |
| `+0x28` | integer timer, incremented at `0x7451` |
| `+0x9c`, `+0xa0`, `+0xa4` | rotation X, Y, Z, float32 |
| `+0xb4`, `+0xb8` | previous/current integer age (`0x7459`–`0x7469` copies `+0xb8` to `+0xb4`, increments `+0xb8`) |
| `+0xc4` | flags |
| `+0xc8`, `+0xcc`, `+0xd0` | position X, Y, Z, float32 |
| `+0xe4`, `+0xe8` | scale X, Y |
| `+0xec` | packed colour `0xAARRGGBB`; alpha is the byte at `+0xef` ([§17](#17-dimming)) |
| `+0xf0`, `+0xf8` | script fields; `+0xf8` is the script base |

| RVA | Function |
| --- | --- |
| `0x69b0` | interpreter step `(manager, vm)`; returns `1` for a VM with no script. Writes `[vm+0xef]` at `0x6b1e` |
| `0x2760` | start script by index; resets position/timers, assigns `+0xf0/+0xf8` |
| `0x2a20` | start script by pointer; assigns `+0xf0/+0xf8`, resets integer age |
| `0x2980` | sprite selection/matrix setup. **Not** a script-lifetime initializer: ANM selects frames without creating an entity, so it is not a spawn hook |
| `0x67f0` | sprite draw `(manager, vm, flags)` in `rcx`/`rdx`/`r8`; tests `+0x9c`, `+0xa0`, `+0xa4` against zero (`0x6816`, `0x6830`, `0x6894`) to pick the rotated path |
| `0x4dc0` | rotated sprite draw, same signature; reached from `0x67f0` |
| `0x36c0` | third draw entry, same signature; opens by reading `[rdx+0xa4]` and `[rdx+0xc4]`; dispatches into the lower-level draws. The menus draw through it |
| `0x4f41`, `0x5170` | multiply the quad by `+0xe4`/`+0xe8` |

The ANM manager pointer is loaded RIP-relative (`mov rcx,[rip+0xa5d466]` at `0x11543`). Every
draw path copies `+0xec` byte for byte into the global draw colour at `0xa6eaf0`
(`+0xef`→`0xa6eaf3`, and so on down). The draw entry points' full state-mutation and batching
contracts are unverified.

### Bullets

Manager `0x3ec2a0` (8-byte header: its frame counters). Pool at manager `+0x08`
(`0x3ec2a8`), 640 (`0x280`) entries, stride `0x620`.

| Offset | Field |
| --- | --- |
| `+0x08`, `+0x0c`, `+0x10` | velocity XYZ, float32 |
| `+0x14` | flags |
| `+0x1c` | counter |
| `+0x24` | speed |
| `+0x28`, `+0x2c` | previous/current age, int32 |
| `+0x30`, `+0x34`, `+0x38` | position XYZ, float32 |
| `+0x40` | angle, float32 |
| `+0x44` | state, uint16; zero is an empty slot |
| `+0x50`, `+0x170`, `+0x290`, `+0x3b0`, `+0x4d0` | embedded VMs, selected by state 1..5 |
| `+0x150` | descriptor pointer; the cull reads half-extents from it |
| `+0x604` | off-screen frame counter (word) |
| `+0x618` | graze flag, set at `0x11277`/`0x112d2` |

The update callback is dissected in [§13](#13-sub-stepped-bullets). The draw callback
`0x11940` copies each bullet position into the selected VM's `+0xc8`/`+0xcc`, sets
Z/rotation/colour (`or dword [vm+0xec], 0xffffff`), and draws. It also draws lasers and items
and **writes state**, including conditional VM initialization.

### Lasers

64 entries, stride `0x298`, in the same manager, updated by the loop at `0x11604`–`0x11740`
inside `0x10870`. With `rbx` as the loop pointer: head `[rbx]`, tail `[rbx-4]`, speed
`[rbx+0x258]`, maximum gap `[rbx+0x274]`, timer pair `[rbx-0xc]`/`[rbx-8]`, VM at
`[rbx+0xc]`. The complete layout is not validated.

### Player (`0x4ff3a0`, update `0x68820`)

| Offset | Absolute | Field |
| --- | --- | --- |
| `+0x408` | `0x4ff7a8` | shot timer (8 bytes) |
| `+0x420` | | shot pool: 80 entries, stride `0x170`; type word at `+0x00` (1 or 2), VM at `+0x08` (first VM `0x4ff7c8`, pool ends at `0x506ac8`) |
| `+0x7710`, `+0x7714` | | per-axis step scale applied to the chosen speed |
| `+0x7730`, `+0x7734`, `+0x7738` | `0x506ad0/4/8` | position XYZ |
| `+0x774c` | `0x506aec` | hitbox radius |
| `+0x7754` | | 16 boxes of `{w,h,cx,cy}`, filled dynamically, no static writer: the player's shots as the enemy side sees them, and the bullet-cancel boxes |
| `+0x7854`, `+0x7858` | `0x506bf4` | previous/current state timer, int32 |
| `+0x7860`, `+0x7864` | | straight speed, unfocused and focused |
| `+0x7868`, `+0x786c` | | diagonal speed, unfocused and focused |
| `+0x7898` | `0x506c38` | dying state; `2` = dying |
| `+0x789c`, `+0x78a0` | | facing, stored before the step multiplies and read by the animation triggers |
| `+0x78c8` | `0x506c68` | the player's own sprite VM |

**Unresolved:** the dimming analysis (and the comment in `src/games/th06nc.c`) also describes
a focus-sprite VM "at `player+0x7898`", i.e. `0x506c38`. That is the dying-state dword and a
0x120-byte VM there would overlap `+0x78c8`. One of the two readings is wrong.

| RVA | Meaning |
| --- | --- |
| `0x4ff0e0` | playfield clamp: min X, min Y, width, height as four floats |
| `0x4ff0cc`, `0x4ff0d0` | graze counters, capped 99999 and 999999 |
| `0xa6ec60`, `0xa6ec64` | current/previous input word |
| `0xa6ec40` | RNG count/state (8 bytes) |
| `0x12be0` | device input poll, returns the input word in EAX; sole caller `0x79c70` |
| `0x69388` | movement step ([§12](#12-sub-tick-player-movement)) |
| `0x69a36` | player-shot motion: `[rbx+0x13c] += [rbx+8]` |
| `0x6a8c0` | pure AABB test of a rectangle against the 16 boxes at `player+0x7754`; no side effects |
| `0x6a980` | bullet hit test ([§13](#13-sub-stepped-bullets)) |
| `0x6aba0` | laser hit test ([§14](#14-lasers)) |

Input bits: `0x02` bomb (rising edge tested), `0x04` focus, `0x10` up, `0x20` down, `0x40`
left, `0x80` right. Right beats left and up beats down. Focus and diagonal select one of the
four speeds. The complete input producer and replay override path are not mapped.

### Enemies, effects, items

- Enemies: pool `0xaa1e98`, 256 entries of `0x10b0`, active while `[+0xbc]` is negative.
  Motion at `0x374e9`: `[+0xb4] += [+0x1088]`, followed by an optional clamp to per-enemy
  bounds. Player-shot damage is applied on the enemy side at `0x37a6c` as
  `add [enemy+0x234], -0xa` per overlapping box; nothing consumes the shot there. `0x373b0`
  contains `+= 0.3`, an option's approach speed.
- Effects: 512-entry pool, stride `0x198`, manager `0xa6ecd0`.
- Items: [§15](#15-items).

## 8. Runtime structure and installation

| File | Responsibility |
| --- | --- |
| `src/fixed_identity.h` | One verified-build registry shared by the x86 dispatcher, x64 helper and DLL |
| `src/games/th06nc.c` | Hash, image size, frozen signatures, RVAs, VM offsets, guard ranges, dimming rules and pools |
| `src/backends/fixed_game.h` | Fixed-clock engine profile schema; RVAs are relocated at runtime |
| `src/hfr64.c` | x64 installation, relays, draw submission, pacing and diagnostics |
| `src/backends/fixed_clock.h` | Elapsed-time 60 Hz scheduling independent of the graphics API |
| `src/backends/fixed_history.h` | Renderer-independent pose histories and discontinuity handling |
| `src/backends/subtick.h` | Slice accounting, direction table and clamp for sub-tick movement |
| `src/backends/substep.h` | Exact dyadic sub-step schedule ([§13](#the-schedule-in-substeph)) |
| `src/core/patch.c` | Transactional code-patch implementation, compiled by both runtimes |
| `src/core/file_hash.h` | SHA-256 verification shared by the registry's callers |
| `src/ui/menu.cpp`, `ui_api.h` | The same menu content and runtime interface for both architectures |
| `src/ui/menu_key.h` | Dual-source F11 handling extracted from the D3D9 window code |
| `src/ui/menu_renderer.h`, `menu_dx9.cpp`, `menu_dx11.cpp` | Renderer adapters around matching ImGui backends |
| `src/ui/overlay_dx11.cpp` | DxLib swap-chain overlay and temporary render-target binding |
| `src/ui/overlay_fixed.c` | Fixed-clock settings/status and capability responses |
| `src/proxy_dxgi.c`, `src/dxgi_exports.h` | The `dxgi.dll` autoloader ([§20](#20-autoload-through-dxgidll-and-proton)) |
| `src/launcher64.c` | Same-architecture injection and initialization outside the loader lock |
| `build64.ps1`/`build64.sh`, `test64.ps1`/`test64.sh` | Separate x64 objects (`build/obj64`), build and regression entry points |

Adding an executable to this engine family takes a profile and an entry in
`fixed_identity.h`. A different engine may need different native loop relays while reusing the
scheduler, history, menu and renderer.

### Verification and install order

`hfr_start` verifies the SHA-256 (the launcher does too), AMD64, image size and every frozen
signature before installing anything. Log lines: `Executable fingerprint rejected; no patches
applied`, `Signature rejected at RVA %x; no patches applied`, `Code patch preparation failed`,
`Sprite hook installation failed`, `Code patch commit failed`, and on success
`Installed at image=%p relay=%p; original executable unchanged`. Pre-existing instruction
changes fail closed. No Steam API files are changed.

[MinHook v1.3.4](https://github.com/TsudaKageyu/minhook/releases/tag/v1.3.4) supplies
instruction relocation for the function-entry detours: `0x67f0`, `0x4dc0`, `0x36c0`, `0x2760`,
`0x2a20`. (`0x36c0` has no frozen signature in the profile; the other four do.) Unmodified
source and licence are under `third_party/minhook`; `UPSTREAM.md` records the archive URL and
SHA-256. The ImGui D3D11 backend is from the same
[v1.91.8 tag](https://github.com/ocornut/imgui/tree/v1.91.8/backends) as the vendored core
([upstream file](https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_dx11.cpp)).

Entry detours are enabled only after patch preflight; the patch queue verifies code bytes and
memory protections before committing. A later failure removes the detours. Under the launcher,
any startup failure terminates the suspended child; no partially initialized game is resumed.

### Relay and data pages

One 8 KiB reservation within rel32 reach of the image (searched outward in allocation-granularity
steps). The first page holds emitted code and becomes `PAGE_EXECUTE_READ` after installation.
The second stays `PAGE_READWRITE` and holds the words the relays and the runtime write:

| Data-page offset | Word |
| --- | --- |
| `+0` | `player_factor` (float) |
| `+4` | `player_ran` (byte) |
| `+8` | `proj_minor` (byte) |
| `+12` | `proj_dt` (float) |
| `+16` | `proj_ran` (byte) |
| `+24` | `draw_node` (pointer) |

They live near the image because the DLL's globals may be more than 2 GB away. **Trap:**
putting them on the code page crashes on the first native tick after install (the log ends
right after the D3D11 menu comes up). Unicorn cannot catch this, since it maps everything
writable; the transaction test now reads back the protections `VirtualProtect` applied and
writes through the pointers.

All generated relays are leaf tail jumps: no RSP change, no nonvolatile register touched, no
calls. Native call sites supply shadow space and existing unwind frames for the compiled C
callbacks. Every relative displacement is range-checked. Redirected calls go through a 16-byte
`jmp [rip]` relay. Requirements for any new x64 hook: register arguments, shadow space, stack
alignment, nonvolatile registers, correct relocation of RIP-relative instructions, unwind
metadata for non-leaf generated code
([x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170)).
The gate mechanism copies site bytes verbatim, so a gated block must contain no RIP-relative
instruction. One patch carries at most 32 bytes. Nothing branches into the interior of any
patched range (checked with the `.pdata`-based scanner; branches reach first bytes only).

The 17 patches:

| # | Site | Size | Treatment |
| --- | --- | --- | --- |
| 1 | `0x45d82` | 5 | call → `update_first` |
| 2 | `0x45dc6` | 5 | call → `update_extra` |
| 3 | `0x45e91` | 5 | call → `draw_frame` |
| 4 | `0x25a1f1` | 5 | call → `present` |
| 5 | `0x3c45c` | 22 | software wait replaced |
| 6 | `0x45d8b` | 12 | post-update relay |
| 7 | `0x10982` | 9 | gate: bullet state switch |
| 8 | `0x11298` | 7 | gate: off-screen counter |
| 9 | `0x11562` | 11 | gate: per-bullet age |
| 10 | `0x11714` | 7 | gate: laser timer and VM step |
| 11 | `0x11796` | 9 | gate: manager frame counter, marks `proj_ran` |
| 12 | `0x1102c` | 5 | bullet motion rewritten |
| 13 | `0x1161d` | 12 | laser growth scaled |
| 14 | `0x108e7` | 5 | call → `item_update` |
| 15 | `0x1154e` | 5 | call → `sprite_vm_step` |
| 16 | `0x3c030` | 10 | draw dispatch records the node |
| 17 | `0x69388` | 24 | player movement scaled, marks `player_ran` |

### Launcher injection

`touhou_hfr.exe` detects the verified executable and dispatches to `touhou_hfr64.exe`, which
creates the game suspended and injects. `DllMain` only remembers the module and disables
thread notifications. The helper injects `LoadLibraryA`, waits, obtains the **full 64-bit**
DLL base through a module snapshot, then calls exported `hfr_start` on a separate remote
thread. **Trap:** `GetExitCodeThread` is not an HMODULE; it is 32 bits. Snapshots retry
`ERROR_BAD_LENGTH`. The primary thread stays suspended until initialization reports success.

On the Steam build the launcher route cannot work: the game calls
`SteamAPI_RestartAppIfNecessary` first and exits when Steam did not start it. The shipped
install is the `dxgi.dll` proxy ([§20](#20-autoload-through-dxgidll-and-proton)); files and
steps are in the [README](../../README.md#new-classic). Never copy the x86 `dinput8.dll` proxy
into New Classic.

## 9. Clock and presentation hooks

| Site | Treatment |
| --- | --- |
| `0x45d82` | First update call: run `0x3be80` on a 60 Hz boundary, otherwise return `0x100` |
| `0x45dc6` | Native extra-update call during replay fast-forward is preserved; each advances the logical tick |
| `0x45d8b`..`0x45d97` | Post-update instructions relocated; audio/fast-forward bookkeeping skipped on presentation-only iterations |
| `0x45e91` | Draw wrapper calls `0x3bf70` inside the draw guard ([§11](#11-draw-guard)) |
| `0x3c45c`, 22 bytes | `call wait_frame; test eax,eax; jnz 0x3c71d; jmp 0x3c5a1` |
| `0x3c5a1` | Native FPS/statistics/pending-ANM bookkeeping resumes here on a real update iteration |
| `0x3c71d` | Native epilogue, entered directly on a presentation-only iteration |
| `0x25a1f1` | Present call site: draws the overlay, sets the sync interval |
| `0x50a204` | `NotWaitVSyncFlag`, written as `!vsync` on every Present |

**Why the post-update relay exists.** Code after the first update examines replay
fast-forward state and advances/seeks an audio cursor. Gating only the calls to `0x3be80`
repeats that bookkeeping at presentation rate. The relay runs `test ah,ah`: `AH=1` branches
straight to the draw call `0x45e91`. On a real update the C wrapper returns only AL, and the
relay reproduces `mov ebx,[base+0x509690]`, `xor sil,sil`, `mov edi,r13d`, then resumes at
`0x45d97`. Native first-update exit codes keep their AL behaviour.

**Scheduler (`fixed_clock.h`).** QPC elapsed time decides when a native tick is due. A ratio
based on the requested FPS would be wrong: requesting 360 while VSync presents 60 would cut the
simulation to 10 updates/sec. At most one native update per outer iteration. Debt over four
native frames is discarded, so long stalls keep native slowdown instead of bursting. Changing
the presentation rate preserves the next simulation deadline.

**Pacer (`wait_frame`).** QPC plus a high-resolution waitable timer (`CreateWaitableTimerExW`
flag `2`, fallback `CreateWaitableTimerW`), sleeping while more than 0.4 ms remains and waking
0.2 ms early, then a `YieldProcessor` spin. A deadline more than four steps behind is reset.
The trailing partial instruction of the overwritten wait block is unreachable; both
continuations land on verified instruction boundaries.

**Present.** D3D11 VSync is `[fixed60] vsync`, off by default and independent of the x86
`[hfr] vsync`. `fps=0` follows the primary display's refresh rate (`EnumDisplaySettingsA`);
explicit targets clamp to 60–1000.

**FPS readout.** The game's counter `0xc2232c` is incremented at `0x3c5f3`, past
`wait_resume`, so it is reached 60 times a second. The runtime increments it on every
presentation-only iteration, so the game's readout shows the presentation rate. This is safe
because nothing outside `0x3c330` touches the counter. The finalizer's slow-percentage
accounting, pending ANM loads and `0xc21974` countdown still run on real update iterations
only. F11 → Timing prints the measured presentation and simulation rates.

## 10. Pose history and sprite smoothing

The three VM draw entry points `0x67f0` (`sprite_draw`), `0x4dc0` (`sprite_draw_rotated`) and
`0x36c0` (`sprite_draw_menu`) are detoured to one wrapper (`sprite` in `hfr64.c`; the blend is
`fixed_pose` / `fixed_pose_extra` in `fixed_history.h`). For each VM it captures the native pose once per logical tick,
writes a blended pose into the VM, calls the original, and restores exactly the bytes it
wrote. Authoritative bullet and player world positions are never replaced. A `depth` guard
makes the inner of two nested hooked draws (`0x67f0`→`0x4dc0`, `0x36c0`→ lower-level draws)
step aside.

- **Table.** 16,384 entries keyed by VM address, up to eight probes; a slot is reusable when
  empty or two ticks stale; exhaustion skips interpolation. Each entry records script base
  (`VM+0xf8`), logical tick, integer age (`VM+0xb8`) and previous/current pose.
- **Reset conditions.** Script replacement/restart, absent logical ticks, address reuse,
  non-finite values, motion of 64 game units or more in one native tick, and the position
  changing within one logical tick. The last rule excludes text/layout scratch objects, the
  sub-tick player VM and sub-stepped bullets, which are therefore drawn where they are.
- **Stack VMs** (address inside the current thread's stack limits) are temporary text/layout
  objects and are skipped.
- **Script-start hooks** `0x2760` and `0x2a20` clear the VM's entry. They catch
  reinitialization to the same script and age at the same address, which pointer/age
  comparison cannot.
- **Rotation (`+0x9c/a0/a4`) and scale (`+0xe4/e8`)** ride on the same validity decision.
  Rotation takes the short way round the wrap. When previous equals current the delta is
  exactly zero and the value comes back bit-identical; this matters because the game selects
  its rotated draw path by testing the angle against zero, so a stray non-zero angle would
  move the sprite to a different renderer. The test asserts it.
- **Interpolation** draws one native frame behind (up to one frame of visual delay).
  **Prediction** (`predict`) continues past the current native position; for an object whose
  velocity changes only at native ticks this is the position sub-stepping would produce. It
  is on exactly when sub-tick movement is active, because a bullet drawn a frame late next to
  a current player is a frame of travel away from its real position. It is not a separate
  setting. Prediction does not overshoot: it draws where the object will be when the frame is
  shown. Disabling it for decoration sprites only would add a bias, not remove one.
- The wrapper is bypassed when `interpolate=0`, the rate is 60 or the draw guard has failed.
  Changing the rate clears the table.

Measured: hooking `0x36c0` takes the title screen from **0 sprite calls to about 112 per
frame** (`samples=0` across three stats windows before).

Not smoothed: animation-frame selection, colour fades, scrolling/3D backgrounds, and any path
that bypasses the three entry points. Colour fades and frame changes are decided by the ANM
interpreter at 60 Hz, so there is nothing to interpolate between. TH10–13 handle this by
sub-stepping the interpreter (`AnmManagerWorld` and `AnmManagerUI` are `MODE_SUB` in all
four profiles). The equivalent here is sub-stepping `0x69b0` (scale what is continuous, gate
what is discrete); it is the most promising remaining rendering work. Menu colour fades are
the most visible stepped thing left.

## 11. Draw guard

`draw_frame` hashes (FNV-1a) selected gameplay state before and after the native draw runner.
A mismatch logs `DRAW GUARD FAILED at frame=… tick=…; reverting to 60 Hz without
interpolation`, forces the rate to 60 and disables interpolation, `subtick` and `substep` for
the session.

| Range (`th06nc_guards`) | Content |
| --- | --- |
| `0x3ec2a0`, 8 bytes | bullet-manager frame counters |
| `0x3ec2a8`, `0x50` bytes × 640, stride `0x620` | bullet motion, state, ages (before the embedded VMs) |
| `0x506ad0`, 12 | player position |
| `0x506bf4`, 8 | player state timer |
| `0x4ff7a8`, 8 | player shot timer |
| `0xa6ec40`, 8 | RNG count/state |
| `0xa6ec60`, 8 | current/previous input |

VM render caches are excluded. The guard stayed `ok` in all observed runs. It covers selected
state, not every field, and does not establish replay equivalence.

An item-pool range (`{0xbaf0e8, 24, 1024, 0x160}`: position and velocity) was written and
removed. Nothing shows the draw path leaves the item pool alone, and a spawn from a draw
callback would silently drop the whole patch to 60 Hz. Arm it only in a session that can watch
a stage run with it.

## 12. Sub-tick player movement

`[fixed60] subtick`, default 0. Input is polled once per drawn frame and the player moves by
that frame's share of the step, so a direction change acts within the frame it is made: at
360 Hz, six samples per 60 Hz frame and up to 2.8 ms of input latency instead of 16.7 ms.
Holding a direction covers exactly the stock distance per 60 Hz frame.

The player can be taken over alone because its displacement comes from one site, depends only
on the input word and four speed constants, and nothing depends on when within the frame it
happens: the hit test, shot origin and item magnet read the position once per native tick and
still do (unless `substep` is also on).

| RVA | Meaning |
| --- | --- |
| `0x69388`, 24 bytes | `mulss xmm6,[rdi+0x7710]; movss [rdi+0x78a0],xmm7; mulss xmm7,[rdi+0x7714]`: the two multiplies that turn a held direction into this frame's step. Relocated whole |
| `0x693a0` | resume: `addss` of the products into the position, then the native clamp |

The relocated site gains `mulss xmm6/xmm7,[rip+player_factor]` after each multiply and
`mov byte [rip+player_ran],1`. The facing store between the multiplies keeps its place.
**With the feature off the factor is `1.0f`, so the site is arithmetically identical to the
original and the game is bit-identical to stock.** The default depends on this.

Accounting: `tau = completed native ticks + phase`. Every presented iteration applies
`(tau_now - tau_last) × displacement(input polled now)` and advances `tau_last`, using the
direction table, the speeds at `+0x7860..+0x786c`, the scale at `+0x7710/14` and the clamp at
`0x4ff0e0`. While the pass is in charge the native factor is `0.0f`. The slices between two
native ticks sum to exactly 1.0 frame; the remainder of a frame is applied at the next native
tick, after the native update, with input polled there. `tau_last` tracks the clock in both
states, so toggling mid-play costs at most one frame of movement and cannot double-apply.

The pass stands aside (native factor `1.0f`) when the setting is off, the rate is 60, the
relay is absent, the draw guard has failed, or the movement site did not run on the previous
native tick. The last condition covers pause, menus, dialogue, death and stage transitions
without enumerating them: `player_ran` is set only when the game itself moves the player.

While it is active, sprites are predicted ([§10](#10-pose-history-and-sprite-smoothing)) and
the menu disables the interpolation toggle. Log line:
`subtick on|standing by: N input polls (rate), longest X ms, M player moves`. The poll cost is
logged because `0x12be0` is the game's own device poll, called once per drawn frame.

Not replay-safe: [§18](#18-replays).

## 13. Sub-stepped bullets

`[fixed60] substep`, default 0. Between native ticks `projectiles_slice` calls `0x10870`
directly with the manager `0x3ec2a0`, with `proj_minor=1` and `proj_dt` set to the slice length. Every
60 Hz block stands aside; motion, culling, cancel boxes, graze and the hit test run.

**This is a gameplay change.** The bullet hit test lives inside the callback, so sub-stepping
it sub-steps player death. A bullet fast enough to jump across the player between two 60 Hz
frames is never tested in the stock game and now is. The game becomes harder.

### Anatomy of `0x10870` (priority 11, argument `0x3ec2a0`)

| RVA | What happens |
| --- | --- |
| `0x108e7` | `call 0x42980`: the item pool update, in the prologue before any bullet ([§15](#15-items)) |
| `0x108f8` | loads `xmm15` = `0.5` from `0x30cd58` |
| `0x10970` | empty slot (state 0) → `0x1156d`, next bullet |
| `0x10982` | state switch: `movzx edx,[rbx+0x44]; mov ecx,edx; sub ecx,r12d` (ZF = state 1) |
| `0x10940`–`0x11021` | state machine: writes angle `+0x40`, speed `+0x24`, velocity `+0x08/0c/10`, flags `+0x14`, counter `+0x1c`, state `+0x44`. Converges on `0x11021` and `0x1102c` |
| `0x1102c`–`0x1105f` | **motion**: `pos += vel` on three axes, no time factor, 52 bytes, with `mov rax,[rbx+0x150]` interleaved |
| `0x11060`–`0x110b8` | off-screen cull against half-extents from the bullet's descriptor; reads `xmm2`/`xmm1` from the motion |
| `0x110ba` | tests the graze flag `+0x618` |
| `0x110f0` onward | spawn-state motion: `pos += vel/2`, `/2.5`, `/3` |
| `0x1113b` | `call 0x6a8c0`: AABB vs the 16 boxes at `player+0x7754`; on overlap the bullet goes to state 5 and spawns a cancel effect |
| `0x1115a`–`0x111df` | **graze**: circle test against `0x506ad0/4` with radius `bullet*0.5 + 20.0 + [0x506aec]`, then counters `0x4ff0cc`, `0x4ff0d0` |
| `0x11277`, `0x112d2` | set the graze flag |
| `0x11298` | `inc word [rbx+0x604]`: off-screen frame counter |
| `0x112b5` | state-5 exit when its script ends |
| `0x11418` | `call 0x6a980`: the **hit test**, only for bullets whose graze flag is already set |
| `0x11434`–`0x11543` | cancel-effect VM work, reached only when the hit test fires and the state changes |
| `0x1152d` | `call 0x69b0` that *starts* a newly spawned entity's script, behind the graze flag; once per bullet |
| `0x11543`, `0x1154a`, `0x1154e` | `mov rcx,[rip+0xa5d466]; lea rdx,[rbx+0x50]; call 0x69b0`: steps the bullet's sprite VM. `0x11553` `movss xmm6,[rip+...]` follows |
| `0x11562` | age: `[+0x28] = t; [+0x2c] = t+1` |
| `0x1156d`, `0x11578` | loop tail; `add rbx,0x620` |
| `0x11604`–`0x11740` | laser loop ([§14](#14-lasers)); epilogue at `0x11740` |
| `0x11796` | manager frame counter |
| `0x11883` | sole call of the laser hit test `0x6aba0` |

`0x6a980(player, position, size)` returns non-zero on contact: first `0x6a8c0` (cancel boxes,
returns 2), then a circle test of `min(w,h)*0.5 + [player+0x774c]` against the distance to
`player+0x7730`. On contact it spawns two effects and writes `player+0x7898 = 2`, guarded by
`player+0x7898 == 0`, so a second contact in the same frame cannot kill twice.

**Graze is once per bullet** (`+0x618`), so repeated passes cannot inflate the counter, score
or effect spawns.

State switch arms:

| State | VM | What the arm does | Where it goes |
| --- | --- | --- | --- |
| 0 | | empty slot (caught at `0x10970`) | `0x1156d` |
| 1 | `+0x50` | acceleration, turning, scripted patterns | **falls through to `0x11021`**: generic motion, cull, collision, graze |
| 2 | `+0x170` | spawn-in: `pos += vel * 1/2`, steps the VM | `0x11562` |
| 3 | `+0x290` | spawn-in: `pos += vel * 1/2.5`, steps the VM | `0x11562` |
| 4 | `+0x3b0` | spawn-in: `pos += vel * 1/3`, steps the VM | `0x11562` |
| 5 | `+0x4d0` | cancel animation: own scaled step (`pos += vel*0.5`), steps the VM | `0x11562`, or `0x112b5` when the script ends |
| other | | | `0x11562` |

Only state 1 reaches the generic motion, the cull and the player collision. States 2–5 move
the bullet themselves and leave the loop body early; the stock game never grazes or kills with
them.

### Gates

Each gate relay tests `proj_minor` first, then either runs the relocated bytes and resumes or
jumps to `skip`. The flag test comes first so the relocated instructions' flags survive.

| Site (field) | Block | On a sub-step pass |
| --- | --- | --- |
| `0x10982` (`proj_states`), resume `0x1098b` | state switch | re-runs the three relocated instructions for their flags, then `je 0x11021` (state 1) else `jmp 0x11562` (`proj_states_other`) |
| `0x11298` (`proj_offscreen`), resume `0x1129f` | off-screen counter `+0x604` | not incremented |
| `0x11562` (`proj_timer`), resume `0x1156d` | per-bullet age | not advanced |
| `0x11714` (`proj_laser_timer`), resume `0x1171b`, skip `0x1172f` | laser timer and VM step | skipped |
| `0x11796` (`proj_epoch`), resume `0x1179f` | manager frame counter; the relay also sets `proj_ran` | not advanced |

```text
minor:  movzx edx,[rbx+0x44]     ; the switch's own three instructions,
        mov   ecx,edx            ; re-run for the flags (they only read)
        sub   ecx,r12d
        je    0x11021            ; state 1: motion, cull, collision, graze
        jmp   0x11562            ; everything else: the age gate, which jumps to 0x1156d
```

A gate with an `other` target re-runs its relocated bytes on the minor branch and chooses
between `skip` and `other`. Gates without one emit the same bytes as before that field existed.

**Trap: gating a switch is not gating a block.** An unconditional jump to `0x11021` on a minor
pass is right for state 1 only. For state 2 the native pass moves `vel/2` and the minor passes
add a further `vel`: three times the intended speed during spawn-in (state 3: 3.5×, state 4:
4×, state 5 likewise), independent of rate (at 120 Hz it is still `vel/2 + vel`). It shows as
a larger radial spread of freshly spawned rings. It also runs graze and the hit test on
spawning bullets. Before relocating a dispatch, enumerate its arms and where each one goes.

**Trap: a coarse gate from the switch entry straight to `0x1102c`/`0x11021` without the
register effects fails.** Later code reads `xmm6`, `xmm7` and `xmm8` (the bullet's radius and
position), which the skipped region establishes. Gate per site.

**Motion.** The block at `0x1102c` is rewritten, not relocated: per axis `movss xmmN,[vel];
mulss xmmN,[rip+proj_dt]; addss xmmN,[pos]; movss [pos],xmmN`, with the `mov rax,[rbx+0x150]`
load kept after the first axis, then `jmp 0x11060`. Only the first five bytes of the 52 are
replaced (patch limit 32); the remaining 47 are unreachable. Both halves (`0x1102c`, `0x11046`,
26 bytes each) are frozen. With the feature off `proj_dt` is `1.0`; multiplying by one is
exact, so the result is bit-identical to the three adds (checked with 2^24, 0.1+0.2 and
denormals).

**Redirected calls.** Two calls are redirected to C functions that do nothing when
`proj_minor` is set, because nothing of them belongs on a sub-step pass:

- `0x108e7` → `item_update` ([§15](#15-items)).
- `0x1154e` → `sprite_vm_step` (`queue_call(game->proj_sprite_call, sprite_vm_step)`),
  returning `1`. Ungated, every live bullet's ANM script
  advances a whole frame per pass, six per game frame at 360 Hz. Scripts that only select a
  sprite look unchanged; scripts that move or fade their sprite (cancel bursts) travel six
  times as far and fade six times as fast. A script step is not scaled by `dt`: ANM
  instructions are scheduled in whole frames. It cannot be a gate because its block contains
  the RIP-relative `mov` at `0x11543`. Both `0x1154e` and `0x69b0` are frozen. The graze spawn
  call at `0x1152d` is a different `call 0x69b0` and is not touched.

**Trap: a gate is wrong at its edges.** The item call sits just before the bullet loop and the
sprite step just after the range that was read (`0x11434`–`0x11543`), four instructions
before the age gate. When gating a loop body, read to the branch. A callback is also not what
it is named after: "the projectile manager" updates items first. Re-read the remaining
sub-step surface for anything a gated function does before or after the part it was gated for.

### Accounting

Same τ bookkeeping as the player (`subtick_slice`). The native pass runs with `proj_dt = 0`
and contributes no motion; the slices of a frame sum to exactly one frame. The finishing
slice is applied at the top of the next boundary, before that frame's logic, so at every
60 Hz boundary bullets are where the stock game puts them and the state machine and spawner
read stock positions. The pass stands aside when the setting is off, the rate is 60, the
relays are absent, the draw guard has failed, or the last native pass did not reach the
manager counter (`proj_ran` clear: paused, between stages, menus).

Toggling off mid-frame gives that frame its slices plus a whole native step: at most one extra
frame of bullet travel, once. Toggling on costs nothing. Positions accumulate across slices,
so they can differ from stock by a few units in the last place: far below any hitbox and
unbiased, but not bit-identical.

Bullets fired by an enemy still appear on frame boundaries. Measured: exactly 6.00 projectile
passes per frame at 360 Hz. Log line: `substep on|standing by: N projectile passes (…/s, … per
frame), N item updates and N bullet animation steps held back`.

### The schedule in `substep.h`

`substep.h` is an exact partition for an engine that decides itself when the 60 Hz logic
runs. `hfr64.c` includes it but currently derives slices from the QPC phase through
`subtick_slice`; `substep.h` is tested and available for a list-level sub-step.

The x86 schedule lets a tick straddle a frame boundary. Here that would apply part of the next
frame's motion before that frame's logic runs: the steps between two boundary ticks summed to
1.25 frames at 144 Hz. Instead, one Bresenham deals the second's R ticks out to its 60 frames,
and a second deals each frame's 256 units (1/256 frame, exact in float32, as is every partial
sum) to that frame's ticks, ceiling-first so the last tick lands on the boundary. At 144 Hz
frames get 2 or 3 ticks; at 60 every frame gets one tick of one frame, so the mechanism is
inert.

## 14. Lasers

A laser is a beam from a fixed origin. The head `[rbx]` grows by the speed `[rbx+0x258]` at
`0x1161d`; the tail `[rbx-4]` follows once the gap exceeds `[rbx+0x274]`, by the game's own
rule. Scaling that one add (`movss xmm1,[rbx+0x258]; mulss xmm1,[rip+proj_dt]; addss xmm1,[rbx]`,
12 bytes, resume `0x11629`) sub-steps the whole beam and its collision. The per-laser timer
and animation step (`0x11714`: timer pair `[rbx-0xc]`/`[rbx-8]`, then `call 0x69b0` at
`0x1172a`) are one gate that skips to `0x1172f`.

The laser state machine needs no gate: every transition is driven by the timer reaching a
duration and resets it, so with the timer frozen between native ticks no transition fires
twice. There is no gate at the loop head `0x11604`; skipping the loop wholesale was replaced
by the two patches above.

Laser hit test: `0x6aba0`, called only from `0x11883`. It rotates the player position into the
laser's frame (`sin`/`cos` at `0x2be07d`/`0x2be071`), tests the radius `0x506aec` against a
rotated box, and writes the dying state at `0x6ad3e`. With bullets ([§13](#13-sub-stepped-bullets))
this makes everything that can kill, graze or cancel a bullet sub-tick.

## 15. Items

Pool: 1024 entries of `0x160` at `0xbaf0d8`. Position `+0x10` (`0xbaf0e8`), velocity `+0x1c`,
type `+0x34`, sprite VM `+0x38` (first VM `0xbaf110`; `0x11940` reads `0xbaf1d8` =
`0xbaf110 + 0xc8`, confirming base and VM offset). Update `0x42980`, single caller `0x108e7`
in the prologue of `0x10870`:

```c
pos += vel;                               /* x, y, z */
if (vel.y >= 3.0) vel.y = 3.0;            /* terminal */
else               vel.y += 0.03;         /* gravity  */
if (pos.y >= 464.0) despawn;
```

An auto-collect branch re-aims the velocity at the player at a flat speed of 8, and a `switch`
on the type awards power, points, lives and bombs.

Items are gated, not scaled: their motion is inseparable from the scoring and collection the
same pass performs. `item_call` (`0x108e7`) is redirected to `item_update` in `hfr64.c`,
which calls `0x42980` only when `proj_minor` is clear. Ungated, items fall, accumulate gravity and test
collection once per sub-step (six times a frame at 360 Hz). Each item's sprite is an ordinary
VM, so it is still interpolated. The log reports `N item updates … held back`.

**`0x3cc50` is not the item physics.** The update node `12@3cc50` is gated by the pause flag
and calls `0x3fca0` and `0x3eee0` (one caller each, 60 Hz only). `0x3fca0` contains a
0x120-stride loop clamping `[rbx+8]` toward `[rbx+4]` by ±0.01/0.02 that looks like fall
integration. It is the HUD: the stride is an ANM VM and the surrounding code formats
`"BONUS %8d"`. Method that finds the real site: search for float fields incremented by a
constant below 0.5. There are two in the binary: `0x373b0` (`+= 0.3`) and `0x42980`
(`+= 0.03`).

## 16. Systems left at 60 Hz

Enemies, the player's shots, items and effects are not sub-stepped, because their discrete
effects land once per 60 Hz frame:

- **Player shots.** Motion at `0x69a36`. Damage is applied on the enemy side at `0x37a6c`
  per overlapping box, and nothing consumes the shot there, so the test must stay at 60 Hz or
  damage multiplies by the sub-step count. With damage at 60 Hz and slices summing to one
  frame, sub-stepping the motion cannot change an outcome.
- **Enemies.** Motion at `0x374e9` is followed by an optional clamp to per-enemy bounds. A
  slice pass must reproduce the clamp or bounded enemies overshoot and snap back once a frame.
  TH10–13 also interpolate enemy sprites instead of sub-stepping enemies.
- **Items and effects.** Same argument; for items see [§15](#15-items).

Sub-stepping these would replace interpolated drawn positions with exact ones: a rendering
improvement only. The gameplay-relevant sub-step work (player, bullets, lasers) is complete.

The one functional gap against TH10–13 is replay support for sub-tick input
([§18](#18-replays)).

## 17. Dimming

Dimming fades what competes with bullets. The x86 implementation is described at the top of
`src/core/dimming.c`; it must flush sprite batches around each classified VM and fade the vertex colours (or
`D3DRS_TEXTUREFACTOR`) of the resulting `DrawPrimitiveUP`. Here the
runtime already wraps every VM draw, so it scales the VM's colour at `+0xec` before the draw
and restores it afterwards. No D3D11 hooks, no flushing. INI keys are `[video] dim_<class>`,
0–100 (see the [README](../../README.md)).

**Alpha is the top byte, `+0xef`** (`0xAARRGGBB`): the bullet draw does
`or dword [vm+0xec], 0xffffff`, forcing the low three bytes and leaving the fourth, and the
ANM interpreter writes `[vm+0xef]` alone at `0x6b1e`.

- Most classes fade alpha. The backend cannot read a VM's blend mode, so **additive effects do
  not fade**. Known gap.
- **Background** scales colour, not alpha: it is drawn over the playfield's own fill, so
  lowering alpha changes nothing. The x86 backend does the same for this class. It is one
  branch in `dim_fade_colour`; no D3D11 quad is drawn. Fallback if a
  stage draws its background through a blend mode where colour scaling is inert: the game's
  rect filler `0x75be0`, which `0x6a130` uses for the bomb darkener with the playfield rect
  `(236,16)-(620,464)` and `alpha<<24`.

**Classification.** The draw dispatch at `0x3c030` is relocated to a relay that stores the
node pointer (`rbx`) before the callback and clears it after. It pushes nothing and `mov` sets
no flags, so the callback sees its usual stack and `cmp eax,2` reads the callback's result.
Pools are checked before callback rules.

| Callback | Priority | What it draws | Class |
| --- | --- | --- | --- |
| `0x78290` | 5 | stage background layers 0 and 1 | background (rule) |
| `0x78390` | 6 | stage background layers 2 and 3 | background (rule) |
| `0x6a130` | 7 | bomb/death screen darkener: a filled rect, no sprites | none |
| `0x6a210` | 9 | player shot pool entries of type 1, **then the player's own VM (`player+0x78c8`, fed from `+0x7730`) and the focus sprite** | none (shots by pool) |
| `0x6a430` | 11 | the same pool, entries of type 2 | none (shots by pool) |
| `0x38290` | 10 | enemies | none |
| `0x2b310` | 12 | the 512-entry effect pool | effects (rule) |
| `0x11940` | 14 | bullets, lasers **and items** | none (items by pool) |
| `0x3cd20` | 15 | HUD digits | none |

Pools (`DimPool`: a VM matches when its address is the first VM plus an exact multiple of the
stride, below the count):

| First VM | Stride | Count | Class | Why a pool |
| --- | --- | --- | --- | --- |
| `0xbaf110` | `0x160` | 1024 | items | shares `0x11940` with bullets |
| `0x4ff7c8` | `0x170` | 80 | player shots | `0x6a210` also draws the player. `0x4ff7c8 + n * 0x170, n < 80` ends at `0x506ac8`, below the position `0x506ad0` and the player VM `0x506c68` |

**Trap:** a callback rule on `0x6a210` fades the player. The function continues after its
loop: `mov eax,[rdi+0x7730]` (`pl_position`) into `[rdi+0x78c8 + 0xc8]`, the player's VM. A callback rule requires a reading of the whole function showing it draws one thing;
any class that shares a callback needs a pool. (The same fault exists in a different form in
the x86 TH10–12 rules, where fading effects also faded the hitbox.)

New Classic has no `DIM_SPECIAL` class; the menu shows that slider disabled.
`UI_DIM_CLASSES` is a read-only bitmask of the classes a game can fade, and the menu disables
sliders with no rule and says why. `UI_DIM_AVAILABLE` also counts pools.
`tools/test_fixed_profile.py` requires every rule to name a function entry with a `.pdata`
record and every pool to lie inside the image.

Sample census line (sprites per callback per stats window), which is what the table was built
from: `sprites by callback: 12@2b310=1840 14@11940=9021 10@38290=402 ...`. Counts noted in
`th06nc.c` for a two-second window at 144 Hz: `0x78290` ≈ 26000, `0x78390` ≈ 720.

## 18. Replays

| RVA | Finding |
| --- | --- |
| `0xa6ec38` | replay-manager pointer |
| `0x6b220` | replay setup/registration; mode-specific callbacks |
| `0x6bfd0` | recording initialization; allocates a `0x78` byte header |
| `0x6c1b0` | playback initialization and stage state restore |
| `0x6af60` | file load, decoding, validation and offset relocation |
| `0x6b110` | checksum helper called by the loader |
| `0x6b970` | node callback: appends an entry when the input word changes. Registered by `lea` at `0x6b307` inside `0x6b24f`; seen live as `15@6b970` in a stage |
| `0x6ba40` | node callback; registered by `lea` at `0x6b401` inside `0x6b24f` |
| `0x6b9f0` | node callback: walks a table by frame number and writes the input global. Registered by `lea` at `0x6b521` inside `0x6b50f` |

Header magic **`T6RP`** (little-endian `0x50523654`), 16-bit version **`0x010f`** at `+0x04`;
the loader checks both. It decodes bytes from file offset `0x13` onward by subtracting a
rolling byte initially read at `+0x12` and increased by seven per byte, checks a checksum at
`+0x0c`, then relocates seven non-zero 64-bit stage offsets beginning at `+0x40` into
pointers. The save path and full record layout are not audited. Resource strings:
`replay/th6_00.rpy`, `replay/th6_%.2d.rpy`. The filename pattern does not establish
compatibility with original TH06 replays. The patch appends no USER metadata and does not
alter recording cadence. No cross-version or HFR replay playback has been tested.

The game records every run. The native format stores one input word per 60 Hz frame, so a run
recorded with `subtick` or `substep` on does not play back faithfully (sub-tick positions
cannot be described; sub-stepped positions are not bit-identical and collisions differ).

**There is no guard against replay playback.** With either feature on, playback drives the
player from the device as well as from the file. It cannot damage the replay, since playback
does not write. Turn both off by hand before recording or watching one. Score runs should
leave both off.

**Trap: `0x4f27b2` is not a playback flag.** It is zero in menus and the title screen and
non-zero while a stage runs (measured with the `idle:` diagnostic; five zero readings, all
from menus). A gate on it disabled `subtick` and `substep` completely: across 42 stats windows
(~40 s of play, `subtick=1, substep=1`, `guard=ok`, presenting at 360 and 480) neither
feature ran once; removing that one term took both to exactly 6.00 sub-steps per frame at
360 Hz. The identification came from `inspect_pe.py xrefs`, which reported readers in
`0x6b970`, `0x6b9f0` and `0x6ba40`; `xrefs64.py` finds a single reference, in the FPS display,
and no writer (so it is written through a register). Whether the three callbacks touch the
byte at all is unverified. The byte is kept in the profile as `replay_suspect` and printed by
the `idle:` line.

Finding the real playback flag needs a live check. Way in: whichever of the three
callbacks writes the input global runs only during playback, so the byte its registering
code tests is the flag. Closing the replay gap fully needs a sidecar carrying the sub-frame input stream, read
back on playback, which is what TH10–13 do in their own format.

## 19. F11 menu, configuration and diagnostics

The overlay (`overlay_dx11.cpp`) takes device and window from the swap chain passed to
Present, creates a backbuffer view for the current presentation, and releases view and
backbuffer afterwards. It saves all output-merger targets and the depth view; ImGui preserves
the rest of the pipeline. No retained backbuffer reference blocks native resizing.
Device/window changes rebuild the overlay. Input uses the shared key state machine
(`menu_key.h`) so messages plus polling cannot double-toggle.

DxLib hides the OS cursor despite `SetCursor`, so the fixed backend reports
`UI_SOFTWARE_CURSOR` and ImGui draws a software cursor while the menu is open (owner-confirmed).
D3D9 keeps its existing behaviour.

`UI_VIDEO_AVAILABLE` means the scaler and window controls have a backend; the x64 runtime has
none. **Trap:** an early `return` on `!UI_VIDEO_AVAILABLE` at the top of
`draw_display_section` also hid the dimming sliders. The dimming block is its own function
called from both paths, and the message names only what is missing. Pixel-art scaling,
sharpening, internal resolution, texture magnification and window controls are not ported; use
the game's display settings.

```ini
[hfr]
fps=0            ; 0 = primary display rate; else clamped 60-1000
debug=0
[fixed60]
interpolate=1
vsync=0          ; D3D11 sync interval; independent of x86 [hfr] vsync
subtick=0        ; sub-tick player movement; inert at 60 Hz; not replay-safe
substep=0        ; sub-stepped bullets and lasers; inert at 60 Hz; not replay-safe
diag_seconds=0   ; run N seconds, log the node lists, quit. Leave at 0
[video]
menu_key=122
dim_<class>=0    ; 0-100
```

F11 → Timing changes presentation rate, interpolation, `subtick` and `substep` live;
F11 → Presentation changes VSync. Save writes these keys without replacing unrelated settings.

Log (`touhou_hfr.log`, always written), every two seconds:

- `stats seconds=… presents=… updates=… frames=… ticks=… samples=… blends=… guard=ok|FAILED api=2`
- `sprites: N calls, N off, N stack, N sampled, N blended`
- `sprites by callback: <prio>@<rva>=<count> …`
- `dimming: N sprites faded`
- `registered update list: …` / `registered draw list: …`, printed when the registered set
  changes. It is a read-only walk of both sentinels; each node is validated with
  `VirtualQuery` before it is dereferenced.
- `idle: subtick=… substep=… rate=… guard=… player_ran=… proj_ran=… suspect[4f27b2]=…`,
  whenever a feature is switched on but did nothing for a whole window. An enabled, idle
  feature is a bug.
- the `substep` and `subtick` lines ([§13](#13-sub-stepped-bullets),
  [§12](#12-sub-tick-player-movement)), printed only when their counters are non-zero.

## 20. Autoload through dxgi.dll, and Proton

`src/proxy_dxgi.c` builds a `dxgi.dll` placed beside the game. DxLib loads `dxgi.dll` by bare
name ([§4](#4-rendering-backend-dxlib-on-direct3d-11)) and the executable's directory is
searched before System32, so the game loads it however it was started. It forwards all 57
exports of the real library and, on the first factory call (`CreateDXGIFactory`, `1` or `2`),
outside the loader lock, loads `touhou_hfr64.dll` and calls `hfr_start`. It does nothing if the
runtime DLL is absent. ReShade and Special K use the same file name.

### Proton: factory bypass

Reported setup: Proton `experimental-11.0-20260910b-x86_64`, DXVK `v3.1-12-g8759acd15dc79c8`,
launch options `WINEDLLOVERRIDES="dxgi=n,b" %command%`; game runs, HFR has no effect, F11 does
nothing.

Evidence from `steam-4659620.log` (follow the game's thread `013c`; other processes such as
`xalia.exe` have their own module lists): line 415 loads the game-folder `dxgi.dll` as native
at `0x6ffff9ed0000`; lines 416-417 load System32's native D3D11 and DXGI, the latter at
`0x6ffffc930000`, a different module; DXVK then creates device and swap chain; no load record
for `touhou_hfr64.dll`. The diagnostic proxy's `touhou_hfr_proxy.log` then shows exactly five
startup records (attachment, system-DXGI load request, distinct system/proxy handles, resolved
system factories, `factory hooks ready`) and **no intercepted factory**; `steam-4659620 (1).log`
confirms them at lines 417-422, followed by DXVK's `CreateDXGIFactory2` warning. The failure is
a bypass of the proxy's factory wrappers, not a missing override, runtime dependency or INI.

Cause: DxLib resolves its factory with `GetProcAddress` (import `0x2c0090`) on the handle its
own `LoadLibraryW("dxgi.dll")` returned, and under Proton that handle is System32's DXGI. Why
the loader selects that module is an inference (Proton's
[loader](https://github.com/ValveSoftware/wine/blob/experimental_11.0/dlls/ntdll/loader.c)
keeps a cached module consulted in basename lookup); the fix does not depend on it.

Fix in `proxy_dxgi.c`: the proxy temporarily observes **only the main executable's
`GetProcAddress` import**, found by PE import names. It saves the existing pointer (including
an earlier mod's hook). When a successful lookup asks the already-loaded system DXGI module
for one of the three factory entry points, the observer restores the previous import and calls
the same `start_runtime` the factory wrappers use, then returns the resolved address and
LastError unchanged. Other modules, names, ordinals and failed lookups pass through. The
ordinary factory path and an explicit unload also remove the observer. Restoration is a
compare/exchange, so a hook installed later is not overwritten. No DXGI export table or code
bytes are rewritten, no thread is started, and nothing moves into `DllMain`. It needs no new
runtime DLL, settings, fingerprint or Linux-specific backend. A final F11/gameplay check on the
affected Proton installation is still outstanding.

### Tests and diagnostic build

- `tools/test_dxgi_proxy_start.c` tests the real `dxgi.dll` basename with a stand-in runtime in
  an isolated directory. Each factory is the first call in a separate process; initialization
  must happen exactly once. `--game-order` loads D3D11 first, then DXGI by bare name (DXVK's
  import ordering). `--system-factory` resolves from System32 to reproduce the bypass without
  relying on loader cache behaviour: it fails with the old proxy and passes with the fix for
  Factory, Factory1 and Factory2. Also verified: an earlier lookup hook is still called;
  returned pointers and LastError survive; unrelated, failed and ordinal lookups do not start
  HFR; the import is restored; unloading the unused proxy. The system-factory and unload cases
  run in `test64.ps1` and `test64.sh`. Never ship the stand-in `touhou_hfr64.dll`.
- The forwarding test compares `DXGIGetDebugInterface1` with identical arguments. It must not
  compare two calls to `DXGIDeclareAdapterRemovalSupport`, which changes process state and
  gives false failures. Forwarding under a renamed DLL does not substitute for the startup test.
- [Wine 11.0 WoW64](https://github.com/Kron4ek/Wine-Builds/releases/tag/11.0) with native
  [DXVK 3.1.1](https://github.com/doitsujin/dxvk/releases/tag/v3.1.1) in an isolated WSL
  prefix loads two distinct DXGI modules. The D3D11-first test still resolves through the
  proxy there, so it does not reproduce the report. A probe resolving Factory2 from System32
  records `system factory lookup intercepted`, import restoration, the real runtime loading
  and entry into `hfr_start`; the runtime rejects the probe's fingerprint. That host has no
  usable Vulkan/display stack: DXVK cannot create the factory and repeating factory creation
  after that failure crashes it, so none of this is a graphics or gameplay test.
- Loading the real runtime from WSL's `/mnt/c` faults in Wine's TLS loader; the same files on
  the Linux filesystem work. Not established as any user's cause.
- `tools/build_dxgi_diagnostic.ps1` builds the proxy with `HFR_PROXY_DIAGNOSTICS` and creates
  `build/touhou-hfr-proton-factory-fix-2026-09-16.zip` (only `dxgi.dll` and instructions; its
  trace says `factory-fallback build`; the earlier logging-only ZIP is distinct). The
  diagnostic writes `touhou_hfr_proxy.log` beside itself and duplicates messages through
  `OutputDebugStringA`, preserving `GetLastError`. It records process/thread, attachment,
  system DXGI path and handle, resolved factories, entry into each wrapper, the runtime path,
  `LoadLibrary` errors (missing runtime reports error 126), a missing `hfr_start` export or
  its return value. Normal builds contain no tracing or extra file I/O. The runtime's own log
  begins only inside `hfr_start`.

To collect evidence from a user: back up the installed proxy, replace only `dxgi.dll`, launch
with the options below, reach the title screen, try F11, quit, and collect the proxy log,
`touhou_hfr.log` if present, and the Proton log. Debug messages also reach Proton's log, so a
missing file log is still informative.

```text
PROTON_LOG=1 WINEDEBUG=+loaddll,+debugstr WINEDLLOVERRIDES="dxgi=n,b" %command%
```

| Last completed stage | Next step |
| --- | --- |
| Attached, hooks ready, no intercepted factory | find which DXGI handle and function address the game uses |
| Runtime load attempted but failed | use the exact path and Win32 error; do not assume a bypass |
| Runtime started | follow the runtime log's fingerprint/signature/hook result |
| No diagnostic attachment at all | verify the replacement DLL is the one Steam launched with |

## 21. Build, tests and validation

```powershell
.\build.ps1       # common launcher and existing x86 games
.\build64.ps1     # x64 DLL/helper, separate build/obj64 directory
.\test64.ps1 -GameExe 'C:\Program Files (x86)\Steam\steamapps\common\th06nc\'
.\package.ps1 -Version '0.4.12-th06nc-prototype' -IncludeExperimental64
```

POSIX equivalents: `build64.sh`, `test64.sh`. The optional package contains both
architectures, source, shaders and third-party licences; the x64 files are
`touhou_hfr64.exe`, `touhou_hfr64.dll` and `dxgi.dll`, installed beside `th06nc.exe` with
`touhou_hfr.ini` (keep an existing INI); `touhou_hfr.exe` is the launcher route. Builds emit
existing ImGui array-bound warnings tied to the repository's non-terminating assertion handler.

**Running the x64 tests under Wine.** The default `wine` may be the 32-bit loader, which fails
PE32+ with `Bad EXE format`. `/usr/lib/wine/wine64` with its own prefix runs everything;
`test64.sh` finds it and `tools/test_fixed_profile.py` honours `RUN64`.

`test64` covers:

- Clock at 60/120/144/165/240/360/480/1000: blocked presentation, phase bounds, long stalls.
- History: births, reuse, teleports, gaps, multiple submissions; prediction against
  interpolation; rotation wrap and bit-identical non-rotating sprites.
- Slices: sum to one frame; stand aside when unarmed, stalled or moving backwards. Direction
  table including right-over-left, up-over-down, focus and both diagonals. Clamp.
- `substep.h` at the same eight rates: exactly 60 boundary ticks and R ticks per second, every
  step a whole number of 1/256 frames, each frame's steps summing to exactly 1.0, phase
  strictly increasing within a frame and 0 on a boundary, no drift over a second.
- Patch transaction against an inert synthetic image: `test_patches` asserts
  `g_patch_count==17`, before/after bytes, the protections `VirtualProtect` applied, and
  writes through the data-page pointers. The count is updated by hand on purpose, so a patch
  cannot appear unnoticed.
- Unicorn executes every emitted AMD64 relay and checks registers and stack balance:
  both branches of the post-update and wait relays; the movement relay (factor scales both
  axes, facing store survives, ran byte set); bullet motion at step lengths 1.0, 0.25 and 0
  (positions, the descriptor load, the `xmm2`/`xmm1` the cull reads, nothing else clobbered);
  the state switch in both directions, including that ZF from `sub ecx,r12d` survives to the
  `je` at its resume, and the state-2..5 path (`tools/test_fixed_stubs.py`); the off-screen
  counter, per-bullet timer and manager counter incrementing only on a full pass; the laser
  relays; both redirected calls landing on their C function and standing aside when
  `proj_minor` is set; the draw dispatch (node recorded during the call, argument still in
  `rcx`, one return address on the stack, result preserved).
- All signatures match the supplied file. Both launchers accept its hash and reject a non-PE
  and a temporary copy with a changed update-call byte; the source executable is re-hashed
  afterwards. The profile test runs the launchers under Wine when the host is not Windows.
  Regression tests do not launch a game process.
- `tools/test_menu_logic.cpp` runs the menu sections headlessly (ImGui with no backend, a
  built font atlas and a display size) against a stub recording which settings each section
  reads; a drawn control reads its value. It asserts every dimming control appears with and
  without a video backend, the scaler's only with one, and that a game with no dimming rules
  still gets disabled sliders with a reason. Reinstating the early-out fails it with ten named
  lines. `tools/test_menu.c` renders on a real Direct3D 9 device and only proves no crash.
- TH10/11/12/13 native harnesses, machine-code regressions and launcher checks pass with the
  shared-menu extraction.

In-game results:

- Copied-game runs measure about **144 presentations/sec with 60 updates/sec**, and **360/60**
  after the owner changed settings. Logs show moving-sprite submissions and non-zero
  interpolation counts. Owner reports: the game runs fine, F11 works, the cursor is usable,
  interpolation looks right. `subtick` and `substep` run at 6.00 sub-steps per frame at
  360 Hz. Item fall speed, spawn-ring spread and faded-player faults were reported by the owner
  and fixed ([§15](#15-items), [§13](#13-sub-stepped-bullets), [§17](#17-dimming)).
- Native baseline (unpatched copy under an x64 MinGW debug helper, `DEBUG_ONLY_THIS_PROCESS`,
  base from `CREATE_PROCESS_DEBUG_EVENT`, counting software breakpoints with single-step
  rearm; about ten seconds at startup/menu, no input):

| Observation | Probe 1 | Probe 2 (extra COM dispatch breakpoint) |
| --- | --- | --- |
| Update runner hits | 601 | 602 |
| Draw runner hits | 601 | 601 |
| Frame-finalizer hits | 601 | 601 |
| First-to-last finalizer span | 10,000 ms | 9,984 ms |
| D3D11 creation wrapper hits | 1 | 1 |
| Shared COM slot-8 thunk hits | not instrumented | 620, including unrelated calls |
| Graphics API selector | not sampled | 2 (D3D11) |

  Counts include both endpoints (600 intervals in ten seconds). Probe 2's extra update at
  cutoff is not a steady 602:601 ratio. These are debugger-overhead observations, not a
  frame-time benchmark. No second-chance exception; a first-chance `0x406d1388` thread-name
  exception was passed through. Executable hashes were rechecked afterwards.

## 22. Reproducing the analysis

`tools/porting/inspect_pe.py` is read-only, supports i386 and AMD64, and needs Python,
`pefile` and `capstone`:

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

For references use `tools/porting/xrefs64.py <exe> <rva> …` ([§3](#3-address-convention-and-analysis-tools)).
The tool was validated on: PE32+ inventory and unchanged hashes for both executables; PE32
inventory against an installed TH12; bounded ASCII/UTF-16 string results; the known FPS-string
and finalizer-call cross-references; equivalent RVA/VA disassembly of the integer bullet timer;
and rejection of a range in non-file-backed zero-initialized storage.

Ghidra, with your own paths and a private output directory (fresh project; for an existing
project use Ghidra's process workflow instead of overwriting). No original-game x86
function-repair script is applied.

```powershell
$env:JAVA_HOME = 'C:\Program Files\Java\jdk-23'
& '..\toolchain\ghidra_11.3.2_PUBLIC\support\analyzeHeadless.bat' `
    '..\analysis\nc' nc -import $ncExe -scriptPath "$PWD\tools" `
    -postScript ExportAll.java "$PWD\..\analysis\nc\decomp.c"
```

Native observation: use a **copy** of the game and an x64 debugger. Break/count at `+0x3be80`,
`+0x3bf70`, `+0x3c330` and `+0x270020`, inspect the pacing globals after initialization, and
for presentation use `+0x25a1f1` or filter `+0x26fff0` by return address `base+0x25a1f6`.
Record RCX/EDX/R8D and the selector at `base+0x8fe20c`.

Local working files kept outside git, relative to the checkout (conveniences, not inputs):

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

The same directory holds `prototype-144-startup.log`, `prototype-144-first-menu.log` and the
cursor-fix logs. **Do not commit game executables, copied game assets, full disassembly or
decompilation exports.**

## 23. Open items

Not tested or validated:

- No native-versus-patched comparison of gameplay state, RNG or replay results over identical
  inputs. This is the acceptance criterion for calling the game supported, and for enabling
  any sub-step feature by default.
- No systematic in-game check of `subtick`/`substep`: player stops at the playfield edges;
  focus and diagonal speeds feel stock; a held direction covers the stock distance; pause,
  unpause, death and bombs do not displace the player; bullet speed overall; graze counts
  roughly match a stock run; animations not fast; lasers normal; a replay recorded with both
  off plays back byte-for-byte. Dying to things that used to miss is the feature working.
- All characters and shot types, bosses, bombs, deaths, item collection, pause/resume,
  unlimited-lives mode, replay recording/playback and fast-forward; resizing, fullscreen,
  alt-tab, multiple monitors, device/resource recreation; forced D3D9; awkward display rates
  such as 144 Hz as well as multiples of 60. A numeric FPS counter or a smooth title screen is
  not evidence.
- The Proton fix on the affected installation ([§20](#20-autoload-through-dxgidll-and-proton)).

Not implemented:

- A replay-playback guard and a sub-frame input sidecar ([§18](#18-replays)).
- Smoothing of animation frames, colour fades and 3D backgrounds; sub-stepping the ANM
  interpreter `0x69b0` ([§10](#10-pose-history-and-sprite-smoothing)).
- Fading additive effects ([§17](#17-dimming)).
- D3D11 video features: scaling, filters, sharpening, internal resolution, window controls.
- A wider draw guard, including the item pool, and an audit of draw-triggered state changes
  ([§11](#11-draw-guard)).

Not mapped:

- The exact DxLib version; swap-chain slot and output lifetimes.
- The trigger of the conditional extra-update path at `0x45dc6`.
- The replay save path and record layout; the real playback flag; what `0x4f27b2`'s writer is.
- The input producer and replay override path.
- The complete laser layout; node field `+0x30`; nodes `0x75af0`/`0x76280`.
- The `player+0x7898` conflict ([§7](#7-engine-objects)).
- A Steam-verified executable fingerprint and short stock replays from the owner.
