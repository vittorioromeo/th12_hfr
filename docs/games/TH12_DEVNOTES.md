# TH12 (Undefined Fantastic Object) developer notes

Reverse-engineering record for **TH12 v1.00b**, the source of `src/games/th12.c`. The project
began as the single-game patch `th12_hfr` (legacy INI `th12_hfr.ini`), so this file also holds the
original sub-stepping design (§4, §5) that the shared runtime still implements. The current
runtime is described in [ARCHITECTURE.md](../../ARCHITECTURE.md), its reasoning in
[DEVNOTES_RUNTIME.md](../DEVNOTES_RUNTIME.md); where this file and `src/` disagree, the code is
right. TH13 is documented as a delta against this file in [TH13_DEVNOTES.md](TH13_DEVNOTES.md).

Everything was reverse-engineered from `th12.exe` v1.00b. The English static patch `th12e.exe`
has the same code layout (byte-identical at every patch site), so every address applies to both.
Addresses are virtual addresses with the default image base 0x400000; the executable is not
relocatable, so they are stable. Identity: replay magic `t12r`, D3DX `d3dx9_40.dll`, 69 frozen
signatures (`src/games/th12_signatures.h`). Assumes familiarity with x86, Win32 and Direct3D 9.

## 1. Address map (th12 v1.00b, th12.exe = th12e.exe)

| address | what |
|---|---|
| 0x44f560 | main loop; frame calls at 0x44f881 / 0x44f89e / 0x44f8aa |
| 0x450600 / 0x4503f0 / 0x450080 | frame functions (vsync / limiter variants) |
| 0x450720 | present (+ latency sleep `cmp byte [0x4cead3],1` at 0x450729) |
| 0x4624c0 / 0x462620 | update runner / draw runner; their `ret`s at 0x4625fb and 0x462722 |
| 0x462380 / 0x462420 / 0x462890 | register update / register draw / remove node |
| 0x4ce89c | runner object (update list +0x18, draw list +0x3c, "scene ending" flag +0x48) |
| 0x4cf0f8 / 0x4cf218 | runner critical section / depth (used when 0x4cee78 & 0x8000) |
| 0x4b2ed0 | global game speed (float) |
| 0x464a80 / 0x464a20 / 0x4067e0 | Timer tick / add / set |
| 0x4931e0 | `ftol` |
| 0x462ec0 / 0x462a80 / 0x465440 / 0x40f690 | input poll / joystick / raw edge derivation / game edge derivation |
| 0x4d48b8 | raw input block (0x130 bytes, see 3.4); 0x4d49d0 game input word |
| 0x4cee78 | misc engine flags (0x400 DirectInput keyboard, 0x800 DirectInput joystick, 0x8000 threaded) |
| 0x4ceae8 | option flags (0x200 "hold shot to focus") |
| 0x4cf3fc | window active |
| 0x4b4518 | ReplayManager (see 3.5); nodes 0x43c510 record → 0x43b7e0, 0x43c520 playback → 0x43b950 |
| 0x43bc10 / 0x43c350 / 0x43c590 | replay save / load / stage start |
| 0x433444 0x434459 0x43519b 0x448e4f | replay save call sites |
| 0x43b1d2 / 0x43b439 | playback load call sites (modes 1 and 2; `replay_load_calls` in `th12.c`) |
| 0x4b44e8 | GameManager (flags +0x60: 0x10/0x20/0x40 pause kinds, 0x800 stage loading) |
| 0x4b4510 | pause menu object |
| 0x4cee40 | scene id; 0x4311e0 scene transition; 0x40f720(4) = continue |
| 0x4b4514 | Player (timer +0xa30, gather counter +0xc418, focus +0xc598) |
| 0x437660 | Player update; 0x4367ca/0x4367dd fixed-point `ftol`; 0x439ed0 shot-vs-enemy test |
| 0x464db0 | MotionState::step (pos += vel at 0x464dbc) |
| 0x4b43dc | EnemyManager (list +0x68); enemy: vm ids +0x1120, offsets +0x1168, parents +0x1220, pos +0x1074, flags +0x26f8 |
| 0x467170 / 0x4154d0 | ECL VM core / th12 instruction switch (ins_447 store at 0x4193e4) |
| 0x40a1f0 | BulletManager update; bullet stride 0x9f8, timer +0x4e4 |
| 0x427380 | ItemManager update; 0x4a3fb8 = 0.2 (double) |
| 0x4283d0 | LaserManager update; vtables 0x4a0654 / 0x4a06ac / 0x4a0704 |
| 0x403ec0 | Stage update; distortion RNG at 0x403145, counter at 0x4036dd |
| 0x4ce8cc | AnmManager; 0x461920 get VM by id; 0x455630 AnmVm::update; lists +0x8856b8 / +0x8856c0 |
| 0x4107e0 / 0x45dcd0 | death-ring / scrolling-mesh ANM callbacks |
| 0x4ceaac / 0x4ceaa8 | pause veil tint VM / flash VM |
| 0x4ce8f0 / 0x4ce9dc / 0x4cf428 | D3D device / present parameters / window flags |
| 0x4cf2a0 | last frame time (double) |
| 0x4cee34 / 0x4cee38 / 0x4cec04 | per-frame context pointer / flag set by the frame function before the runner / context value (`frame_context_value`) |
| 0x464c40 / 0x4cf0d8 | scene teardown (ESI = 0x4cf0d8) |
| 0x42fca0 / 0x450891 | screenshot routine / its call site (`screenshot_fn`, `screenshot_call`) |

TH12 has no F10 size cycle of its own (`native_size_cycle = 0`): the window procedure swallows
`SC_KEYMENU`, and nothing in the executable tests `VK_F10`.

## 2. Toolchain and workflow

Development runs on Linux with a cross compiler against a copy of the binary; the game runs on
the tester's Windows machine.

**Static analysis.** Ghidra 11.3.2, headless:

```
analyzeHeadless /home/claude/proj th12 -import th12.exe -postScript FixFuncs.java -postScript ExportAll.java
```

- `tools/FixFuncs.java` creates the functions auto-analysis misses: one after every run of `int3`
  padding and one at every direct `call` target without a function. ZUN's MSVC builds reach many
  `__thiscall`/`__fastcall` helpers only through vtables or computed calls; without this pass a
  few hundred functions are missing.
- `tools/ExportAll.java` decompiles every function into one file (`decomp.c`, 3519 functions for
  th12) for `grep`; `tools/fn.py` prints the decompiled function containing an address.
- `objdump -d -M intel` of `.text` (`th12.asm`) gives exact bytes; every patch site is verified
  against its original bytes.
- `pefile` + `capstone` scripts do pattern scans, e.g. `tools/scan_reg2.py` (3.2).
- `th12.dat` is unpacked with `thdat` (thtk); ECL is decompiled with `thecl -d 12`. The scripts
  are not modified; which instructions touch timers and the game speed decides what can be
  sub-stepped.

**References.** Everything not listed here (the update-runner protocol, the game-speed float,
the per-system frame assumptions) comes from the binary.

* **thprac** (`thprac_th12.cpp`) — manager singletons, player/enemy/bullet struct offsets, ECL
  details.
* **OpenInputLagPatch** — the Direct3D 9Ex approach (managed-pool conversion, `CreateDeviceEx`,
  `SetMaximumFrameLatency`), the th12 main-loop hook site, notes on thcrap interactions.
* **thtk / truth** — ECL/ANM decompilers and the th12 instruction tables.

**Building.** The current build is `build.sh` / `build.ps1`
([ARCHITECTURE.md](../../ARCHITECTURE.md)); `src/hfr.c` is the unity-build entry. The original
single-game build was

```
i686-w64-mingw32-gcc -O2 -Wall -Wno-unused-function -shared -static-libgcc -o th12_hfr.dll hfr.c -ld3d9 -lwinmm -Wl,--kill-at
```

- `--kill-at` is required: the DLL exports `DirectInput8Create` so it can be dropped in as
  `dinput8.dll`, and the export name must not carry the `@20` stdcall decoration.
- `src/launcher.c` is a `CreateProcess(CREATE_SUSPENDED)` + `CreateRemoteThread(LoadLibraryA)`
  injector for users who already have another `dinput8.dll` proxy (vpatch, thcrap's own, ...).
  A named mutex prevents a double install (once as `dinput8.dll`, once injected).
- Guard stubs (5.2) are emitted at runtime by byte-emitting macros into a `VirtualAlloc`'d RWX
  page: most jump back into the middle of a game function and need absolute addresses of the
  DLL's globals. Inline asm is used only for the `naked` trampolines that need the FPU stack
  (5.1) and for calling game functions with register arguments.

**Log.** The game cannot run where the code is written, so bugs are diagnosed from the log
(`th12_hfr.log` originally, now `touhou_hfr.log`) and the tester's report. It records the
configuration, every patch site that did not match its expected bytes, the display mode, and
per-5-second statistics (presents/s, ticks/s, catch-up and skipped ticks, sub and frame node
calls, long frame gaps). `debug=1` adds periodic dumps of game state (game speed, pause object
state, the pause veil sprite's colour, the complete list of live UI sprite VMs, ...).

## 3. Engine anatomy (th12, applicable to th10–th13 with different addresses)

### 3.1 Main loop and frame functions

`FUN_44f560` is the message loop. Each iteration pumps messages, calls
`IDirect3DDevice9::TestCooperativeLevel`, then calls one of three per-frame functions depending
on the "input latency" option: `FUN_450600` (vsync-driven, "fast"), `FUN_4503f0` and
`FUN_450080` (the two software-limited modes). The three calls are at `0x44f89e`, `0x44f8aa` and
`0x44f881`; all three are hooked (`hfr_frame`). Each frame function, in order:

1. runs the update runner (`FUN_4624c0`); if it returned 0/-1, tears down the scene
   (`FUN_464c40`) and returns 1/2 to the main loop;
2. runs the draw runner (`FUN_462620`) between `BeginScene`/`EndScene`;
3. presents (`FUN_450720`, which also contains a `Sleep` for the game's own latency setting,
   disabled by the patch);
4. measures the frame time into the double at `0x4cf2a0`.

### 3.2 Update and draw runners ("UpdateFunc" / "DrawFunc")

Every game system registers callbacks with a runner object (`DAT_004ce89c`). Update callbacks
form a linked list at `runner+0x18`, draw callbacks at `runner+0x3c`; both are sorted by
priority.

```
struct UpdateFunc {
    int      priority;      // +0x00
    uint32_t flags;         // +0x04  bit 1 (0x2) = enabled
    NodeFn   func;          // +0x08  __thiscall (ECX = arg)
    void*    on_register;   // +0x0c
    NodeFn   on_cleanup;    // +0x10
    struct { UpdateFunc* entry; ListNode* next; ListNode* prev; } node;   // +0x14 embedded
    void*    arg;           // +0x20
};
```

The list node is embedded, not a pointer: guessing a pointer puts `arg` at +0x18 instead of
+0x20 and crashes at startup. Confirm struct offsets from the registration function's stores and
pin them with `_Static_assert`.

Registration is `FUN_462380` (update, priority in EBX, node in ESI) and `FUN_462420` (draw);
removal `FUN_462890`. The runner (`FUN_4624c0`) walks the list, skips disabled nodes, and
interprets the callback's return value: 0 remove the node, 1 continue, 2 call the same node again,
3 stop walking the list ("cut"), 4 or 8 make the runner return 0 (scene ends), 5 make it return
-1, 6 restart from the head, 7 call the node's cleanup and continue. A critical section
(`0x4cf0f8`, with a depth byte at `0x4cf218`) wraps the walk when `DAT_004cee78 & 0x8000`.

Update priorities (from `tools/scan_reg2.py`, which disassembles backwards from every
`call 0x462380` to find the preceding `mov ebx, prio` and `mov [reg+8], func`). Label a node by
reading its function, not from neighbouring priorities: 0x406bb0 was first labelled
"PlayerShots", is the Bomb node, and sub-stepping it ran bombs six times too fast.

| priority | function | system |
|---|---|---|
| 0x01 | 0x42f000 | "Supervisor": polls input (`FUN_462ec0`), scene changes |
| 0x08 | 0x460c40 | AnmManager (world sprites, "unaffected by slow-mo" handling) |
| 0x0a | 0x422bd0 | GameManager: pause, stage flow, returns 3 while paused |
| 0x0b | 0x43c510 / 0x43c520 | replay record node / replay playback node |
| 0x0c | 0x403ec0 | Stage (3D background) |
| 0x10 | 0x437660 | Player (movement, shots, hit test against bullets) |
| 0x11 | 0x406bb0 | Bomb |
| 0x12 | 0x413210 | EnemyManager (ECL VMs) |
| 0x13 | 0x44a860 | UfoManager |
| 0x14 | 0x4283d0 | LaserManager |
| 0x15 | 0x40a1f0 | BulletManager |
| 0x16 | 0x427380 | ItemManager |
| 0x17 | 0x40e040 | Spellcard |
| 0x19 | 0x41f8d0 | Gui / HUD |
| 0x1a | 0x424190 | player bomb effects (`"PlayerBomb?"` in `th12_classes[]`) |
| (n/a) | 0x460c30 | AnmManager UI list |

**Draw list** (used by the dimming; method in [DEVNOTES_RUNTIME.md](../DEVNOTES_RUNTIME.md) §3b).

| item | value |
|---|---|
| draw runner | `0x462634`; dispatch `0x462691`: `mov ecx,[esi+0x20]; mov eax,[esi+8]; call eax` |
| sprite batch flush | `0x45a3c0` (ESI = AnmManager, pointer at `0x4ce8cc`; pending-quad count at manager+0x4b56a0) |
| sprite VM draw | `0x45c900` (VM in EAX, 0x4b4 bytes) |
| VM fields | loaded-ANM pointer +0x3f8, sprite layer +0x20, script index +0x3ea (16-bit), slot +0x3e6, sprite id +0x3e4 |
| loaded ANM | begins with its slot index and its file name |

Draw priorities (decimal, from a `debug=1` trace):

| prio | what |
|---|---|
| 1 | `0x42f0b0` binds the offscreen stage target |
| 2, 5 | the Stage's 3D passes |
| 4..11 | sprite layers 0..4 |
| **12** | `0x42f200` binds the world target (**`world_prio`**) |
| 14 | `0x42f3b0` copies the stage into it |
| 13, 16..20 | sprite layers 5..9 |
| 21 / 22 | EnemyManager / UfoManager |
| 23 | layer 10 |
| 24 | Player |
| 25 / 26 | layers 11 / 12 |
| **27** | ItemManager |
| 29 / 31 | LaserManager / BulletManager |
| 33 | `0x40fbd0` |
| 34 | layer 16 |
| 35 | Bomb |
| 37 / 39 | stage effects copies |
| 41..45 | layers and Gui |
| 48 | `0x42f320` back to the back buffer |
| 49 | `0x42f470` the final copy |
| 50 on | the interface |

What draws where:

- `bullet.anm` carries the items (layer 10) and the effects. Bullets are the BulletManager's own
  draws (31) with their VMs' layer left at 0. Layer 16 (priority 34) looks like "the bullets". It
  is not: it holds the enemy death bursts (the coloured disc and its rotating ring, scripts
  78-152, the block that sets `ins_68(16)`), the bullet-cancel sparks, and the player's hitbox
  (scripts 76-77, `DIM_NONE` in `th12_dim_rules`).
- `enemy.anm`: layer 7 is spawn flashes and auras *and the UFOs* (scripts 135-138, the one place
  a script-range rule is needed); layers 8/9 are the enemies.
- The player's shots are `pl0X.anm` on layers 11 and 13; its body sets no layer.

### 3.3 Timers and the global game speed

The engine has a global float **game speed** at `0x4b2ed0`, used for the boss-death slow motion
(ECL instruction 447 sets it to 0.5 or 0.25). Every `Timer`

```
struct Timer { int prev; int cur; float cur_f; float* speed; uint32_t flags; };   // 0x14 bytes
```

advances through `FUN_464a80` ("tick", ESI = timer): `if 0.99 < speed < 1.01 { cur++; cur_f += 1 }
else { cur_f += speed; cur = ftol(cur_f) }`. `FUN_464a20` adds a value times speed,
`FUN_4067e0` sets. Most integrators (`pos += vel * speed`) already multiply by the speed because
slow motion requires it, so setting the float to a fraction advances most of the engine by a
fraction of a frame. This is the basis of the patch; the places that do not consult the float
are in 5.2.

Because of the `0.99..1.01` fast path, timers count in integers at speed exactly 1.0 and
accumulate in float at any other speed. Sub-steps are therefore chosen to be exactly
representable (4.3).

### 3.4 Input

`FUN_462ec0` polls the keyboard (`GetKeyboardState`, or the DirectInput keyboard at
`DAT_004ce908` when `DAT_004cee78 & 0x400`) and the joystick (`FUN_462a80`: `joyGetPosEx` or the
DirectInput device at `DAT_004ce90c`) and returns the button word in EAX. Bits: 0x01 shot,
0x02 bomb, 0x08 focus, 0x10/0x20/0x40/0x80 up/down/left/right, 0x200 skip; 0x400 is a derived
"shot+focus pressed together" bit. The Supervisor node calls it once per frame and stores the
result in a 0x130-byte input block at `0x4d48b8`:

```
0x4d48b8 cur   0x4d48bc prev   0x4d48c0 repeat   0x4d48c4 pressed   0x4d48c8 released
0x4d48cc .. 0x4d4948   32 per-key hold counters (raw)          0x4d49e4 raw "held > 7 frames" mask
0x4d494c .. 0x4d49c8   32 per-key hold counters (game input)   0x4d49cc auto-focus counter
0x4d49d0 game cur   0x4d49d4 prev   0x4d49d8 repeat   0x4d49dc pressed   0x4d49e0 released   0x4d49e8 held mask
```

`FUN_465440` derives prev/pressed/released/repeat for the raw block, `FUN_40f690` for the game
block. The replay record node copies raw `cur` into game `cur` every frame (synthesizing the
focus bit when the "hold shot to focus" option `DAT_004ceae8 & 0x200` is on) and appends 6 bytes
per frame to the stage record; the playback node writes game `cur`/pressed from the recording
instead. The player reads the game block only. Menus read the raw block.

### 3.5 Replay manager

`DAT_004b4518` points to the ReplayManager.

| offset | field |
|---|---|
| `+0x10` | mode: 0 recording, 1 playing, 2 info-only load by the replay list |
| `+0x20 + stage*4` | per-stage record pointers (non-null for stages in the replay) |
| `+0x1d0` | frame counter within the stage: -1 before the stage starts, set to 0 by the stage-start routine `FUN_43c590`, incremented by the record/playback node |
| `+0x1d8` | stage index |

Saving is `FUN_43bc10` (fastcall, filename in ECX), called from four sites; playback loading is
`FUN_43c350` (called at `0x43b1d2`). File format (th10+): a 0x24-byte header (`t12r`, user-data
offset at +0xc, compressed size at +0x1c, decompressed size at +0x20), the compressed body, then
`USER` chunks (`"USER"`, u32 size, u8 type, 3 pad bytes, payload). The game reads only its own
two chunk types from the user-data offset, so chunks with other type bytes can be appended. The
original patch appended type 0x48 (recording rate, text) and 0x49 (per-tick inputs, binary); the
current chunk set is in [ARCHITECTURE.md](../../ARCHITECTURE.md) ("Replay extensions").

### 3.6 Objects the patch touches

*Player* (`DAT_004b4514`): state timer at `+0xa30..+0xa38` (a `Timer`), focus flag `+0xc598`,
gather counter `+0xc418`. The position is not read directly. Movement code in `FUN_4367xx`
integrates in 16.16 fixed point with `ftol(vel * speed)` (5.3). Player shots are a separate array
walked from `FUN_436f80`/`FUN_439b10` and use the shared `MotionState::step` (`FUN_464db0`).

*Enemies* (`DAT_004b43dc` EnemyManager, list of `{enemy, next, prev}` nodes at `+0x68`): ANM VM
ids at `+0x1120` (14 slots), sprite offsets `+0x1168`, parent slot indices `+0x1220`, position
`+0x1074`, hp `+0x2648`, flags `+0x26f8` (0x01000000 = being deleted, 0x02000000 = "sprite
positions are absolute" mode). The ECL VM core is `FUN_467170` with the th12-specific
instruction switch in `FUN_4154d0`.

*Bullets* (BulletManager, update `0x40a1f0`): 0x9f8-byte entries, position `+0x4bc`, timer
`+0x4e4`, sprite VM embedded at `+0x8`; per-frame counters at `+0x4` and `+0x520`.

*Items*: timer `+0x988`, states 1–5 in `FUN_425xxx`, the "+0.2 per frame" gravity constant at
`0x4a3fb8`, the UFO attraction acceleration at `+0x9bc`, a state-5 countdown at `+0x9c0`.

*Lasers*: three classes with vtables `0x4a0654` (line), `0x4a06ac` (curve), `0x4a0704` (beam),
update at vtable slot 2, per-object timers at `+0x14/+0x18` (and `+0x28/+0x2c` graze timer for
line/curve), an "ex wait" counter at `+0x44c`.

*ANM VMs* (`AnmManager` `DAT_004ce8cc`; `FUN_461920` looks a VM up by id, EDX = manager, id on
the stack): timer `+0x68`, colour `+0x3bc`, sprite index `+0x3ea`, instruction pointer `+0x3f0`,
pending interrupt `+0x3c4`, position `+0x430`, flags `+0x47c`. `FUN_455630` is `AnmVm::update`;
the world and UI lists live at `AnmManager+0x8856b8` and `+0x8856c0`. The pause "veil" is a
full-screen tint quad (`DAT_004ceaac`) drawn at priority 0x31; the pause menu's draw function
(`FUN_432260`) copies its colour from a child sprite and resets it to `0xffffffff` after each
draw.

*Direct3D*: device pointer `DAT_004ce8f0`, present parameters `DAT_004ce9dc`, window flags
`DAT_004cf428`; `Direct3DCreate9` is imported through the IAT (and called directly at
`0x44f6fc`).

## 4. Design: a true high-rate tick

The requirement: run at 144/240/360 Hz "without changing the game logic and speed".

*Render interpolation* (draw objects between the last two 60 Hz frames) is safe and generic but
adds one frame of display latency, interpolates through direction changes, and does nothing for
input latency or hit precision. *A true high-rate tick* runs the simulation more often with a
smaller step: lower input latency and more precise collisions, at the cost of finding every
place that assumes "one call = one frame". The patch does the latter for the player, player
shots, bullets, lasers, items, the 3D stage and all sprite animation. Scripted enemies stay
frame-locked and are interpolated for display (5.5); input is polled every tick (5.7).

### 4.1 Classification: SUB or FRAME

Every update node is **SUB** (may be called with a fractional game speed) or **FRAME** (runs
exactly once per 60 Hz frame with speed 1.0).

- SUB: continuous state (positions, velocities, timers that multiply by the speed float) and
  discrete events triggered by timers, not by counting calls. Bullets, lasers, items, the player,
  the 3D stage and the ANM interpreter qualify because slow motion already requires it.
- FRAME: the system counts calls (`counter++`, `x % 60 == 0` per invocation), consumes the RNG
  per call (more calls change the random sequence and therefore the stage), drives discrete game
  flow (menus, pause, stage transitions, spell card timing), or is the ECL interpreter. ECL
  scripts are frame-counted programs: `wait N` means N calls, enemies move by per-call increments
  computed from script constants, and `frame % n` logic is common (§8 item 3).

The table is `th12_classes[]` in `src/games/th12.c` (`g_classes[]` in the shared code). Unknown
nodes default to FRAME. Each entry can be flipped from the INI (`[systems]`), which lets a tester
bisect a bug by turning systems off one at a time.

### 4.2 The tick model

The engine's frame remains the unit of game logic. The update pass runs at the display rate
(R ticks/s); each tick has a length `dt` in frames such that 60 frames elapse every R ticks. On
a **frame boundary** ("major") tick, where the integer frame counter increments, FRAME nodes are
called with speed 1.0 and behave as in the original. On the other ("minor") ticks only SUB nodes
are called, with speed `dt`.

`hfr_runner` reimplements `FUN_4624c0` (jumped to from its first bytes) with the same
return-code protocol plus the classification, the speed factor and the state in 5.5. Draw
callbacks run every tick unchanged, and every tick is presented.

### 4.3 Dyadic Bresenham sub-steps

`dt = 60/R` is not representable in float for most R (360 gives 0.1666…). Adding the rounded
value to `Timer::cur_f` every tick makes the sum after six ticks differ from 1.0, so the integer
part flips a tick early or late, and events keyed on timers (shot cadence, item states, ANM
`wait`s) drift and jitter. Instead a frame is 256 units and tick lengths are integer unit counts
from a Bresenham sequence: at 360 Hz the steps are 42, 43, 43, 42, 43, 43 (sum 256 = one frame).
All partial sums of multiples of 1/256 are exact in float, so every timer crosses every integer
on the tick it should, with zero drift. The same mechanism handles rates that do not divide 60
(144 Hz: 106, 107, 107, … units; 2.4 ticks per frame on average) and a logic rate different from
the present rate (replays). An "add 1e-5 bias" alternative masks the drift without fixing it.

The sequence restarts at the first frame of every stage (5.6), so the sub-step pattern of a
frame depends only on the frame number.

### 4.4 Frame pacing

Ticks are scheduled against the wall clock, not counted per present. `hfr_frame` keeps an origin
`g_t0` and a count of ticks run, computes how many ticks should have run by now, and:

- runs one extra update-only tick (no draw) when the deficit exceeds 6;
- holds one back (present without updating) when it is below -6;
- re-anchors instead of catching up when the deficit is beyond ±60 (stall, alt-tab, clock jump).

The hysteresis of 6 exists because the DWM present queue delivers presents in bursts of two or
three. Per-present catch-up logic read each burst as missed frames and ran the game up to 40%
too fast (v0.7b). Do not infer elapsed time from present callbacks under a compositor.

With vsync the presents pace the loop. When vsync does not pace it (rate measured 8% above the
display rate for a second) a software limiter takes over. An earlier `Sleep(1)` limiter overshot
vblanks and ran the game 5% slow. The game's own frame limiter and its "input latency" sleep are
disabled. `timeBeginPeriod(1)` is requested.

The ratio between logic ticks and presents is a second Bresenham (`ticks_for_slot`), so the logic
rate can be lower than the display rate: a 144 Hz replay on a 360 Hz display runs 144 logic ticks
per second and repeats frames.

## 5. Patch sites

Every stub is installed only if the bytes at the site match the expected original; a mismatch is
logged and skipped.

### 5.1 The game speed float and who writes it

The effective speed the engine sees is `logical × factor`. `logical` is the game's own value
(1.0, or the ECL slow-motion value); `factor` is 1.0 for FRAME nodes and `dt` for SUB nodes, set
by the runner before every callback. The game writes the float at 21 sites, all of the form
`fstp dword [0x4b2ed0]`. Each hooked site is redirected to a small naked trampoline
(`fstp [tmp]; pushad; call C-handler; popad; ret`) that updates `logical` and rewrites the
effective value (`th12_speed_sites[]`):

| group | sites | handling |
|---|---|---|
| permanent 1.0 (stage/boss reset) | 0x421d5f 0x4222a0 0x42f56d 0x436ed7 0x4653fc | `logical = 1` |
| temporary 1.0 (Stage / AnmVm "ignore slow-mo", restored later from a saved effective value) | 0x4030fc 0x455670 | write `factor` only |
| pause: save and set 1.0 | 0x432835 0x43293c 0x433853 0x4339a2 | shadow ← logical; logical = 1 |
| pause: restore | 0x432988 0x433a1d 0x4348ed | logical ← shadow |
| ECL ins_447 | 0x4193e4 | logical = value |
| save/restore of the effective value, literal 0.0 for items | 0x403123 0x42840c 0x42841b 0x455b3e 0x4586f4 0x45871f | untouched |

The pause groups matter because the pause code saves the current value and restores it later: if
it saved `dt` it would restore a speed of 0.17 and the game would crawl.

### 5.2 Per-frame assumptions inside SUB systems

Systems that honour the speed float still contain code that assumes one call per frame. Find it
by reading the system's update function and searching for increments, modulo operations, RNG
calls and constants added without a speed multiply. Two kinds of stub cover almost everything.

**Scale**: an increment proportional to time gets a multiply inserted (`fmul dword [g_factor]`,
or by the speed float for the item sites).

| site | what | multiplier |
|---|---|---|
| 0x437016 (20 bytes) | player shot `pos += vel` | `g_factor` |
| 0x436fe2, 0x439b72 | player shot `speed += accel` (`+0x14 += +0x18`) in the two shot loops (`FUN_436f80`, `FUN_439b10`); `th12.c` comments 0x436fe2 as angle += angular velocity | `g_factor` |
| 0x464dbc (27 bytes) | shared `MotionState::step` (player shots, damage sources; enemies and bombs run with factor 1) | `g_factor` |
| 0x425fc5, 0x426080, 0x426218 | items' `+= 0.2` gravity | speed float `0x4b2ed0` |
| 0x426926 | item UFO attraction acceleration | speed float `0x4b2ed0` |

The `g_factor` sites use the factor rather than the effective speed deliberately: they did not
scale with slow motion in the original either.

**Gate**: a per-call event happens only on the tick where the object's own `Timer` crossed an
integer (`prev != cur`, exactly once per frame by construction), or only on a frame-boundary
tick when the object has no ticking timer.

| site | what | gate |
|---|---|---|
| 0x436dd9 | player death particles | player timer |
| 0x4374fc | the `state_timer % 60` counter | player timer |
| 0x4368f7 | option gather counter | player timer |
| 0x409fdb | bullet per-frame counters | bullet timer `+0x4e4` |
| 0x425c5c | item state-5 countdown | frame boundary (state-5 items do not tick their timer) |
| 0x42979a, 0x42c90c, 0x42adef | laser "ex wait" counters (line, curve, beam) | laser timer `+0x14/+0x18` |
| 0x429a55, 0x42cbf7, 0x42b068 | laser graze-every-3-frames (line, curve, beam) | graze timer `+0x28/+0x2c`; beam `+0x14/+0x18` |
| 0x403145, 0x4036dd | stage distortion effect (consumes RNG every frame) and its counter | frame boundary |
| 0x410814 | enemy death-ring effect callback | effect timer `+0xc/+0x10` |
| 0x45dcd0 | scrolling-mesh effect callback (UV scroll) | VM timer `+0x68/+0x6c` |

Gate on the object's timer, not the global frame boundary: an object created mid-frame has its
own timer phase, and a global gate moves its per-frame events relative to the original.

**Constant `Timer::add` arguments.** Two `Timer::add` call sites take a *script constant in
frames* (the shot cycle `timer -= 14` at 0x439ac2 and the ANM `wait` helper at 0x43adbd). They
are redirected to a version that multiplies by `logical` instead of the effective speed, i.e.
stock behaviour; otherwise the constant is scaled twice and shot cadence is wrong. Check every
`Timer::add` argument: rate or constant.

**Curved laser (v0.4.12).** `LaserCurve::update` (`0x42c770`, ESI = the laser) keeps its trail
as a ring of nodes (five floats each — x, y, z, angle, width — at `+0xf9c`, count at `+0x470`).
Every call it shifts the ring by one node (`0x42c925`) and adds a whole frame's velocity
(`+0x5c..+0x64`, recomputed by the "ex" behaviours from angle and speed) to the head node with no
speed multiply (`0x42c965`). It is the one motion in the game that ignores the speed float (line
and beam lasers, bullets and items all multiply), so under sub-stepping a curved laser advanced N
times per frame and its trail streamed out N times as long: Nazrin's and Shou's lasers sped up
with the tick rate. It is easy to miss when auditing the laser classes' timers because the head's
`+=` is written against the node array, not the object. Fix: shift the ring only on the tick
where the laser's graze timer (`+0x28/+0x2c`, ticked at the end of the same update) crossed a
whole frame, and add `velocity × factor` every tick (25 bytes at `0x42c965`). The nodes are the
head's position at each whole frame, so between shifts the head glides from the last node towards
the next frame's position, which the trail geometry already assumes. TH13 computes each node from
a float timer through the laser's motion segments and needs nothing. TH10 and TH11 have no curved
laser class (`LaserCurveInf` appears in the TH12 and TH13 binaries only).

**Player shot behaviours (v0.4.12).** The shot-type table at `0x4aebd8` holds five per-shot
callbacks called with EDX = the shot:

| callback | per-call behaviour | handling |
|---|---|---|
| `0x43a480` homing | turn towards the target, speed ±0.2 | gated on the shot's integer timer (`+0/+4`) |
| `0x43a810` | stop at the enemy's height, timer-state checks | gated |
| `0x43aa50` gravity | speed −0.1 | gated |
| `0x43ab60` | speed −0.38/−0.6 and angle += angular velocity | gated |
| `0x43a6b0` option laser | anchors the laser to the option | ungated; growth (`+28` per call towards 448, `0x43a750`, constant at `0x4a4140`) scaled by the factor |

Ungated, ReimuA's homing and the other behaviours turn and accelerate per sub-tick. TH11 gates
its two on the shot's timer the same way. TH13's table (`0x4bb4d8`: homing `0x446cb0`,
`speed += 1` `0x447590`, `speed *= 0.8` `0x447510`) gets the same gate on its shot timer
`+0x18/+0x1c` ([TH13_DEVNOTES.md](TH13_DEVNOTES.md) §6). Open in both games: the option laser's
angle smoothing (`angle += (target − angle) × 0.1`) is still per call and turns N times faster;
the effect is small.

### 5.3 The player's fixed-point movement

The player integrates movement as `pos += ftol(vel * speed)` in 16.16 fixed point (0x4367ca,
0x4367dd). Truncating per sub-step loses up to one unit per tick; at 360 Hz focused movement is
1.6% slow. The `ftol` calls are replaced by a stub that carries the truncation residual to the
next step, so the sum over a frame matches the original to within one unit. Any `ftol`/integer
conversion inside an integrator needs the same treatment.

### 5.4 Enemy hit test guard

`FUN_439ed0` (player shot vs enemy) begins with "if the player's state timer did not change this
frame, no damage", a stock guard against double hits. With sub-steps the integer timer changes on
one tick in six, so enemies become nearly immune (v0.8). The guard (0x439ef2) is replaced by a
comparison of the player's float timer before/after the most recent Player update, which the
runner records around the Player node (`g_ptf_prev` / `g_ptf_cur`).

### 5.5 Frame-locked systems inside a sub-stepped frame

**Pause.** The pause menu works by GameManager returning 3, which cuts the update list before
Player, bullets and so on. GameManager is not called on minor ticks, so without help the cut does
not happen and bullets and animations keep moving while paused. The runner remembers which node
cut the list on the last boundary tick and cuts at the same node on the following minor ticks.

A pause raised mid-frame by a sub-stepped node (the game-over pause raised by the Player when the
last life is lost) is honoured immediately: on minor ticks the runner checks the GameManager's
pause flags (`GameManager+0x60 & 0x70`) when it reaches that node. Without this the Player node
keeps running for the rest of the frame after triggering game over, and each extra call creates
another copy of the pause background tiles. Only the last one receives the "close" interrupt, so
a translucent white veil stays on screen. Diagnosed from the `debug=1` UI VM dump: 5–6 copies of
the pause background tile VM after each game over. For a persistent visual artefact, dump object
lists; counts and creation patterns say more than colours and flags.

**Enemy interpolation.** Enemies (ECL) are FRAME nodes, so their positions change once per frame
and their sprites would stutter at 60 Hz. `enemy_interp` runs after every update pass. For every
live enemy it keeps the last two frame positions in a hash table keyed by enemy pointer (entries
expire when an enemy is not seen for two frames) and writes each of its 14 sprite VMs' positions
as `last - (last - prev) * (1 - alpha)` plus the sprite offset (and the parent VM's position for
attached sprites, plus the 224,16 playfield offset in the normal mode), with `alpha = phase + dt`.
Jumps larger than 48 px are treated as teleports and not interpolated. The sprites sit exactly at
the frame position on the last tick of the frame, so the interpolation adds no persistent offset.

### 5.6 Replays

A recording made with sub-stepping is not bit-identical to 60 Hz, so a replay must be played with
the tick sequence it was recorded with. The save hook appends a `USER` chunk with the logic rate;
the load hook reads it and `replay_check` switches the logic rate to the recorded one (or to
stock 60 when the chunk is absent) for the duration of playback.

At rates like 144 Hz the sub-step pattern of a frame depends on the phase of the Bresenham
sequence, which would depend on how long the game had been running. The runner therefore
restarts the sequence (`schedule_reset_here`) on the frame boundary tick on which the replay
record or playback node is about to run frame 0 of a stage (`ReplayManager+0x1d0 == 0`). From
then on the pattern of every frame is a function of the frame number only, in recording and
playback.

### 5.7 Sub-tick input

On every minor tick of an active frame (one where the replay node ran), the runner calls the
game's poll routine `FUN_462ec0` with the raw input block saved and restored around the call (so
per-frame edge detection for menus is not disturbed), and merges only the movement bits (0xf0)
and the focus bit (0x08) into the game input word `0x4d49d0`. The focus bit respects the "hold
shot to focus" option by checking the auto-focus counter the replay node maintains. The Player,
a SUB node, sees the new bits on the next tick. `joyGetPosEx` is IAT-hooked to return the last
frame's result between frames, since some joystick drivers make it slow.

For replays, the bits the player saw on every tick since the stage's first frame are appended to
a per-stage buffer (one byte per tick, 5 significant bits) and written at save time as a second
`USER` chunk (type 0x49), run-length encoded — typically a few kilobytes for a full run. During
playback the same stream is applied on minor ticks instead of polling. Ticks are counted only
while the frame is "active", so pauses (during which the replay node does not run) do not shift
the stream. Frame-boundary ticks keep the game's own recorded input, so a replay with the chunk
reproduces the sub-tick movement exactly, and one without it plays with the frame input held for
the whole frame. The log's "per-tick input available" line at each stage start confirms the chunk
was found.

### 5.8 Presentation: Direct3D 9Ex

`Direct3DCreate9` is IAT-hooked. With `d3d9ex=1` the hook calls `Direct3DCreate9Ex` from the
same module the game's import resolved to (so a wrapper `d3d9.dll` in the game folder, such as a
rotation wrapper, stays in the chain), and `CreateDevice`/`Reset` on the returned objects are
redirected to `CreateDeviceEx`/`ResetEx`.

- `SetMaximumFrameLatency(1)` after creation limits the driver's present queue to one frame,
  removing the up-to-two-frames of latency the default queue depth adds. The present interval
  stays `ONE` (vsync).
- Direct3D 9Ex has no managed pool. `CreateTexture`/`CreateVertexBuffer`/`CreateIndexBuffer`
  (device vtable slots 23, 26, 27) and the two D3DX loaders the game imports from `d3dx9_40.dll`
  (`D3DXCreateTexture`, `D3DXCreateTextureFromFileInMemoryEx`) are hooked to turn
  `D3DPOOL_MANAGED` into `D3DPOOL_DEFAULT | D3DUSAGE_DYNAMIC`, the conversion OpenInputLagPatch
  uses.
- The window style is refreshed with `SetWindowPos(SWP_SHOWWINDOW)` after device creation
  because 9Ex resets it.
- If `Direct3DCreate9Ex` is unavailable or the device does not answer a `QueryInterface` for
  `IDirect3DDevice9Ex`, everything falls back to plain Direct3D 9.

The refresh rate is read from `GetDisplayMode` after every device creation/reset (falling back
to `EnumDisplaySettings`) and drives the logic rate unless `fps` is set in the INI. In exclusive
fullscreen the requested refresh rate is written into the present parameters. Later Direct3D 9
rules are in [DEVNOTES_RUNTIME.md](../DEVNOTES_RUNTIME.md).

## 6. Traps

Traps tied to one site are with that site: the embedded list node and node labelling (3.2),
present bursts and `Sleep(1)` (4.4), pause save/restore (5.1), constant `Timer::add` arguments
and the curved laser (5.2), fixed-point truncation (5.3), the hit guard (5.4), the pause cut and
the game-over veil (5.5). The rest:

- **thcrap drops import hooks silently.** thcrap injects after the loader has finished (after
  this DLL's `DllMain` and install), walks the game's import table, matches by *name*, and chains
  to `GetProcAddress(dll, func)` instead of the pointer it replaced (`iat_detour_func` in
  `thcrap/src/mempatch.cpp`). `d3d9.dll!Direct3DCreate9`, which is how the patch obtains the
  device, was dropped while the log reported a successful install. Redirecting the exporting
  module's export table looks like the fix. It is not: the export table belongs to the whole
  process, including Steam's overlay, and Steam copies of TH10 crashed on the first frame with an
  access violation inside `ntdll` and nothing in the log (the non-Steam copy was fine). In use
  now: an import hook chains to whatever the slot held, and the imports are re-asserted once
  from `kernel32.dll!QueryPerformanceCounter`. The games call it before they ask for Direct3D
  (measured); no translation patch has a reason to touch it (no ANSI/Unicode pair, nothing shown
  to the player); the import table is never encrypted, so it can be armed while a DRM wrapper's
  stub is the only thing that has run, and the same trigger identifies a Steam release. The log
  lists every module in the process that did not come from Windows and says when an import has
  been taken back. `tools/check_patch_overlap.py` compares thcrap's game definitions against
  every byte this patch writes or verifies; for TH10–13 the two are disjoint. Full account:
  [DEVNOTES_RUNTIME.md](../DEVNOTES_RUNTIME.md) §5d.
- **Stub jump targets (v0.3 stage-start crash).** `hook_site` computed the jump target from the
  emit pointer *after* emitting the stub, so it pointed at the stub's end. `STUB_BEGIN()` records
  the start explicitly.
- **Register clobbers in inline asm.** `anm_get_vm` passes the manager in EDX and
  `call_with_esi` passes ESI; both need `"+d"`/`"+S"` in/out operands or GCC assumes the register
  survives.
- **ANM effect callbacks.** The mesh/ring effect callbacks and the stage distortion consume RNG
  or scroll UVs per call; they need per-frame gates (5.2) even though the ANM interpreter itself
  is SUB.

## 7. Verification

- Float exactness of the sub-step scheme: an offline Python model of the Timer update
  (accumulate `cur_f` in float32, compare the integer crossings against the ideal schedule) for
  every rate from 60 to 1000.
- Game speed: the log's stats line ("ticks/s") confirms it to four digits over minutes of play;
  the tester checks that stage and spell-card timers reach the same values as the original at the
  same wall-clock times.
- The `debug=1` VM dump (counts of live sprite VMs per list, their sprite ids and colours) found
  the game-over veil.
- Replays: record, play back, watch for desync. There is no other real test.

## 8. Known limitations and open items, in order of value

1. **Per-tick enemy hit tests.** Player shots hit enemies once per frame (the hit test runs
   inside the Player node against enemy positions that change once per frame). Running it every
   tick against the *interpolated* enemy position would give sub-frame hit timing; the damage
   bookkeeping in `FUN_439ed0` would need the same guard treatment as 5.4.
2. **Bullet-vs-player at interpolated enemy-relative positions.** Already per tick for bullets,
   which are sub-stepped; nothing to do unless enemies are sub-stepped.
3. **Full ECL sub-stepping.** Would make enemies and bosses truly high-rate. Needs the ECL
   interpreter's `wait`/time semantics (`FUN_467170`: float time += speed, jumps set integer
   time) to run with fractional speed; all per-call increments in the enemy update (`FUN_413xxx`,
   `FUN_4154d0` instruction handlers: movement modes, `frame % n` logic, RNG-consuming
   instructions) gated or scaled; and the spell-card/dialogue/boss-HP code kept frame-locked. The
   enemy update is ~2000 lines decompiled; the work is of the same kind as 5.1–5.4.
4. **Menus/HUD at high rate.** Cosmetic; the Gui node could be sub-stepped for its scrolling
   elements if the score/count-up code is gated.
5. **Replay robustness.** Playback at a different rate than recorded is not supported (the
   simulation differs slightly). A "resimulate at 60" fallback is not possible: the original
   60 Hz inputs are available only at frame granularity. Those are what the game records, so a
   patched replay still plays in an unpatched game, approximately.
6. **Option laser angle smoothing** is per call (5.2).

## 9. Porting to other Touhou games

th10 (MoF) through th13 (TD), and with more changes th14–th18, share this engine generation:
the runner protocol, `Timer`, the global game speed (introduced for slow motion), the input block
layout, the replay format with `USER` chunks, the ANM VM. th06–th09 are a different engine; only
the general approach applies. The profile procedure is [ADDING_A_GAME.md](../../ADDING_A_GAME.md);
the reverse-engineering steps, in order:

1. **Decompile.** Ghidra headless with the two scripts in `tools/` (adjust the `.text` end
   address in `FixFuncs.java` only if the CRT confuses it). Export to one file and use `fn.py`.
2. **Find the runner.** A function that reads a list head at `+0x18` of a global object and
   switches on a callback's return value with cases 0..8 (`case 6: restart` is distinctive). Its
   registration counterpart takes a priority in EBX. Run the equivalent of `scan_reg2.py` with the
   new registration addresses to get the priority table, then read each callback to label it.
   Confirm the `UpdateFunc` layout from the registration function's stores.
3. **Find the game speed float.** It is the float the ECL "set game speed" instruction stores
   (ins_447 in th12; look the number up in thtk's ECL table for the target game), and the one
   every `Timer` update compares with 0.99/1.01. List all `fstp` stores to it (`grep` the objdump
   for the address) and classify them into the groups of 5.1: literal 1.0 stores are either a
   reset (permanent) or paired with a save/restore (temporary); the pause code saves before
   setting.
4. **Hook the frame.** Locate the main loop by its `PeekMessage` loop and the three frame
   variants; hook all three. Disable the game's limiter and latency sleep (look for `Sleep` calls
   in the present function).
5. **Classify and audit.** Start with everything FRAME except bullets and enable one system at a
   time. Read each SUB candidate's update function top to bottom for: increments without a speed
   multiply, `%` on frame counters, RNG calls, `ftol` in integrators, `Timer::add` with constant
   arguments, and callbacks it invokes per call. Use the INI switches to bisect.
6. **Player.** The fixed-point movement and the shot code vary most between games; the hit-test
   guard (5.4) exists in the same form in th10–th13.
7. **Replays.** Save/load and the `USER` chunk convention are the same; only the magic (`t10r`,
   `t11r`, ...) and the addresses change. Find the ReplayManager's frame counter through the node
   that copies the raw input word into the game input word.
8. **Direct3D.** Identical; only the D3DX DLL version in the IAT changes (`d3dx9_NN.dll`). Later
   games (th14+) may use different texture creation paths — check the imports.

Reusable verbatim: the tick scheduler, the dyadic Bresenham, the runner reimplementation (given
the same protocol), the stub assembler, the enemy interpolation (given the enemy struct offsets),
the sub-tick input mechanism (given the input block layout), the replay chunk reader/writer, and
the Direct3D 9Ex layer.
