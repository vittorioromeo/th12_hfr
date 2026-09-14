> Historical notes for the original per-game build. The current unified runtime and source locations are documented in [ARCHITECTURE.md](ARCHITECTURE.md); installation instructions are in [README.md](README.md). Findings from the current work are in [DEVNOTES_RUNTIME.md](DEVNOTES_RUNTIME.md).

# th12_hfr developer notes

These notes document how the high-refresh-rate patch for Touhou 12 ~ Undefined Fantastic Object
(v1.00b) was built: the tooling, the parts of the engine that had to be understood, the design of
the patch, the bugs that were hit along the way, and what it would take to port the same
improvements to other Touhou games. They are written for someone who wants to modify this patch
or build the same thing for another ZUN game, and assume basic familiarity with x86, Win32 and
Direct3D 9.

Everything here was reverse-engineered from `th12.exe` v1.00b (the English static patch
`th12e.exe` has the same code layout, so every address below applies to both). Addresses are
virtual addresses with the default image base 0x400000; the executable is not relocatable, so
they are stable.

## 1. Goal and the two ways to get there

The request was to make the game run at 144/240/360 Hz "without changing the game logic and
speed". There are two fundamentally different ways to do that.

*Render interpolation* keeps the 60 Hz simulation untouched and draws every object at a position
interpolated between the last two simulated frames. It is safe and generic (it is what most
"high fps" patches for old games do), but it adds one frame of display latency, interpolates
straight through direction changes, and does nothing for input latency or hit precision.

*A true high-rate tick* runs the simulation itself more often with a smaller time step. It gives
lower input latency and more precise collisions, but the engine's per-frame code has to be
convinced to advance by a fraction of a frame, and every place that implicitly assumes "one call
= one frame" has to be found and dealt with. This is what the patch does for the systems where it
matters (player, player shots, bullets, lasers, items, the 3D stage, all sprite animation); the
scripted enemies stay frame-locked and are interpolated for display, and input is polled every
tick. Section 5 explains why that split was chosen.

## 2. Toolchain and workflow

The whole project was done on a Linux box with a cross compiler, against a copy of the game
binary, with the game itself running on the user's Windows machine. Nothing needed a Windows
development environment.

### 2.1 Static analysis

Ghidra 11.3.2 was used headless. The project was created and analysed with

```
analyzeHeadless /home/claude/proj th12 -import th12.exe -postScript FixFuncs.java -postScript ExportAll.java
```

`FixFuncs.java` (in `tools/`) creates functions that Ghidra's auto-analysis misses: it starts a
function after every run of `int3` padding bytes and at every direct `call` target that has no
function yet. ZUN's binaries are MSVC builds with lots of `__thiscall`/`__fastcall` helpers that
are reached only through vtables or computed calls, and without this pass a few hundred functions
are missing. `ExportAll.java` then decompiles every function into one big text file
(`decomp.c`, 3519 functions for th12). Having the whole decompilation in one file made grepping
for globals, constants and call sites far faster than clicking around a GUI; `tools/fn.py`
prints the decompiled function containing a given address.

Alongside the decompilation, an `objdump -d -M intel` listing of the whole `.text` section
(`th12.asm`) was used whenever the exact instruction bytes mattered (every patch site is verified
against its original bytes), and `pefile` + `capstone` scripts were used for pattern scans, for
example `tools/scan_reg2.py`, which finds every registration of an update or draw callback and
prints its priority and function pointer (see 3.2).

The game data (`th12.dat`) was unpacked with `thdat` from thtk, and the ECL scripts were
decompiled with `thecl -d 12`. The scripts themselves did not need changes in the end, but reading
them (in particular which instructions touch timers and the game speed) was necessary to decide
what could be sub-stepped safely.

### 2.2 References

Three open-source projects were used as starting points for engine layout and addresses:

* **thprac** (`thprac_th12.cpp`) — a large, well-tested set of th12 addresses: manager
  singletons, player/enemy/bullet struct offsets, ECL details.
* **OpenInputLagPatch** — the Direct3D 9Ex approach (managed-pool conversion, `CreateDeviceEx`,
  `SetMaximumFrameLatency`), the th12 main-loop hook site and its notes on thcrap interactions.
* **thtk / truth** — ECL/ANM decompilers and the instruction tables for th12.

Everything else, in particular the update-runner protocol, the game-speed float and the
per-system frame assumptions, was reverse-engineered from the binary.

### 2.3 Building

The patch is a single C file (`src/hfr.c`) built with mingw-w64:

```
i686-w64-mingw32-gcc -O2 -Wall -Wno-unused-function -shared -static-libgcc -o th12_hfr.dll hfr.c -ld3d9 -lwinmm -Wl,--kill-at
```

`--kill-at` matters: the DLL exports `DirectInput8Create` so that it can be dropped in as
`dinput8.dll`, and the export name must not carry the `@20` stdcall decoration. `src/launcher.c`
is a tiny `CreateProcess(CREATE_SUSPENDED)` + `CreateRemoteThread(LoadLibraryA)` injector for
people who already have another `dinput8.dll` proxy (vpatch, thcrap's own, ...). A named mutex
guards against the DLL being loaded twice (once as `dinput8.dll`, once injected).

Small pieces of machine code (the guard stubs in section 5.5) are generated at runtime by a
mini "assembler" made of byte-emitting macros rather than by inline asm, because most of them
need to jump back into the middle of a game function and need absolute addresses of the DLL's
own globals; emitting bytes into a `VirtualAlloc`'d RWX page is simpler than fighting the
compiler for that. Inline asm is used only for the few `naked` trampolines that need the FPU
stack (section 5.4) and for calling game functions with register arguments.

### 2.4 Test loop

The game could not be run where the code was written. The loop was: build, copy the DLL into the
game folder, the tester plays and reports, and `th12_hfr.log` is read back. Almost every bug in
section 6 was diagnosed from the log plus the tester's description, so the log is deliberately
chatty: it records the configuration, every patch site that did not match its expected bytes,
the display mode, per-5-second statistics (presents/s, ticks/s, catch-up and skipped ticks, sub
and frame node calls, long frame gaps) and, with `debug=1`, periodic dumps of the game state that
was relevant to whatever was being chased (game speed, pause object state, the pause veil sprite's
colour, the complete list of live UI sprite VMs, ...). Keeping the debug dump code in the source
and gating it with an ini switch was cheaper than adding and removing it each time.

## 3. Engine anatomy (th12, applicable to th10–th13 with different addresses)

### 3.1 Main loop and frame functions

`FUN_44f560` is the message loop. Each iteration pumps messages, calls
`IDirect3DDevice9::TestCooperativeLevel`, then calls one of three per-frame functions depending on
the "input latency" option: `FUN_450600` (vsync-driven, "fast"), `FUN_4503f0` and `FUN_450080`
(the two software-limited modes). The three calls are at `0x44f89e`, `0x44f8aa` and `0x44f881`
and are the hook point of the patch (`hfr_frame`). Each frame function does, in order: run the
update runner (`FUN_4624c0`), and if it returned 0/-1 tear down the scene (`FUN_464c40`) and
return 1/2 to the main loop; run the draw runner (`FUN_462620`) between `BeginScene`/`EndScene`;
present (`FUN_450720`, which also contains a `Sleep` used by the game's own latency setting,
disabled by the patch); measure the frame time into the double at `0x4cf2a0`.

### 3.2 Update and draw runners ("UpdateFunc" / "DrawFunc")

Every game system registers callbacks with a runner object (`DAT_004ce89c`). Update callbacks
form a linked list at `runner+0x18`, draw callbacks at `runner+0x3c`; both are sorted by
priority. The node structure is

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

Registration is `FUN_462380` (update, priority in EBX, node in ESI) and `FUN_462420` (draw);
removal `FUN_462890`. The runner (`FUN_4624c0`) walks the list, skips disabled nodes, and
interprets the callback's return value: 0 remove the node, 1 continue, 2 call the same node again,
3 stop walking the list ("cut"), 4 or 8 make the runner return 0 (scene ends), 5 make it return
-1, 6 restart from the head, 7 call the node's cleanup and continue. A critical section
(`0x4cf0f8`, with a depth byte at `0x4cf218`) wraps the walk when `DAT_004cee78 & 0x8000`.

The update priorities found in th12 (the ones that matter here):

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
| 0x1a | 0x424190 | player bomb effects |
| (n/a) | 0x460c30 | AnmManager UI list |

`tools/scan_reg2.py` produced this table: it disassembles backwards from every `call 0x462380`
to find the `mov ebx, prio` and `mov [reg+8], func` that precede it.

**The draw list, and what the dimming needs of it** (DEVNOTES_RUNTIME §3b). The draw runner
is `0x462634` (dispatch `0x462691`: `mov ecx,[esi+0x20]; mov eax,[esi+8]; call eax`), the
sprite batch flush `0x45a3c0` (ESI = AnmManager, pointer at `0x4ce8cc`, pending-quad count at
manager+0x4b56a0), the sprite VM draw `0x45c900` (VM in EAX, 0x4b4 bytes; loaded-ANM pointer
at +0x3f8, sprite layer at +0x20, script index at +0x3ea). A loaded ANM begins with its slot
index and its file name. Draw priorities from a `debug=1` trace: 1 `0x42f0b0` binds the
offscreen stage target, 2 and 5 the Stage's 3D passes, 4..11 sprite layers 0..4, **12
`0x42f200` binds the world target (`world_prio`)**, 14 `0x42f3b0` copies the stage into it,
then 13/16..20 layers 5..9, 21 EnemyManager, 22 UfoManager, 23 layer 10, 24 Player, 25/26
layers 11/12, **27 ItemManager**, 29 LaserManager, 31 BulletManager, 33 `0x40fbd0`, 35 Bomb,
37/39 stage effects copies, 41..45 layers and Gui, 48 `0x42f320` back to the back buffer,
49 `0x42f470` the final copy, 50 on the interface. bullet.anm carries the items (layer 10)
and the effects; the bullets are the BulletManager's own draws (31) with their VMs' layer
left at 0 — layer 16 (priority 34), once excluded as "the bullets", is the enemy death
bursts (the coloured disc and its rotating ring: scripts 76-152, the block that sets
`ins_68(16)`) and the bullet-cancel sparks. enemy.anm: layer 7 spawn flashes and auras
*and the UFOs* (scripts 135-138, the one place a script-range rule is needed), 8/9 the
enemies. The player's shots are pl0X.anm on layers 11 and 13, its body sets none. VM
script index at +0x3ea (16-bit), slot at +0x3e6, sprite id at +0x3e4.

### 3.3 Timers and the global game speed

The single most useful discovery: the engine has a global float **game speed** at `0x4b2ed0`
that ZUN uses for the boss-death slow motion (ECL instruction 447 sets it to 0.5 or 0.25). Every
`Timer` structure

```
struct Timer { int prev; int cur; float cur_f; float* speed; uint32_t flags; };   // 0x14 bytes
```

advances through `FUN_464a80` ("tick", ESI = timer): `if 0.99 < speed < 1.01 { cur++; cur_f += 1 }
else { cur_f += speed; cur = ftol(cur_f) }`. `FUN_464a20` adds a value times speed,
`FUN_4067e0` sets. Most integrators (`pos += vel * speed`) already multiply by it, because the
slow-motion feature required them to. So a large part of the engine can be made to advance by a
fraction of a frame just by setting this float to the fraction — which is the whole basis of the
patch. The places that do *not* consult it are enumerated in section 5.5.

Note the `0.99..1.01` fast path: with speed exactly 1.0 timers count in integers, with any other
speed they accumulate in float. That is why the sub-steps are chosen to be exactly representable
(section 5.2).

### 3.4 Input

`FUN_462ec0` polls the keyboard (`GetKeyboardState`, or the DirectInput keyboard at
`DAT_004ce908` when `DAT_004cee78 & 0x400`) and the joystick (`FUN_462a80`: `joyGetPosEx` or the
DirectInput device at `DAT_004ce90c`) and returns the button word in EAX. The bit layout (from the
player code): 0x01 shot, 0x02 bomb, 0x08 focus, 0x10/0x20/0x40/0x80 up/down/left/right,
0x200 skip; 0x400 is a derived "shot+focus pressed together" bit. It is called once per frame
from the Supervisor node and stored in a 0x130-byte input block at `0x4d48b8`:

```
0x4d48b8 cur   0x4d48bc prev   0x4d48c0 repeat   0x4d48c4 pressed   0x4d48c8 released
0x4d48cc .. 0x4d4948   32 per-key hold counters (raw)          0x4d49e4 raw "held > 7 frames" mask
0x4d494c .. 0x4d49c8   32 per-key hold counters (game input)   0x4d49cc auto-focus counter
0x4d49d0 game cur   0x4d49d4 prev   0x4d49d8 repeat   0x4d49dc pressed   0x4d49e0 released   0x4d49e8 held mask
```

`FUN_465440` derives prev/pressed/released/repeat for the raw block, `FUN_40f690` for the game
block. The replay record node copies raw `cur` into game `cur` every frame (optionally
synthesizing the focus bit when the "hold shot to focus" option `DAT_004ceae8 & 0x200` is on)
and appends 6 bytes per frame to the stage record; the playback node writes game `cur`/pressed
from the recording instead. The player reads the game block only. Menus read the raw block.

### 3.5 Replay manager

`DAT_004b4518` points to the ReplayManager. `+0x10` is the mode (0 recording, 1 playing, 2 used
for info-only loads by the replay list). `+0x1d0` is the frame counter within the current stage
(-1 before the stage starts, set to 0 by the stage-start routine `FUN_43c590`, incremented by the
record/playback node), `+0x1d8` the stage index; `+0x20 + stage*4` are the per-stage record
pointers (non-null for stages that are part of the replay). Saving is `FUN_43bc10` (fastcall,
filename in ECX), called from four sites; playback loading is `FUN_43c350` (called at `0x43b1d2`).
The file format is the usual th10+ one: a 0x24-byte header (`t12r`, user-data offset at +0xc,
compressed size at +0x1c, decompressed size at +0x20), the compressed body, then `USER` chunks
(`"USER"`, u32 size, u8 type, 3 pad bytes, payload). The game only reads its own two chunk types
from the user-data offset, so additional chunks with other type bytes can be appended freely —
the patch appends type 0x48 (recording rate, text) and 0x49 (per-tick inputs, binary).

### 3.6 Objects the patch touches

*Player* (`DAT_004b4514`): the patch does not read the position directly; it needs the state timer at `+0xa30..+0xa38` (a `Timer`), the focus flag at `+0xc598`, the
gather counter `+0xc418`, and the movement code in `FUN_4367xx` which integrates movement in
16.16 fixed point with `ftol(vel * speed)` (section 5.6). Player shots are a separate array
walked from `FUN_436f80`/`FUN_439b10` and use the shared `MotionState::step` (`FUN_464db0`).

*Enemies* (`DAT_004b43dc` EnemyManager, list of `{enemy, next, prev}` nodes at `+0x68`): an enemy
has its ANM VM ids at `+0x1120` (14 slots), sprite offsets at `+0x1168`, parent slot indices at
`+0x1220`, position at `+0x1074`, hp at `+0x2648`, flags at `+0x26f8` (0x01000000 = being
deleted, 0x02000000 = "sprite positions are absolute" mode). The ECL VM core is `FUN_467170`
with the th12-specific instruction switch in `FUN_4154d0`.

*Bullets* (BulletManager, update `0x40a1f0`): 0x9f8-byte entries, position at `+0x4bc`, timer at
`+0x4e4`, the sprite VM embedded at `+0x8`; per-frame counters at `+0x4` and `+0x520`.

*Items*: timer at `+0x988`, states 1–5 in `FUN_425xxx`, the "+0.2 per frame" gravity constant at
`0x4a3fb8`, the UFO attraction acceleration at `+0x9bc`, a state-5 countdown at `+0x9c0`.

*Lasers*: three classes with vtables `0x4a0654` (line), `0x4a06ac` (curve), `0x4a0704`
(beam), update at vtable slot 2, per-object timers at `+0x14/+0x18` (and `+0x28/+0x2c` graze
timer for line/curve), an "ex wait" counter at `+0x44c`.

*ANM VMs* (`AnmManager` `DAT_004ce8cc`, `FUN_461920` looks a VM up by id, EDX = manager, id on
the stack): timer at `+0x68`, colour at `+0x3bc`, sprite index `+0x3ea`, instruction pointer
`+0x3f0`, pending interrupt `+0x3c4`, position `+0x430`, flags `+0x47c`. `FUN_455630` is
`AnmVm::update`; the world and UI lists live at `AnmManager+0x8856b8` and `+0x8856c0`. The pause
"veil" is a full-screen tint quad (`DAT_004ceaac`) drawn at priority 0x31 whose colour is copied
from a child sprite by the pause menu's draw function (`FUN_432260`) and reset to `0xffffffff`
after each draw.

*Direct3D*: the device pointer is `DAT_004ce8f0`, the present parameters `DAT_004ce9dc`,
window flags `DAT_004cf428`; `Direct3DCreate9` is imported through the IAT (and called directly
at `0x44f6fc`).

## 4. Deciding what to sub-step

A first pass classified every update node as either **SUB** (safe to call with a fractional game
speed) or **FRAME** (must run exactly once per 60 Hz frame with speed 1.0). The criteria:

A system is a candidate for SUB if its state is continuous (positions, velocities, timers that
already multiply by the speed float) and its discrete events are triggered by timers rather than
by counting calls. Bullets, lasers, items, the player, the 3D stage and the ANM interpreter are
all like this, because the slow-motion feature already required them to be.

A system stays FRAME if it counts calls (a `counter++` or `x % 60 == 0` per invocation), if it
consumes the RNG per call (calling it more often changes the random sequence and therefore the
stage), if it drives discrete game flow (menus, pause, stage transitions, spell card timing) or if
it is the ECL interpreter. The ECL scripts are frame-counted programs: `wait N` means N calls,
enemies move by per-call increments computed from script constants, and there is a lot of
`frame % n` logic; making them run at a fraction of a frame per call would require patching the
interpreter's instruction semantics one by one (possible, see section 9, but not attempted).

The classification is a table in the source (`g_classes[]`), with unknown nodes defaulting to
FRAME, and each entry can be flipped from the ini (`[systems]`) for troubleshooting. That switch
turned out to be very useful: several bugs were localised by the tester turning systems off one
at a time.

## 5. Patch design

### 5.1 The tick model

The engine's frame remains the unit of game logic. The patch runs the update pass at the display
rate (R ticks/s) and gives each tick a length `dt` in frames such that 60 frames elapse every R
ticks. A tick on which the integer frame counter increments is a **frame boundary** ("major")
tick; on that tick FRAME nodes are called with speed 1.0 and behave exactly as in the original.
On the other ("minor") ticks only SUB nodes are called, with speed `dt`.

`hfr_runner` is a complete reimplementation of `FUN_4624c0` (jumped to from its first bytes) that
replicates the return-code protocol above and adds the classification, the speed factor, and a
few pieces of memory described below. Draw callbacks run every tick unchanged, and every tick is
presented.

### 5.2 Dyadic Bresenham sub-steps

`dt = 60/R` is not representable in float for most R (360 gives 0.1666…). If every tick added
that rounded value to `Timer::cur_f`, the sum after six ticks would not be exactly 1.0, the
integer part would sometimes flip one tick early or late, and events keyed on timers (shot
cadence, item states, ANM `wait`s) would drift and jitter. Instead a frame is divided into 256
units and tick lengths are integers of units chosen by a Bresenham sequence: at 360 Hz the steps
are 42, 43, 43, 42, 43, 43 (sum 256 = one frame). All partial sums of multiples of 1/256 are exact
in float, so every timer crosses every integer on the tick it should, with zero drift. The same
mechanism handles rates that do not divide 60 evenly (144 Hz: 106, 107, 107, … units; 2.4 ticks
per frame on average) and the "logic rate ≠ present rate" case used for replays.

The sequence is restarted at the first frame of every stage (section 5.9) so that the sub-step
pattern of a given frame depends only on the frame number.

### 5.3 Frame pacing

Ticks are scheduled against the wall clock rather than counted per present. `hfr_frame` keeps an
origin `g_t0` and a count of ticks run, computes how many ticks *should* have run by now, and
runs one extra update-only tick (no draw) when the deficit exceeds 6, or holds one back (present
without updating) when it is below -6. The hysteresis of 6 is there because the DWM present queue
delivers frames in bursts; treating every burst as missed frames made the game run up to 40% too
fast (section 6). A deficit beyond ±60 (a stall, alt-tab, clock jump) re-anchors instead of
catching up. With vsync the presents pace the loop; when vsync does not appear to pace it (rate
measured 8% above the display rate for a second) a software limiter takes over. The game's own
frame limiter and its "input latency" sleep are disabled. `timeBeginPeriod(1)` is requested.

The ratio between logic ticks and presents is a second Bresenham (`ticks_for_slot`), so the
logic rate can be lower than the display rate (a 144 Hz replay played on a 360 Hz display runs
144 logic ticks per second and repeats frames).

### 5.4 The game speed float and who writes it

The effective speed the engine sees is `logical × factor`, where `logical` is the game's own
notion (1.0, or the ECL slow-motion value) and `factor` is 1.0 for FRAME nodes and `dt` for SUB
nodes, set by the runner before every callback. The game writes the float at 21 sites, all of the
form `fstp dword [0x4b2ed0]`. They fall into five groups and each group is redirected to a small
naked trampoline (`fstp [tmp]; pushad; call C-handler; popad; ret`) that updates `logical` and
rewrites the effective value:

| group | sites | handling |
|---|---|---|
| permanent 1.0 (stage/boss reset) | 0x421d5f 0x4222a0 0x42f56d 0x436ed7 0x4653fc | `logical = 1` |
| temporary 1.0 (Stage / AnmVm "ignore slow-mo", restored later from a saved effective value) | 0x4030fc 0x455670 | write `factor` only |
| pause: save and set 1.0 | 0x432835 0x43293c 0x433853 0x4339a2 | shadow ← logical; logical = 1 |
| pause: restore | 0x432988 0x433a1d 0x4348ed | logical ← shadow |
| ECL ins_447 | 0x4193e4 | logical = value |
| save/restore of the effective value, literal 0.0 for items | 0x403123 0x42840c 0x42841b 0x455b3e 0x4586f4 0x45871f | untouched |

Getting this right was necessary because the pause code saves the current value and restores it
later: if it saved `dt` it would later restore a speed of 0.17 and the game would crawl.

### 5.5 Per-frame assumptions inside SUB systems ("site patches")

Even in systems that honour the speed float, some code assumes one call per frame. Each such
place was found by reading the decompilation of the system's update function and grepping for
increments, modulo operations, RNG calls and constants added without a speed multiply, then
patched with a tiny stub. Two kinds of stub cover almost everything:

*Scale*: an increment that should be proportional to time gets `fmul dword [g_factor]`
inserted. Examples: player shot `pos += vel` (0x437016), shot `speed += accel` (0x436fe2,
0x439b72), the shared `MotionState::step` (0x464dbc, used by player shots and damage sources),
items' `+= 0.2` gravity (0x425fc5, 0x426080, 0x426218) and attraction acceleration (0x426926).
`g_factor` rather than the effective speed is used deliberately: these sites did not scale with
the slow-motion factor in the original either.

*Gate*: a per-call event is made to happen only on the tick where the object's own timer crossed
an integer (`prev != cur` on the object's `Timer`, which by construction happens exactly once per
frame), or only on a frame-boundary tick when the object has no timer. Examples: player death
particles (0x436dd9), the `state_timer % 60` counter (0x4374fc), the option gather counter
(0x4368f7), bullet per-frame counters (0x409fdb), item state-5 countdown (0x425c5c), laser "ex
wait" counters (0x42979a, 0x42c90c, 0x42adef), laser graze-every-3-frames (0x429a55, 0x42cbf7,
0x42b068), the stage distortion effect that consumes RNG every frame (0x403145, 0x4036dd), the
enemy death-ring effect callback (0x410814) and the scrolling-mesh effect callback (0x45dcd0).

Gating on the object's timer rather than on the global frame boundary matters: an object created
mid-frame has a timer phase of its own, and gating on the global boundary would make its
per-frame events happen at a slightly different moment than in the original.

Two `Timer::add` call sites take a *script constant in frames* as the argument (the shot cycle
`timer -= 14` at 0x439ac2 and the ANM `wait` helper at 0x43adbd); those are redirected to a
version that multiplies by `logical` instead of the effective speed, i.e. stock behaviour.

Every stub is installed only if the bytes at the site match the expected original; a mismatch is
logged and skipped, which is also how th12e.exe was confirmed to be byte-identical at all sites.

**The curved laser, found by a player (v0.4.12).** `LaserCurve::update` (`0x42c770`, ESI = the
laser) keeps its trail as a ring of nodes (five floats each — x, y, z, angle, width — at
`+0xf9c`, count at `+0x470`) and, every call, shifts the ring by one node (`0x42c925`) and then
adds a whole frame's velocity (`+0x5c..+0x64`, recomputed by the "ex" behaviours from angle
and speed) to the head node with no speed multiply (`0x42c965`). It is the one motion in the
game that ignores the speed float — the line and beam lasers, the bullets and the items all
multiply — so under sub-stepping a curved laser advanced N times per frame and its trail
streamed out N times as long: Nazrin's and Shou's lasers visibly sped up with the tick rate.
The 2010 first pass read the laser classes' timers and missed the integration because the
head's `+=` is written against the node array, not the object. Fix: shift the ring only on
the tick where the laser's own graze timer (`+0x28/+0x2c`, ticked at the end of the same
update) crossed a whole frame, and add `velocity × factor` every tick. The nodes are the
head's position at each whole frame, so between shifts the head glides from the last node
towards the next frame's position, which is what the trail geometry already assumed. TH13
computes each node from a float timer through the laser's motion segments and needed nothing;
TH10 and TH11 have no curved laser class (`LaserCurveInf` appears in the TH12 and TH13
binaries only).

**Player shot behaviours (v0.4.12).** The shot-type table at `0x4aebd8` holds five per-shot
callbacks called with EDX = the shot: homing `0x43a480` (turn towards the target, speed
±0.2 per call), `0x43a810` (stop at the enemy's height, timer-state checks), gravity
`0x43aa50` (speed −0.1 per call), `0x43ab60` (speed −0.38/−0.6 and angle += angular velocity
per call) and the option laser `0x43a6b0`. TH11 gated its two of these on the shot's integer
timer (`+0/+4`) from the start; TH12 had not, so ReimuA's homing and the other behaviours
turned and accelerated per sub-tick. The four are now gated the same way, and the option
laser's growth (`+28` per call towards 448, `0x43a750`) is scaled by the factor. TH13's table
(`0x4bb4d8`: homing `0x446cb0`, `speed += 1` `0x447590`, `speed *= 0.8` `0x447510`) gets the
same gate on its shot timer `+0x18/+0x1c` (TH13_DEVNOTES). What is still per call in both:
the option laser's angle smoothing (`angle += (target − angle) × 0.1`), which turns N times
faster — small, and left for now.

### 5.6 The player's fixed-point movement

The player integrates movement as `pos += ftol(vel * speed)` in 16.16 fixed point (0x4367ca,
0x4367dd). Truncating per sub-step loses up to one unit per tick; at 360 Hz that made focused
movement 1.6% slow. The `ftol` calls are replaced by a stub that carries the truncation residual
over to the next step, so the sum over a frame matches the original to within one unit.

### 5.7 Enemy hit test guard

`FUN_439ed0` (player shot vs enemy) begins with "if the player's state timer did not change this
frame, no damage" — a stock guard against double hits. With sub-steps the integer timer only
changes on one tick in six, so enemies became nearly immune. The guard (0x439ef2) is replaced by
a comparison of the player's float timer before/after the most recent Player update, which the
runner records around the Player node.

### 5.8 Frame-locked systems inside a sub-stepped frame

Two consequences of running FRAME nodes only on boundary ticks needed handling in the runner.

The pause menu works by GameManager returning 3, which cuts the update list before Player, bullets
and so on. On minor ticks GameManager is not called, so the cut did not happen and everything kept
moving while "paused". The runner remembers which node cut the list on the last boundary tick and
cuts at the same node on the following minor ticks. In addition, a pause that is raised
mid-frame by a sub-stepped node (the game-over pause raised by the Player when the last life is
lost) is honoured immediately: on minor ticks the runner checks the GameManager's pause flags
(`GameManager+0x60 & 0x70`) when it reaches that node. Without this the Player node kept running
for the rest of the frame after triggering game over, and each extra call created another copy of
the pause background tiles — only the last one received the "close" interrupt, so a translucent
white veil stayed on screen forever (section 6).

Enemies (ECL) are FRAME nodes, so their positions change once per frame; their sprites would
stutter at 60 Hz. `enemy_interp` runs after every update pass: for every live enemy it keeps
the last two frame positions in a hash table keyed by enemy pointer (entries expire when an enemy
is not seen for two frames) and writes each of its 14 sprite VMs' positions as
`last - (last - prev) * (1 - alpha)` plus the sprite offset (and the parent VM's position for
attached sprites, plus the 224,16 playfield offset in the normal mode), with `alpha = phase + dt`.
Jumps larger than 48 px are treated as teleports and not interpolated. Since the sprites are
placed exactly at the frame position on the last tick of the frame, the interpolation adds no
persistent offset; it displays each enemy where a sub-stepped object would be.

### 5.9 Replays

A recording made with sub-stepping is not bit-identical to 60 Hz, so a replay must be played with
the same tick sequence it was recorded with. The save hook appends a `USER` chunk with the logic
rate; the load hook reads it and `replay_check` switches the logic rate to the recorded one
(or to stock 60 when the chunk is absent) for the duration of playback.

That is not sufficient at rates like 144 Hz where the sub-step pattern of a frame depends on the
phase of the Bresenham sequence: the phase at stage start would depend on how long the game had
been running. The runner therefore restarts the sequence (`schedule_reset_here`) on the frame
boundary tick on which the replay record or playback node is about to run frame 0 of a stage
(`ReplayManager+0x1d0 == 0`). From then on the pattern of every frame is a function of the frame
number only, in both recording and playback.

### 5.10 Sub-tick input

On every minor tick of an active frame (one where the replay node ran), the runner calls the
game's own poll routine `FUN_462ec0` with the raw input block saved and restored around the call
(so the per-frame edge detection for menus is not disturbed), and merges only the movement bits
(0xf0) and the focus bit (0x08) into the game input word `0x4d49d0`. The focus bit respects the
"hold shot to focus" option by checking the auto-focus counter the replay node maintains. The
Player, being a SUB node, sees the new bits on the next tick. `joyGetPosEx` is IAT-hooked to
return the last frame's result between frames, since some joystick drivers make it slow.

For replays, the bits the player saw on every tick since the stage's first frame are appended to
a per-stage buffer (one byte per tick, 5 significant bits) and written at save time as a second
`USER` chunk (type 0x49), run-length encoded — typically a few kilobytes for a full run. During
playback the same stream is applied on minor ticks instead of polling. Ticks are only counted
while the frame is "active", so pauses (during which the replay node does not run) do not shift
the stream. Frame-boundary ticks keep the game's own recorded input, so a replay with the chunk
plays back the sub-tick movement exactly, and one without it plays with the frame input held for
the whole frame.

### 5.11 Presentation: Direct3D 9Ex

`Direct3DCreate9` is IAT-hooked; with `d3d9ex=1` the hook calls `Direct3DCreate9Ex` from the
same module the game's import resolved to (so a wrapper `d3d9.dll` in the game folder, such as the
rotation wrapper the tester has, stays in the chain), and `CreateDevice`/`Reset` on the returned
objects are redirected to `CreateDeviceEx`/`ResetEx`. After creation `SetMaximumFrameLatency(1)`
limits the driver's present queue to one frame, which removes the up-to-two-frames of latency the
default queue depth adds; the present interval stays `ONE` (vsync). Direct3D 9Ex has no managed
pool, so `CreateTexture`/`CreateVertexBuffer`/`CreateIndexBuffer` (device vtable slots 23, 26,
27) and the two D3DX loaders the game imports from `d3dx9_40.dll` (`D3DXCreateTexture`,
`D3DXCreateTextureFromFileInMemoryEx`) are hooked to turn `D3DPOOL_MANAGED` into
`D3DPOOL_DEFAULT | D3DUSAGE_DYNAMIC`, the same conversion OpenInputLagPatch uses. The window
style is refreshed with `SetWindowPos(SWP_SHOWWINDOW)` after device creation because 9Ex resets
it. If `Direct3DCreate9Ex` is unavailable or the created device does not answer a
`QueryInterface` for `IDirect3DDevice9Ex`, everything falls back to plain Direct3D 9.

The refresh rate is read from `GetDisplayMode` after every device creation/reset (falling back to
`EnumDisplaySettings`), and drives the logic rate unless `fps` is set in the ini. In exclusive
fullscreen the requested refresh rate is written into the present parameters.

## 6. Bugs met on the way, and what they taught

*Invisible to the user, invisible to us: thcrap silently un-hooked the whole patch.* A report
of "nothing happens with the English patch installed" turned out to be an import-table race
that no log line and no test could have shown, because nothing failed. thcrap injects by
letting the loader finish and stopping the game's thread at the executable's entry point, so
this DLL's `DllMain` — and therefore its whole install — has already run by the time thcrap's
code starts. thcrap then walks the game's import table, matches by *name*, overwrites whatever
it finds, and chains to `GetProcAddress(dll, func)` rather than to the pointer it replaced
(`iat_detour_func` in `thcrap/src/mempatch.cpp`; the comment there says the point is to
"override any existing patches"). Every import this patch hooks that thcrap also detours was
therefore dropped, `d3d9.dll!Direct3DCreate9` among them — which is how this patch obtains the
device, so it did nothing at all while reporting a successful install.

The first fix was wrong, and the way it was wrong is the more useful half of the story. It
redirected the export table entry of the module each hooked function comes from, so that
anyone chaining through `GetProcAddress` landed back here without this patch having to do
anything. It worked, and it crashed Steam copies of TH10 on the first frame — access violation
inside `ntdll`, nothing in our own log, because an import slot belongs to the game but an
export table belongs to the whole process. Handing a foreign address to every module that
resolves `d3d9.dll!Direct3DCreate9` includes handing it to Steam's overlay, which has every
right to expect an address inside `d3d9.dll` and to detour it. The non-Steam copy, with no
overlay in the process, was fine throughout — which is exactly the shape of evidence that says
"you changed something global".

What replaced it is smaller. Hooking an import records what the slot held and calls *that*,
whoever put it there, so a patch arriving later is nested inside rather than discarded — which
is all thcrap had to do and did not. Then the imports are taken back once, from
`kernel32.dll!QueryPerformanceCounter`: these games call it before they ask for Direct3D
(measured, and the reason it is not hung on the Direct3D import — that is the one thcrap
takes), no translation patch has any reason to touch it because it has no ANSI/Unicode pair
and says nothing to the player, and the import table is never encrypted, so it can be armed
while a DRM wrapper's stub is still the only thing that has run. That one trigger now does
both jobs: it is also how a Steam release gets identified at all, replacing the old
Direct3D-import trigger, which thcrap would have taken away.

Lessons. Two patches in one process is not "who wins", it is "who is still in the chain", and
a hook that can be replaced by name is not a hook, it is a request. Prefer the intervention
with the smallest blast radius that does the job: the game's own import table is ours to
rearrange, a system library's export table is not, and "it works on my machine" hid that for
exactly one build. A failure that produces no error is the expensive kind: the only reason any
of this was diagnosable is that thcrap's source could be read and the user could read a
faulting module out of the Windows event log, so the log now lists every module in the process
that did not come from Windows itself, and says when an import has been taken back. And "do
our patches collide?" is a question with a mechanical answer — `tools/check_patch_overlap.py`
compares thcrap's own game definitions against every byte this patch writes and every byte it
verifies; for TH10–13 the two are disjoint, which is *why* coexistence is possible at all.

*Startup crash (first build).* The `UpdateFunc` layout was guessed with the list node as a pointer
instead of embedded, putting `arg` at +0x18 rather than +0x20. Lesson: put a `_Static_assert` on
every struct offset that the game code implies, and cross-check with the registration function's
stores.

*Game 5% slow.* The first limiter used `Sleep(1)` and overshot vblanks. Replaced by "spin only
when vsync is effective", later by the wall-clock scheduler.

*Stage-start crash (v0.3).* `hook_site` computed the jump target from the emit pointer *after*
emitting the stub, i.e. it pointed at the stub's end. Introduced `STUB_BEGIN()`. Lesson: a
runtime assembler needs explicit label handling even when it is only 40 lines long.

*Pause menu not pausing bullets and animations.* The list cut by GameManager (return 3) is a
per-frame decision; see 5.8 for the stop-node memory.

*Player shots too fast, wrong cadence, ghost collisions.* Three separate causes stacked up: the
shot motion lives in the shared `MotionState::step`, which does not multiply by speed; the shot
cycle timer subtracts a script constant through `Timer::add`, which does multiply by speed (so it
was scaled twice); and the node initially labelled "PlayerShots" was in fact the Bomb node, so
bombs ran six times too fast. Lesson: label nodes by *reading* the function, not by guessing from
neighbouring priorities, and check every `Timer::add` argument to see whether it is a rate or a
constant.

*Game 40% too fast (v0.7b).* The per-present catch-up logic saw the DWM deliver two or three
presents in quick succession and interpreted it as missed frames. Fixed by scheduling against the
wall clock with hysteresis. Lesson: never infer elapsed time from present callbacks under a
compositor.

*Enemies immune to shots (v0.8).* The stock double-hit guard in the hit test compares the player's
integer state timer with its previous value; see 5.7.

*Lingering white veil after game over.* Took four iterations. The debug dump of the UI VM list
showed 5–6 copies of the pause background tile VM after each game over, which pointed at the
Player node running several more sub-ticks after raising the pause. Lesson: when a visual artefact
persists, dump the object lists — counts and creation patterns are far more informative than
colours and flags.

*Timer drift at non-power-of-two ratios.* Discovered by analysis before it was reported: the
dyadic Bresenham steps replaced an interim "add 1e-5 bias" idea that would have masked, not
fixed, the problem.

*Focused movement 1.6% slow at 360 Hz.* The fixed-point truncation per sub-step; see 5.6.
Lesson: any `ftol`/integer conversion inside an integrator is a per-call rounding that has to be
carried.

*Register clobbers in inline asm.* `anm_get_vm` passes the manager in EDX and `call_with_esi`
passes ESI; both need `"+d"`/`"+S"` in/out operands or GCC assumes the register survives.

*Mesh/ring effect callbacks and stage distortion running per tick.* Visual effects driven by ANM
callbacks that consumed RNG or scrolled UVs per call. Per-frame gates on their own timers.

## 7. Verification techniques that worked

The float-exactness of the sub-step scheme was checked offline with a small Python model of the
Timer update (accumulate `cur_f` in float32 and compare the integer crossings against the ideal
schedule for every rate from 60 to 1000). The stats line in the log ("ticks/s") confirmed the
game speed to four digits over minutes of play. Stage and spell-card timers reaching the same
values as in the original at the same wall-clock times was the tester's check for speed. The
debug VM dump (counts of live sprite VMs per list, their sprite ids and colours) was the tool for
the game-over veil. For replays, "record, play back, watch for desync" is the only real test;
the log's "per-tick input available" line at each stage start confirms the chunk was found.

## 8. Known limitations and future work, in order of value

1. **Per-tick enemy hit tests.** Player shots hit enemies once per frame (the hit test runs
   inside the Player node against enemy positions that change once per frame). Running the test
   on every tick against the *interpolated* enemy position would give sub-frame hit timing; the
   damage bookkeeping in `FUN_439ed0` would need the same guard treatment as in 5.7.
2. **Bullet-vs-player at interpolated enemy-relative positions.** Already per tick for bullets,
   which are sub-stepped; nothing to do unless enemies are sub-stepped.
3. **Full ECL sub-stepping.** Would make enemies and bosses truly high-rate. It needs the ECL
   interpreter's `wait`/time semantics (`FUN_467170`: float time += speed, jumps set integer time)
   to run with fractional speed, all per-call increments in the enemy update (`FUN_413xxx`,
   `FUN_4154d0` instruction handlers: movement modes, `frame % n` logic, RNG-consuming
   instructions) to be gated or scaled, and the spell-card/dialogue/boss-HP code to stay
   frame-locked. It is a large audit (the enemy update is ~2000 lines decompiled) but mechanically
   the same work as sections 5.4–5.7.
4. **Menus/HUD at high rate.** Purely cosmetic; the Gui node could be sub-stepped for its
   scrolling elements if the score/count-up code is gated.
5. **Replay robustness.** Playback at a different rate than recorded is not supported (the
   simulation differs slightly). A "resimulate at 60" fallback is not possible without the
   original 60 Hz inputs, which are only available at frame granularity — those are what the game
   records, so a patched replay still plays in an unpatched game, approximately.

## 9. Porting to other Touhou games

The design is not th12-specific; the addresses are. th10 (MoF) through th13 (TD), and with more
changes th14–th18, use the same engine generation: the same runner protocol, the same `Timer`,
the same global game speed (introduced for the slow-motion effect), the same input block layout,
the same replay format with `USER` chunks, the same ANM VM. th06–th09 are a different engine and
this document does not apply to them beyond the general approach.

A porting checklist, in the order the work should be done:

**Get the decompilation.** Ghidra headless with the two scripts in `tools/` (adjust the `.text`
end address in `FixFuncs.java` only if the CRT confuses it). Export to one file and use `fn.py`.

**Find the runner.** Search for a function that reads a list head at `+0x18` of a global object
and switches on a callback's return value with cases 0..8 (the `case 6: restart` is distinctive).
Its registration counterpart pushes a priority in EBX. Run the equivalent of `scan_reg2.py` with
the new registration addresses to get the priority table, then read each callback to label it.
Confirm the `UpdateFunc` layout from the registration function's stores.

**Find the game speed float.** It is the float that the ECL "set game speed" instruction stores
(ins_447 in th12; look the number up in thtk's ECL table for the target game), and the one every `Timer` update compares with
0.99/1.01. List all `fstp` stores to it (`grep` the objdump for the address) and classify them
into the five groups of 5.4 by reading the surrounding code: literal 1.0 stores are either a
reset (permanent) or paired with a save/restore (temporary); the pause code saves before setting.

**Hook the frame.** Locate the main loop by its `PeekMessage` loop and the three frame variants;
hook all three. Disable the game's limiter and latency sleep (look for `Sleep` calls in the
present function).

**Classify systems and audit them.** Start with everything FRAME except bullets, and enable one
system at a time. For each SUB candidate read its update function top to bottom looking for:
increments without a speed multiply, `%` on frame counters, RNG calls, `ftol` in integrators,
`Timer::add` with constant arguments, and callbacks it invokes per call. Use the ini switches to
bisect when the tester reports something odd.

**Player specifics.** The fixed-point movement and the shot code are the most game-version-
dependent parts; the hit-test guard (5.7) exists in the same form in th10–th13.

**Replays.** The save/load functions and the `USER` chunk convention are the same; only the
magic (`t10r`, `t11r`, ...) and the addresses change. Find the ReplayManager's frame counter by
looking for the node that copies the raw input word into the game input word.

**Direct3D.** Identical; only the D3DX DLL version in the IAT changes (`d3dx9_NN.dll`). Later
games (th14+) may use different texture creation paths — check the imports.

**What can be reused verbatim.** The tick scheduler, the dyadic Bresenham, the runner
reimplementation (given the same protocol), the stub assembler, the enemy interpolation (given
the enemy struct offsets), the sub-tick input mechanism (given the input block layout), the
replay chunk reader/writer, and the whole Direct3D 9Ex layer.

## 10. Address appendix (th12 v1.00b, th12.exe = th12e.exe)

| address | what |
|---|---|
| 0x44f560 | main loop; frame calls at 0x44f881 / 0x44f89e / 0x44f8aa |
| 0x450600 / 0x4503f0 / 0x450080 | frame functions (vsync / limiter variants) |
| 0x450720 | present (+ latency sleep `cmp byte [0x4cead3],1` at 0x450729) |
| 0x4624c0 / 0x462620 | update runner / draw runner |
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
| 0x433444 0x434459 0x43519b 0x448e4f | replay save call sites; 0x43b1d2 playback load call site |
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
| 0x4cee34 / 0x4cee38 | per-frame context pointer / flag set by the frame function before the runner |
| 0x464c40 / 0x4cf0d8 | scene teardown (ESI = 0x4cf0d8) |
