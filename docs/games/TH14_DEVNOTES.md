# Touhou 14 — Double Dealing Character

Reference for the TH14 profile in the shared x86 runtime. Implementation:
`src/games/th14.c`, `src/games/th14_signatures.h`, `src/games/th14_conflicts.h`; stub tests in
`tools/test_th14_stubs.py`. Shared mechanisms are described in
[DEVNOTES_RUNTIME.md](../DEVNOTES_RUNTIME.md); the TH13 equivalents of most sites are in
[TH13_DEVNOTES.md](TH13_DEVNOTES.md).

## 1. Identity and tooling

| | |
|---|---|
| executable | Japanese `th14.exe`, v1.00b (`th14e.exe` is accepted by name but unverified, §15) |
| ImageBase / SizeOfImage | `0x400000` / `0x101000` |
| entry / TimeDateStamp | `0x487453` / `0x520dc559` |
| platform | x86, Direct3D 9, `d3dx9_43.dll` — the TH10–13 family, so a profile and not a backend |
| replay magic | `t13r` — ZUN reused TH13's. Three occurrences in the executable, one the loader's check at `0x455cb7`. `t14r` is wrong. A wrong magic only costs `replay_read_chunk` the header shortcut: it scans for `USER` from the start of the file |
| replay directory | `%APPDATA%\ShanghaiAlice\th14\` (`addr.data_dir`, §10) |
| conflict sites | none recorded: `vpatch_th14.dll` has not been read; the module-name check in `conflict.c` covers it |

Signatures in `th14_signatures.h` are frozen in three groups: every site the patch writes, sites
that are read and never written (the runner epilogue `0x401382`, the frame function's runner call
`0x46a99e`, the frame context stores `0x46a960` / `0x46a994`, the cleanup call `0x46a9a7`, the
flush call `0x46a955`, the two peek call sites of §10), and identification only (`0x4013b0`,
`0x401630`, `0x46a950`, `0x445000`, `0x46ab70`, `0x46a720`, `0x46a360`).

Debug switches, all under `[hfr]` in the ini: `debug=1` (callback census, draw trace, VM trace,
site census, exception report), `replay_trace=1`, `replay_trace_from`, `replay_trace_to` (§12).

## 2. State

Described: identification, the video path, the scheduler, the catch-up tick, the draw path and
dimming, the game speed and its write sites, the class table with six sub-stepped systems
(§5), enemy and option sprite interpolation (§9), the replay extension (§10).

Not described: sub-tick input (`poll_input`, `game_input` are read but not in the profile, §10),
`pp`, the pause test (§4), `sprite_round_sites`, `vpatch_th14.dll` conflict sites, the English
and Steam builds. See §15.

A callback that is not in the class table is `MODE_FRAME` (`node_mode`'s default), so it runs
once per 60 Hz tick. `install()` logs which systems are sub-stepped:

    sub-stepping: N of 21 identified systems step with the display (<names>).
      everything else steps once per 60 Hz frame, so what it draws moves in 60 Hz steps however high the frame rate is.

With only the two sprite passes classified this read `2 of 21 ... (AnmSpritesEarly,
AnmSpritesLate)`; the current table gives six.

## 3. Engine differences from TH13

TH14 is TH13's engine rebuilt with a newer MSVC. The rebuild moved arguments into ECX or onto
the stack in six places the runtime depends on. Each is a named profile field; TH15 onwards is
expected to differ again, so check all of them when porting.

| # | what | TH14 | TH10–13 | profile field |
|---|---|---|---|---|
| 1 | update runner's object | ECX (`mov ebx,ecx` at `0x40128e`) | TH11–13 already in EBX; TH10 on the stack | `runner_arg = RUNNER_ARG_ECX`; `update_runner.c` has a thunk per form |
| 2 | the three frame functions | thiscall: `mov ecx,0x4f5a18; call ...`, plain `ret` | context pushed, callee `ret 4` | `frame_ctx_ecx` selects a `fastcall` wrapper (one pointer argument: fastcall and thiscall are the same code) |
| 3 | `remove_node` | method: runner in ECX, node pushed, `ret 4` at `0x4016e4` | TH10–12 `(node, runner)` in ECX/EDX; TH13 swapped | `remove_node_abi = REMOVE_NODE_RUNNER_THIS` (an enum; three forms exist) |
| 4 | screenshot routine | stdcall, filename pushed (`lea eax,[ebp-0x108]; push eax; call 0x445000`) | filename in EAX, stub ignores it | `screenshot_stack_arg`: the stub forwards the argument and cleans it up |
| 5 | end-of-pass cleanup | `mov ecx,0x4d98ec; call 0x403bb0` at `0x46a9a7` | TH13: `mov esi,0x4dcebc; call 0x473590` | `cleanup_this_ecx` |
| 6 | per-VM draw | VM is the first stack argument, `this` in ECX | VM in a register (`draw.vm_reg`) | `draw.vm_stack_arg` |
| 7 | `anm_get_vm` (`0x47f0a0`) | manager in ECX | manager in EDX | `anm_get_vm_ecx`; `interpolation.c` picks the register |

`hfr_frame` is `__stdcall`; wired into a TH14 frame call unchanged it reads a garbage context and
loses four bytes of stack per frame.

The per-VM draw, side by side:

```text
TH13 0x46a700:  push ebp; mov ebp,esp; and esp,-8; sub esp,0x1c; push ebx
                mov ebx,eax                 <- the VM arrives in EAX
                mov eax,[ebx+0x594]
TH14 0x478f60:  push ebp; mov ebp,esp; and esp,-8; sub esp,0x18; push esi
                mov esi,[ebp+8]             <- the VM arrives on the stack
                mov edi,ecx                 <- ... and `this` in ECX
                mov eax,[esi+0x5bc]
```

`0x478f60` scores 0.71 against TH13's `0x46a700` and is called both from the ANM manager's layer
worker and from each system's draw. The wrap borrows EAX to read `[esp+8]` and restores it; it
uses `mov` only, because the stub may not disturb flags. The VM struct grew (`+0x594` became
`+0x5bc`), so TH13's VM field offsets do not transfer (§4).

Other differences:

- **Frame flag value.** The frame function stores 2 in `frame_flag`, TH13 stores 1
  (`frame_flag_value = 2`; zero selects 1). Every store to that word in the binary pairs
  `0x4d93e8` with `2`, as TH13 pairs `0x4dc9d0` with `1`. A wrong value does not crash; it tells
  the engine it runs in a context it is not in.
- **`latency_cmp`** is `cmp byte [0x4d9159],1` inside `0x46aa70`, a routine the frame function
  calls; TH13 has it inline. The patch turns the `1` into `0x7f`, so the game's Sleep-based
  limiter never runs.
- **Speed stores are SSE or immediate**, not x87. `install_speed_sites` patched a 6-byte
  `fstp [speed]` and captured the value off the x87 stack. `SpeedSite.pop_float` became `src`
  (`enum SpeedSrc`): the capture is `fstp [g_fpu_tmp]`, `movss [g_fpu_tmp],xmmN` with N from the
  profile, or nothing. `SPEED_SRC_FPU` is 1 so the four older profiles keep their meaning.
- **The speed stub saves all eight XMM registers.** It replaces one instruction, so the next
  instruction expects every register intact. `pushad`/`pushfd` sufficed on TH10–13 because code
  around an `fstp` is x87; on TH14 the site sits in SSE code and the operation behind the stub is
  C compiled `-mfpmath=sse`.
- **Timers carry a rate pointer** (§8), which is why TH14 needs about a dozen per-frame hooks
  where TH13 needed about three hundred lines.
- **No native F10.** The window procedure at `0x469e61` handles `WM_SYSKEYDOWN` with `VK_RETURN`
  (Alt+Enter) and swallows `SC_KEYMENU`; nothing reads `VK_F10`. `native_size_cycle = 0`, so the
  patch supplies the size cycle as it does for TH10. The value 1 copied from TH13's profile made
  F10 do nothing. The same check across TH11–13 is in
  [DEVNOTES_RUNTIME.md §11](../DEVNOTES_RUNTIME.md).
- **Save and load conventions** moved too (§10).
- **First-chance exceptions.** TH14 raises four harmless first-chance access violations inside
  KERNEL32 before the title screen. The exception reporter therefore reports once per distinct
  address with twelve slots (`REPORT_SLOTS`); a budget of four total was spent before a real
  crash and logged nothing.

### thprac

thprac's TH14 support hooks `0x40138a` (update pass) and `0x40149a` (render pass) — the last
instruction of each runner, as in TH10–13. `runner_ret = 0x40138a` handles the
one-instruction conflict described in [DEVNOTES_RUNTIME.md §5e](../DEVNOTES_RUNTIME.md). Not
verified against a running game.

## 4. Address map

### Frame path

| | TH14 | TH13 |
|---|---|---|
| update runner (`runner_fn`) | `0x401280`, ends `0x40138a` | `0x470af0`, ends `0x470c04` |
| draw runner | `0x4013b0`, ends `0x40149a` | |
| `update_runner` (the global) | `0x4db51c` | `0x4dc658` |
| `remove_node` | `0x401630` | `0x470e90` |
| frame calls | `0x469a27`, `0x469a45`, `0x469a51` | `0x45c5de`, `0x45c5fb`, `0x45c607` |
| `frame_fn` | `0x46a950` | `0x45d570` |
| `latency_cmp` | `0x46aa80` | `0x45d69e` |
| `device` | `0x4d8f68` | `0x4dc6a8` |
| `window_flags` | `0x4f7a54` (fullscreen bit `0x40`) | `0x4df0e0` (bit `0x10`) |
| critical section / count / gate byte (`misc_flags`) | `0x4f56d0` / `0x4f5808` / `0x4f5815` | `0x4e48a8` / `0x4e49e0` / `0x4e49ed` |
| `raw_input` / `raw_pressed` | `0x4d6878` / `0x4d6884` | `0x4e49f0` / `0x4e49fc` |
| `replay_manager` | `0x4db688` | `0x4c22c8` |
| screenshot routine / call | `0x445000` / `0x46abf6` | `0x43a950` / `0x45d856` |
| frame context ptr / value / flag | `0x4d9640` / `0x4d93e8` / `0x4d9644` (= 2) | `0x4dcc18` / `0x4dc9d0` / `0x4dcc1c` (= 1) |
| cleanup fn / this | `0x403bb0` / `0x4d98ec` | `0x473590` / `0x4dcebc` |
| draw dispatch | `0x40141a`, node in EDI | `0x470c9e`, node in ESI |
| sprite batch flush / manager | `0x475eb0` / `[0x4f56cc]` | `0x4679a0` / `[0x4dc688]` |
| per-VM draw | `0x478f60` | `0x46a700` |

`node_arg` is `+0x24` and `runner_next` is `+0x50`, both as TH13. `input_width = 4`,
`critical_flag_mask = 0xff`, `runner_return8_ends = 1`.

The three frame calls sit in one `if/else if/else` at `0x469a03`: the automatic-latency path
(`0x46a360`), the frame function this profile describes (`0x46a950`), and the window manager's
fast update (`0x46a720`). The frame function calls the update runner at `0x46a99e`
(`mov ecx,[0x4db51c]; call 0x401280`); `runner_fn` and `update_runner` are read off that one
instruction pair.

The frame context is what the frame function sets before running the update list:
`mov [0x4d9640],0x4d93e8` at `0x46a965`, `mov [0x4d9644],2` at `0x46a994`. TH13:
`mov edi,0x4dc9d0; mov [0x4dcc18],edi` and a `1` in `0x4dcc1c`. `update_only_tick` (the catch-up
tick) reproduces it and calls the cleanup (`0x46a9a7`, also `0x46a9bf`).

The draw dispatch is `mov ecx,[edi+0x24]; mov eax,[edi+8]; call eax` (`dispatch_len = 8`). The
flush and its manager come off the frame function's first act
(`mov ecx,[0x4f56cc]; call 0x475eb0` at `0x46a955`; TH13: `mov esi,[0x4dc688]; call 0x4679a0`).
Rendering goes through two offscreen targets and reaches the game's own target only from draw
priority 52, so every world draw in the trace reports an offscreen viewport.

### Globals

| | |
|---|---|
| `addr.speed` | `0x4d8f58` (§7) |
| `addr.player` | `0x4db67c` — `mov eax,[0x4db67c]` then `[eax+0x184b4]`, a field the player update writes through EDI |
| `addr.player_callback` | `0x44ec60`, which is `jmp 0x44dbd0` |
| `addr.anm_manager` / `addr.anm_get_vm` | `0x4f56cc` / `0x47f0a0` |
| `addr.enemy_manager` | `0x4db52c` — the manager the shot-versus-enemy test uses at `0x451463` |
| bullet manager | `*(void**)0x4db530` (not a profile field; used by the trace) |
| `addr.record_callback` / `addr.playback_callback` | `0x455e40` / `0x455e60`. OpenInputLagPatch patches `0x455e82`, 0x22 bytes into the playback node, to skip replay speed control |
| `addr.replay_save` / `addr.replay_load` | `0x455490` / `0x455c20` |
| `addr.data_dir` | `0x4f5a45` |
| global object | `0x4f5a18` (the frame functions' `this`; holds the data directory at `+0x2d`) |
| game manager (supervisor) | `0x4db558`, flags at `+0x80` — the word every update callback's gate tests. Not in the profile: the runtime's pause test is written against TH10–13's bit assignments (`0x70`) and TH14's bits have not been read |
| `poll_input` (not in profile) | `0x41e710`, thiscall with the raw input object `0x4d6878` in ECX |
| `game_input` (not in profile) | `0x4d6a90` — the word the record node writes and the player's movement reads at `0x44d33e` |
| previous frame's input | `0x4d6a94`; pressed and released are derived from it |
| two further recorded words | `0x4d6a9c`, `0x4d6aa0` |

The input addresses were read from the replay record node at `0x455040`, where the game latches
its input for the frame. The replay records three words, six bytes a frame; sub-tick input has
to keep producing exactly that while sampling more often.

### Timer

`{prev, int, float, const float* rate}` at `+0x00/+0x04/+0x08/+0x0c`. Tick:
`float += rate ? *rate : 1; int = (int)float; prev = the int before`. A rate within 1% of 1.0
(0.99–1.01) takes a fast path that skips the multiply. The generic initialiser `0x408b00` stores
`&0x4d8f58` into `+0xc`. `timer_rewind` is `0x414420` (§8).

### Player (`[0x4db67c]`, update body `0x44dbd0`, EDI = player)

| offset | |
|---|---|
| `+0x5ec`, `+0x5f0` | position, fixed point, 1/128 pixel (`layout.player_pos = 0x5ec`) |
| `+0x604`, `+0x608` | velocity, already scaled by the game speed (`0x44d72f`, `0x44d746`) |
| `+0x684` | life state; dispatch table at `0x44ebf4`, five arms, state 1 = alive |
| `+0x68c` / `+0x690` / `+0x694` / `+0x698` | life-state timer prev / int / float / rate (`layout.player_timer = 0x694`); rate set to the game speed at `0x44dd2a` |
| `+0x6c8` | 256 weapon objects, walked by `0x451380`, updated by `0x4510b0`; weapon timer at `+0x18/+0x1c/+0x20/+0x24`, damage volume `weapon+0xc0` |
| `+0xd6ec` | eight options, stride `0xe4`: active `+0x00`, position (1/128 px) `+0x5c`, `+0x60`, ANM VM ids `+0xb0`, `+0xb4` |
| `+0xde28` | 256 shots, stride `0xa4`: active `+0x00`, rates `+0x14 += +0x18`, `+0x1c += +0x20` (angle, wrapped), timer prev/int/float/rate `+0x60/+0x64/+0x68/+0x6c`, hit interval `+0x80` |
| `+0xde18`, `+0xde90` | the hit test walks the shot array as `[player+0xde18]` with the cursor at `+0x74`; the player's tail walks it as `[player+0xde90]` |
| `+0x182bc` | option approach blend (30) |
| `+0x1830c` | focus counter; the option-gather laser reads it against 30 at `0x44da0d` |
| `+0x18338` | shot-cycle timer, driven by `0x450fb0`; `0x450ed0` fires one pattern step |
| `+0x184b4` | written by the player update |

Weapon type callbacks: tables of six per type at `0x4d5908`, `0x4d5928`, `0x4d5948`.

### Bullet (manager `[0x4db530]`, update `0x416700`, ESI = bullet)

Constructed in one call at `0x416510`:
`array_construct(manager+0x8c, stride 0x13f4, count 0x7d1, ctor 0x416070)`. The manager's
allocation is `0x9bf6d0` = `0x8c + 2001 * 0x13f4`, which confirms the three numbers. Slots are
fixed, so a slot index is stable across two runs for as long as they agree.

| offset | |
|---|---|
| `+0x20` | flags word; non-zero = live slot (`s` in the dump) |
| `+0x24` | wait counter, decremented by the update itself (`0x416a7a`, `0x416d09`) |
| `+0x28` | motion state; position floats first (`lea ecx, [esi+0x28]`). `motion+0x4a0` is set to 1 on a player hit by `0x416e3a` |
| `+0x4d4` | promotion gate (= `motion+0x4ac`); not written by bullet code (§13) |
| `+0x520`..`+0x548` | four (x,y) pairs, the draw quad |
| `+0xbfc` | collision countdown, decremented by the update itself (`0x416d14`) |
| `+0xc04` | flag word (`g`); `0x80000000` = delay flag, reads `0x00000008` once cleared |
| `+0xc0e` | state word (16-bit, `st`). The jump table at `0x4167dd` dispatches 1..5; anything else goes to `0x416c40`, past every timer. State 2 = entering (`0x4167e4`), state 1 body `0x416883`, state 3 set on a player hit, state 5 = cancel animation |
| `+0x10a8` | delay countdown (`-= rate`); int `+0x10ac` (`c1`), rate `+0x10b4`, set through `0x408b00`; fast path at `0x41698e` |
| `+0x12e8` | second countdown; int `+0x12ec` (`c2`), rate `+0x12f4`, set at `0x4190bb`; fast path at `0x416a2c` |
| `+0x13c0` / `+0x13c4` | timer prev / int (`k`), ticked by the manager after every update (`0x4171b7`); rate set at `0x416f82` |
| `+0x13d4` / `+0x13d8` / `+0x13dc` / `+0x13e0` | age timer prev / int / float / rate, ticked by the update at entry; rate set to `&0x4d8f58` at `0x416fdb` and `0x417961` |

The player collision test is `0x416d70` (calls `0x44ee80` / `0x44efa0`). The off-screen test at
`0x416620` culls on `pos ± vel * 0.5` against `[-192, 192]` by `[-64, 448]`.

### Enemy (manager `[0x4db52c]`)

Read off the game's own sprite placement, `0x424810`, called as `lea ecx,[ebx+0x11f0]` from the
enemy update at `0x42476b` and `0x4247ef`.

| | |
|---|---|
| `layout.enemy_list` | `0xd0`, walked as `{enemy, next}` by the update at `0x422974` |
| sprite sub-object | `enemy+0x11f0`: position `+0x44`, 14 VM ids `+0x124`, offsets `+0x164` (three floats each), parent slots `+0x224`, flags `+0x4054` |
| `layout.enemy_flags` | `0x5244` = `0x11f0 + 0x4054`; the word the manager tests before updating an enemy (`0x422995`) |
| `layout.enemy_position` | `0x1234` = `0x11f0 + 0x44` |
| `layout.enemy_skip_mask` | `0x02000000`, the manager's own "skip this enemy" bit |
| absolute-position bit | `0x04000000` in the same word (TH13: `0x08000000`) |
| VM position | `+0x59c` (TH13: `+0x574`); a parent's contribution at its VM `+0x3c`, the same three words TH13 reads |

Cross-check: the enemy update also reaches the id array as `lea esi,[ebx+0x1314]`, and
`0x11f0 + 0x124 = 0x1314`.

### Item (update body `0x438550` behind `0x439750`, EDI = item)

Stride `0xc18`. Timer prev `+0xbc8`, int `+0xbcc`, float `+0xbd0`, rate `+0xbd4`, ticked in the
per-item tail at `0x438d0c`. Fall speed `+0xbfc`, countdown `+0xc00`.

```text
438716  movss xmm0,[0x4d8f58]      ; the game speed
43871e  mulss xmm0,[0x4c1918]      ; 0.03
...
43892f  addss xmm0,[0x4c1968]      ; += 0.2 into [edi+0xbfc], once a frame
438937  movss [edi+0xbfc],xmm0
```

`+= 0.2` a frame into a per-entity fall speed is TH13's item model. Motion is
`pos += vel * speed` (`0x4386b6`).

### Laser (list walk `0x43a570` behind `0x43a6a0`)

Four classes, vtables `0x4be2fc`, `0x4be364`, `0x4be3cc`, `0x4be434`, updated through
`[vtable+0x10]` (`0x440910`, a no-op `0x443c50`, `0x43e040`, `0x43be20`). Base timer of every
managed object: prev `+0x18`, int `+0x1c`, float `+0x20`, rate `+0x24`, ticked at `0x43a603`.

### Sprite VM and ANM manager

`vm_anm_off = 0x30`, `vm_layer_off = 0x24` (same as TH13), `vm_script_off = 0` (unknown; no rule
needs it, and the trace dump stops short of where TH13's sits). The per-VM update is `0x46fe50`.
The manager keeps two VM lists at `[this+0xfe8210]` and `[this+0xfe8208]`.

The two offsets were read off a running game. With the offsets zero no VM is classified, no dim
rule can match, and `dim_vm_trace` scans each VM's first three hundred words for a pointer to a
loaded ANM record (validated by the name ending in `.anm`) and logs the offset:

```text
draw      vm anm pointer at +0x30: slot 3 bullet.anm
```

All 280 VMs across a stage reported `+0x30`; no other offset ever matched. A guessed
`vm_anm_off` prints plausible ANM names that are not the ones drawn, and nothing downstream
catches that.

### Replay manager

`layout.replay_stage = 0x218`, `layout.replay_frame = 0x210`, `layout.replay_stages = 0x20`
(eight stage records). Same numbers as TH13, read independently: `0x455f7f` stores the stage
index at `+0x218` and the next instruction indexes the array at `+0x20` with it. The label shown
in the menu is at `[+0x1c]`; `[+0x10]` is the load mode (§10).

## 5. Systems

### Update list

Callback census (`debug=1`) on a first stage. Priority is the registration priority.

| priority | callback | seen | class table name | mode |
|---|---|---|---|---|
| 1 | `0x444890` | always | Update01 | frame |
| 3 | `0x4447b0` | always | Update03 | frame |
| 4 | `0x40b8e0` | always | Ascii | frame |
| 6 | `0x459f30` | always | Title | frame |
| 8 | `0x47e7f0` | always | AnmSpritesEarly | **sub** |
| 27 | `0x41ee80` | always | Effects? | frame |
| 29 | `0x47e7c0` | always | AnmSpritesLate | **sub** |
| 9 | `0x448bd0` | in a stage | Update09 | frame |
| 11 | `0x436d70` | in a stage | Update11 (stage / background) | frame |
| 12 | `0x455e40` | in a stage | ReplayRecord | frame |
| 13 | `0x40eb70` | in a stage | Update13 | frame |
| 17 | `0x457ee0` | in a stage | Update17 | frame |
| 18 | `0x44ec60` | in a stage | Player | **sub** |
| 20 | `0x411eb0` | in a stage | Update20 | frame |
| 21 | `0x422a60` | in a stage | Update21 | frame |
| 22 | `0x43a6a0` | in a stage | LaserManager | **sub** |
| 23 | `0x417610` | in a stage | BulletManager | **sub** |
| 24 | `0x439750` | in a stage | ItemManager | **sub** |
| 26 | `0x41cb50` | in a stage | Update26 | frame |
| 28 | `0x431a40` | in a stage | Front (GUI) | frame |
| 30 | `0x455e60` | in a stage | ReplayPlayback | frame |

A system may be `MODE_SUB` only when everything it counts in whole frames still does so with its
update running several times a frame, and when every once-a-frame reader of its state still gets
the stock answer (§8). A callback in the wrong class is silent: a system stepped six times a
frame with its own timers counting in whole ones ([DEVNOTES_RUNTIME.md §7](../DEVNOTES_RUNTIME.md)). Classifying anything also makes `class_count` non-zero, which makes
sub-stepping available in the menu.

Enemies stay `MODE_FRAME` in TH14 as in TH13: their behaviour is an ECL script, and an
interpreter stepped six times a frame is a different game. Their sprites are interpolated (§9).

The menu's "Sub-stepped subsystems" list shows only `MODE_SUB` classes (also for TH10–13). A
checkbox against a `MODE_FRAME` class does nothing and makes a system look ruled out.

### The two sprite passes

`0x47e7f0` (priority 8) is `jmp 0x47e6c0`. `0x47e7c0` (priority 29) tests a flag on the
supervisor at `0x4db558` and either returns 1 or falls into `jmp 0x47e5e0`. Both are registered
from `0x47a780` (sites `0x47aa5b` for priority 29, `0x47aac8` for priority 8), the shape of
TH13's two ANM managers (`0x46f330`, `0x46f360`). The two bodies are the same walk over two lists
of one manager object, calling `0x46fe50` per VM and re-bucketing survivors by layer.

They need no per-frame hook: a sprite VM's motion is in units of the game speed and nothing in it
counts frames. `0x473139`, `0x47318e`, `0x4731e0`, `0x473238`, `0x47325c` are
`mulss xmm0,[0x4d8f58]` in the interpolator, and `0x474e7b`, `0x474f1b`, `0x474fbb`, `0x475054`,
`0x475166`, `0x475276`, `0x47fdc9`, `0x47fe22` store the speed's address into interpolator
fields.

Systems that step their own VMs by calling `0x46fe50` directly (about 190 call sites) from a
`MODE_FRAME` callback still advance once a frame.

TH13 names its pair world and UI. Which of TH14's is which is not established; the late pass
being skipped on a supervisor flag suggests the pass that stops with the game. Both get the same
treatment, so they are named for when they run.

### How the systems were identified

| method | result | trap |
|---|---|---|
| ANM strings in the constructor that contains the `mov [esi+8], <callback>` store | table below | a name identifies a texture, not a system, when two systems share one (`bullet.anm` is used by bullets, lasers, items and `0x41ee80`) |
| normalised code shape against TH13's named callbacks (immediates and absolute displacements masked) — `tools/match_functions.py` | scores below | the callbacks are thunks ending in `jmp`; a shape that stops at the first `jmp` compares wrappers, and three different callbacks scored 1.00 against TH13's Stage until the matcher followed the tail jump |
| registration priority | ordering only | TH13's BulletManager is priority 23, while TH14's `0x41ee80` at 27 also loads `bullet.anm` |
| draw trace prim counts | see draw list | 270 prims of one texture at draw priority 31 read as "enemies"; they are items |
| constants scan | found the item constants | a fixed 0x800 window from a call-target start ran past the four-line destructor `0x438530` into `0x438550` and attributed the constants to the wrong function |
| reading the update body | settled items (§4) | — |

| priority | callback | registered in | strings there |
|---|---|---|---|
| 4 | `0x40b8e0` | `0x40b530` | `ascii.anm`, `ascii_960.anm`, `ascii_1280.anm` |
| 6 | `0x459f30` | `0x459620` | `title.anm`, `title_v.anm` |
| 22 | `0x43a6a0` | `0x43a350` | `bullet.anm` |
| 23 | `0x417610` | `0x416110` | `bullet.anm` |
| 27 | `0x41ee80` | `0x41eb80` | `bullet.anm`, `effect.anm` |
| 28 | `0x431a40` | `0x42ea30` | `front.anm` |
| 8, 29 | `0x47e7f0`, `0x47e7c0` | both `0x47a780` | — |

Evidence per system:

- `0x417610` BulletManager: 0.91 shape match, `bullet.anm` in its constructor, 658 sprites
  (1316 prims) from one texture in the play area on a stage frame.
- `0x47e7f0` / `0x47e7c0` ANM managers: one registering function as in TH13; 0.62 on the early
  one.
- `0x43a6a0` LaserManager: 0.52 shape, `bullet.anm`; confirmed by reading the list walk (§4).
- `0x436d70` stage: 0.59 shape, drawn first.
- `0x40b8e0` text: `ascii.anm`, 3796 prims of glyphs.
- `0x44ec60` player: its draw `0x44ec70` draws `pl00.anm`; the shape matcher scored 0.27.
- `0x439750` items: the update body (§4).

### Draw list

Draw callbacks pair with update callbacks by address (usually 0x10 apart).

| system | update (prio) | draw (prio) | trace |
|---|---|---|---|
| text / HUD digits | `0x40b8e0` (4) | `0x40b900` (72) | 3796 prims, one texture, full screen |
| stage / background | `0x436d70` (11) | `0x436d80` (2) | drawn first, into the offscreen target |
| bullets | `0x417610` (23) | `0x417640` (35) | 1316 prims, one texture, play area; `bullet.anm` L0 |
| player | `0x44ec60` (18) | `0x44ec70` (28) | ~10 quads, one texture; `pl00.anm` |
| items | `0x439750` (24) | `0x439780` (31) | 270 prims, one texture; `bullet.anm` L0 |
| GUI | `0x431a40` (28) | `0x431a50`, `0x431a60` (48, 45) | `front.anm` in its constructor |
| replay record / playback | `0x455e40` (12), `0x455e60` (30) | `0x455eb0` (62) | — |
| ANM managers | `0x47e7f0` (8), `0x47e7c0` (29) | layer callbacks `0x47e0f0`..`0x47e550` | — |

Per-priority ANM and layer from the VM trace:

| prio | callback | ANM / layer |
|---|---|---|
| 3 | `0x40eb80` | `st01wl.anm` L0 |
| 5 | `0x47e0f0` | `title.anm`, `front.anm` L0 |
| 9 | `0x47e110` | `effect.anm` L2 |
| 19 | `0x47e1f0` | `enemy.anm` L8 |
| 27, 28, 29, 30 | `0x47e230`, `0x44ec70`, `0x47e240`, `0x47e250` | `pl00.anm` L13, L14, L15 (and `effect.anm` L14 at 29: focus ring, hitbox) |
| 31, 35 | `0x439780`, `0x417640` | `bullet.anm` L0 |
| 42, 43 | `0x47e320`, `0x47e2b0` | `effect.anm` L20, L21 |
| 49, 51 | `0x47e2c0`, `0x47e2d0` | `front.anm` L22, `st01logo.anm` L23 |
| 54+ | the rest | `title.anm`, `ascii.anm`, `front.anm`, `text.anm` |

## 6. Dimming

Mechanism: [DEVNOTES_RUNTIME.md §3b](../DEVNOTES_RUNTIME.md). `world_prio = 19`, the first
world object (`enemy.anm` L8). Rules in `th14_dim_rules`, in match order:

| prio | ANM | layers | class | why |
|---|---|---|---|---|
| 31 | any | any | `DIM_ITEMS` | the item manager's draw; matched on priority with no ANM, as TH13's item rule is. Items do not go through the sprite VM draw, so the VM census never shows them |
| 35 | `bullet.anm` | any | `DIM_NONE` | bullets are what the rest is faded for |
| 9 | `effect.anm` | 2 | `DIM_EFFECTS` | effects under the world; TH13 has the same rule at its priority 8 |
| 27–30 | `effect.anm` | 13–15 | `DIM_NONE` | focus ring and hitbox: `effect.anm` L14 at priority 29, between `pl00.anm` L13 and L15. TH13's carve-out is `effect.anm` layer 12 inside its 12..43 world band. Widened to the player's three layers: the cost of being wrong is an effect near the player that does not fade, against a hitbox that disappears |
| any | `pl*.anm` | 14 | `DIM_NONE` | the player herself |
| any | `pl*.anm` | 13–15 | `DIM_PLAYER_SHOTS` | what she fires |
| any | `pl*.anm` | any | `DIM_NONE` | anything else of hers |
| any | `effect.anm` | any | `DIM_EFFECTS` | |

Player shots are separated by draw rate, since every stream is `pl00.anm`: over about 5054
frames her own callback (priority 28) drew 5054 times, layer 13 (priority 27) 77658 times and
layer 15 (priority 30) 65172 times — fifteen and thirteen draws a frame.

`DIM_SPECIAL` is unclaimed: TH13 has it for divine spirits, and nothing in TH14 competes with
bullets for attention that way.

`UI_DIM_CLASSES` reports the classes the profile's rules mention, so the menu offers no slider
that does nothing.

`bullet.anm` on layers 20 and 21 looks like the items. It is not: a `dim_items` rule on it faded
nothing, and a stage with items everywhere produced no such row. What draws it is not
established (bullet cancels are likely).

With `world_prio = 0` the menu reports dimming unavailable but the dispatch is still wrapped, so
the debug draw trace runs; that is how the rules above were obtained for a new game.

## 7. The game speed

`addr.speed = 0x4d8f58`, identified three ways: the save/override/restore idiom at `0x424780`
(TH13's `SPEED_ONE_TEMP` shape), 61 `mulss` reads across gameplay code, and the item manager
multiplying its per-frame step by it at `0x438716`.

`set_factor` writes `g_logical * dt` there every tick, and the game writes it too. The two
compose only if every game write that sets an absolute value is a described speed site that
folds the factor in. A profile with `speed` and no sites holds the game at 1.0 and disables its
slow-motion and pause; `install()` logs a warning for that combination.

There are 25 writes. A search for `movss [0x4d8f58], xmm*` alone finds 12; the other 13 are
`mov dword [0x4d8f58], imm32`, storing `1.0` or `0.0`.

| encoding | sites |
|---|---|
| `movss [speed], xmmN`, 8 bytes | `0x40dac5` `0x411780` `0x4247a6` `0x4247fa` `0x429796` `0x43a6f1` `0x449097` `0x44a165` `0x44af39` `0x44b0e5` `0x46feff` `0x472d4c` |
| `mov dword [speed], 1.0f`, 10 bytes | `0x40747a` `0x40da9a` `0x435b44` `0x436100` `0x444a00` `0x448eb6` `0x448ff8` `0x449eff` `0x44a0b5` `0x44df30` `0x46fe8a` |
| `mov dword [speed], 0.0f`, 10 bytes | `0x43a6dd` `0x46ff09` |

Three idioms, which need different treatment:

- **Absolute store** (`speed = 1.0` at a stage start, `speed = 0.0` to freeze): clobbers the factor; becomes
  "record N, store N × factor".
- **Save, set, restore** (`movss xmm,[speed]` into a local, an absolute set, a call, the local
  written back; `0x40da9a`/`0x40dac5`, `0x43a6dd`/`0x43a6f1`): the set needs the
  treatment above. The restore writes back the already-composed value; patching it folds the
  factor in twice.
- **Read, modify, write** (`0x424777`: read, scale, clamp, store): composes by itself.

A heuristic "does it read the speed just before?" mislabels the set of a save/set/restore triple
as self-composing, because the save is the read it sees. Each site has to be read.

Twelve sites are in `th14_speed_sites`; thirteen are correct untouched.

| site | treatment | why |
|---|---|---|
| `0x40747a` (speed object's initialiser) `0x435b44` (game state change) `0x436100` (supervisor reset) `0x444a00` (player reset) `0x44df30` (end-of-stage sequence) | `SPEED_ONE_PERM` | the new logical speed is 1.0 |
| `0x40da9a` (around three slow-motion updates; restored `0x40dac5`) | `SPEED_ONE_TEMP` | 1.0 for the duration of something the game restores after |
| `0x448eb6` `0x448ff8` `0x449eff` `0x44a0b5` (pause menu opens; restored `0x449097` `0x44a165` `0x44af39` `0x44b0e5`) | `SPEED_ONE_TEMP` | same |
| `0x46fe8a` (a sprite flagged "unaffected by slow-motion") | `SPEED_ONE_TEMP` | same |
| `0x429796` | `SPEED_ECL`, `SPEED_SRC_XMM0` | the script instruction that sets the speed |
| `0x40dac5` `0x449097` `0x44a165` `0x44af39` `0x44b0e5` `0x4247fa` `0x472d4c` | none | writes back the raw global the game saved, already scaled |
| `0x43a6dd` `0x46ff09` | none | stores 0; 0 × factor is 0 |
| `0x43a6f1` | none | the restore half of the freeze at `0x43a6dd` |
| `0x4247a6` | none | `speed*(1-k)` clamped to [0, 1]; multiplicative, and the clamps cannot bind while the factor is at most 1 |
| `0x46feff` | none | `saved*(1-k)` from the value `0x46fe50` read at entry |
| `0x411780` | none | unreachable: `movss [speed],xmm1; ret` with no call, jump or data reference in the image |

`SPEED_ONE_TEMP` and not `SPEED_PAUSE_SET`: the pause pair is for games whose restore writes a
value the runtime has to reconstruct. TH14's restores write back the raw global, so patching the
set alone is necessary and sufficient.

## 8. Sub-stepping: hazards and patch sites

Timer constructors store `&0x4d8f58` into the rate field in 199 places. Under sub-stepping those
timers advance a fraction of a frame per tick and each integer crosses a whole number on exactly
one tick per frame. Offsets below were read off the constructors, not taken from TH13.

Hazards a rate pointer does not fix. Every hook is one of these:

1. **An integer the update moves itself.**
2. **A block gated on a timer's integer being N**, which holds for a whole frame and so runs on
   every tick of it.
3. **A truncation** of a quantity already scaled by the speed.
4. **An exponential approach** (a fixed proportion per frame).
5. **A guard written against a timer's prev/int pair**, true on only one tick per frame — and
   that tick need not be the boundary tick on which a `MODE_FRAME` reader asks. At 360 Hz, where
   dt is not exact in float32, it never is.
6. **A float `a += b` with no speed multiply**, sitting among rates that are scaled.
7. **A rate-aware operation on a discrete amount** (`timer_rewind`).
8. **A null rate pointer**: the tick adds a whole 1.0 per call.
9. **Anything that consumes the replay RNG.**
10. **The integer lags within a frame.** Stock ticks 8.0 → 9.0 and the whole frame's update sees
    9. Sub-stepped, the float goes 8.0 → 8.167 on the boundary tick and the integer stays 8 until
    the last sub-tick, so for five sixths of the frame every reader sees last frame's value. A
    decreasing timer mirrors it: truncation toward zero drops the integer on the first sub-tick,
    so a countdown's zero arrives five sixths of a frame early. The integer is correct at frame
    boundaries, so a boundary trace cannot see this. There is no general fix: `ceil` would have
    to replace an inline truncation in 199 places, and biasing the float breaks every
    interpolation that reads it (laser widths). Fix per decision, where the decision matters.
11. **Order within a frame.** A system run six times a frame can see state that a once-a-frame
    system writes after it. The reading instruction is correct; it is asked too often (§13).

For every new `MODE_SUB` callback ask: who reads this system's state, and how often?

### Patch sites (`th14_install_sites`)

| site | len | hazard | what the stub does |
|---|---|---|---|
| `0x416a7a` | 11 | 1 | bullet wait counter `[+0x24]--` inside the behaviour loop: only when the bullet timer's integer changed this tick (`prev +0x13c0` vs `int +0x13c4`); else skip to `0x416a85` |
| `0x416d09` | 28 | 1 | the same counter and the collision countdown `[+0xbfc]--` (`0x416d14`) at the end of the update; else skip to `0x416d25`. The copied bytes keep their own short jumps: each skips only instructions inside the copy |
| `0x416b0f` | 21 | 2, 9 | bullet state 5 spawns the cancel effect when the timer integer is 3 (`cmp eax,3`). Keeps the game's `jl 0x416c40` and `jne 0x416bd9`, adds "integer changed this tick" (else `0x416bd9`, motion only); spawn continues at `0x416b24` |
| `0x416877` | 12 | 11 | `gate_block(0x416877, 12, 0x416c40, 0, -1)`: the state 2 → 1 promotion (`mov eax,1; mov word [esi+0xc0e],ax`) only on the frame boundary. State-2 motion at `0x4167e4` still runs every tick. See §13 for what it does and does not fix |
| `0x43a603` | 13 | 8 | laser manager base timer: a null rate becomes `&g_factor`; an object's own rate is kept; continues at `0x43a610` |
| `0x4385c3` | 12 | 1 | item state 5 despawn countdown `dec [edi+0xc00]`: only when the item timer's integer changed (`+0xbc8` vs `+0xbcc`); else `0x438d96`; continues `0x4385cf` |
| `0x438631` | 25 | 1 | item state 1 "wait, then fall" countdown: the game's "already expired" branch (`jle 0x43865c`) stays ahead of the gate, so a falling item updates every tick; continues `0x43864a` |
| `0x44dbf8` | 7 | 2, 9 | player state dispatch `jmp [eax*4+0x44ebf4]`: on a minor tick any state but 1 goes to the tail `0x44dfd1` |
| `0x44d774`, `0x44d77c` | 8, 8 | 3 | `movement_cvttss(0x44d774, 0x8f, 0x604, R_ECX, 0)` and `(0x44d77c, 0x87, 0x608, R_EAX, 1)`: carry the truncation residual across sub-steps |
| `0x44e16f` | 14 | 5 | invincibility blink guard: compare `g_ptf_prev` / `g_ptf_cur`; unchanged → `0x44e1a5`, else load `[edi+0x690]` and continue at `0x44e17d` |
| `0x44d924` | 6 | 1 | focus counter `[+0x1830c]++`: `gate_block(0x44d924, 6, 0x44d92a, R_EDI, 0x68c)` |
| `0x44d9a1` | 9 | 4 | option approach: minor tick → `0x44db3d`; else the game's `cmp eax,0x1e; jl 0x44db3d`, then `0x44d9aa` |
| `0x44e01f`, `0x44e040` | 10, 10 | 6 | shot rates: multiply the increment by `g_factor`; continue at `0x44e029`, `0x44e04a` |
| `0x451450` | 8 | 5 | shot-versus-enemy guard on the player state timer: `g_ptf_prev` / `g_ptf_cur`; changed → `0x451463`, unchanged → `0x451458` (return 0) |
| `0x44e08d` | 13 | 5 | shot timer tick: prev = int; minor tick → `0x44e0dd`; boundary tick counts down by `g_logical` (continues `0x44e096`) |
| `0x45101a` | 7 | 7 | shot-cycle rewind: swap the timer's rate pointer for `&g_logical` around `call 0x414420`; continues `0x451021` |
| `0x45131b` | 13 | 5 | weapon timer tick: prev = int; minor tick stores int and float back unchanged (`0x45135f`); boundary tick advances by `g_logical` (`0x451328`) |
| `0x449a29` `0x44aa5c` `0x44b973` `0x4617b8` | 5 | — | replay save call sites (§10) |
| `0x4549bc` `0x454b40` | 5 | — | replay load call sites that play (§10) |
| debug only | | | `0x41686a` (7), `0x451463` (6), `0x4514ce` (11), `0x4514dd` (8), `0x451618` (7), `0x451671` (6) — §12 |

The install log ends with `TH14 site patches installed (%u bytes of stubs)`.

### Bullets

The update is `0x416700`. The three raw decrements and the cancel-effect spawn are the only
hand-counted parts; both countdowns and the age timer carry the game speed as their rate. The
null-rate branch of the countdowns (subtract a whole 1.0) looks like a per-tick bug. It is not
reached: `0x4190bb` stores `&0x4d8f58` into `+0x12f4`, and `0x408b00` stores it into `+0x10b4`.

### Lasers

Motion multiplies by the game speed (`0x43bcd3`, `0x43bf8d`, `0x43e0a5`, `0x43e0c8`, `0x4426f5`
…), every timer inside a laser has a rate pointer at the speed, no integer is counted by hand,
there is no "timer is exactly N" gate, and the updates never touch the player.

The base timer is the exception: the laser classes leave its rate pointer null, so it would add
1.0 six times a frame and the phases compared against it (`0x43e1bd`, `0x43e207`) would last a
sixth as long. The hook gives it `g_factor`, not the game speed, because this timer is
deliberately not subject to the game's slow-motion. With no sub-stepping the fraction is 1.0 and
the game's own "within 1% of 1.0" test at `0x43a614` makes the result bit-identical. The float
must stay continuous as well as the integer: one laser class interpolates its width from it
(`0x43e240`).

### Items

Gravity is `speed * 0.2` (`0x438716`), so an item falls at the same rate however often it is
stepped. Only the two countdowns count frames by hand. Nothing reads items once a frame: the
manager collects against the player inside its own update.

### Player

- **State dispatch.** States other than 1 (death, respawn, stage clear) count whole frames, spawn
  effects on exact frame numbers and draw on the RNG. Gating the dispatch is one hook instead of
  a dozen. The tail steps the sprite VMs, so it still runs every tick and the death animation
  stays smooth.
- **Movement.** `cvttss2si` of a velocity already scaled by the speed: six sixths truncate to
  less than one whole, and a velocity under six units a frame truncates to nothing, so the
  player would not move. `movement_cvttss` does nothing when the factor is 1, because the game's
  own slow-motion truncates the same way in stock.
- **Blink.** "The state timer's integer changed and is a multiple of 3" (`0x44e16f`) would hold
  for a sixth of a frame. Asked of the timer's float before and after this tick's Player call
  (`g_ptf_prev` / `g_ptf_cur`), it is true every tick, so the "multiple of 3" holds for a whole
  frame. TH13 replaces the identical guard at `0x446888`.
- **Options.** `(target - pos) * [+0x182bc] / 100` at `0x44d9aa`, blend 30. Thirty percent six
  times a frame is 88% a frame; the options would sit on the player instead of trailing her,
  which is a gameplay difference. The approach runs on frame boundaries; the tail from
  `0x44db3d`, which writes the option position into its sprite VMs, runs every tick. Smoothness
  comes from interpolation (§9).

### Shots against enemies (`0x451400`, called by the enemy with its position and radius)

The enemy code is `MODE_FRAME`, so it asks on the boundary tick; both guards must be true there.

- **Player state timer guard** (`0x451450`): "integer did not change → return 0". Unpatched,
  nothing the player fires ever hits.
- **Per-shot guard** (`0x4514ce`): "this shot's integer changed this tick and is a multiple of
  the interval at `+0x80`" — the hit cadence in frames. Fixed where the timer is ticked, in the
  player's tail (`0x44e08d`, a countdown by its rate pointer): one whole frame by the logical
  speed on the boundary tick, nothing on the others, prev set to the integer. Per-frame total
  and shot lifetime are unchanged.

TH13 has this pair at `0x446888` and `0x4436b4`.

**Shot rates.** `[-0x64] += [-0x60]` and the angle `[-0x5c] += [-0x58]` from the loop cursor at
`0x44e01f` and `0x44e040`. Nothing multiplies them by the speed; sub-stepped, a homing shot's aim
converged six times as fast and landed shots that would have missed. Reported by a player as
"slightly more damage". TH13 has the same pair and fix at `0x443691`.

**Weapons.** Reimu's focus weapon kept moving and stopped damaging. Every weapon type's update
asks its timer's integer "did it change, and is it a multiple of N": the cadence on which the
weapon fires, retargets and drives the damage volume it owns in the shot array (`weapon+0xc0`,
flags set to the swept shape at `0x451b17`). The timer is ticked at `0x45131b` with the game
speed as its rate. Same treatment as the shot timer. Weapon motion is a MotionState stepped every
tick by `0x4510b0`, so nothing moves in steps.

**Shot cycle.** One timer at `[+0x18338]`, driven by `0x450fb0`. Pressing shoot starts it at 0,
every integer it passes fires that pattern step (`0x450ed0` takes the integer), and at 14 with
the button held it rewinds by 14. `timer_rewind` (`0x414420`) multiplies the amount by the
timer's rate (`mulss xmm1,xmm2`), so sub-stepped it rewound by 14/6: the timer fell from 14 to
about 11.7 and only steps 12 and 13 came round again. Holding fired one or two shots and then
nothing; tapping (which sets the timer to -1, then 0) fired a burst. A rewind is a discrete
reset, so it takes the logical speed. `0x414420` has exactly one caller, and at 60 Hz
`g_logical` is what the rate already holds, so the site is bit-identical with sub-stepping off.
TH14 has no second rewind site; the sweep across TH10–13 is in
[DEVNOTES_RUNTIME.md §11](../DEVNOTES_RUNTIME.md).

## 9. Render interpolation

**Enemies.** `enemy_interp` places enemy sprites every tick between the last two frame
positions — the same one-frame lag everything sub-stepped has. `th14_place_enemy` walks the 14
VM ids of the sub-object (§4); unless the absolute-position bit is set it adds the sprite offset
and the parent VM's contribution, and writes VM `+0x59c`.

**Options.** `place_options` is a second, optional profile hook called from the same place as
`place_enemy` with the frame-boundary flag. The adapter keeps its own tracking (eight slots,
`th14_opt`). A delta beyond ±6144 units (a whole screen) in one frame is a teleport and is not
interpolated. The option loop's tail at `0x44db3d` writes the two VMs from the two fixed-point
integers scaled by the 1/128 at `0x4c1900`, z zero; `th14_place_options` runs after the pass and
overwrites them.

The interpolation pass checks `addr.anm_manager` before use, so a profile may have one hook
without the other.

## 10. Replay extension

Installed. A replay records the logic rate and simulation settings in an appended `USER` chunk;
playback restores them. Sub-tick input depends on it, because it samples more often than the
replay format records.

| | |
|---|---|
| save | `0x455490`, stdcall, four stack arguments (filename, the label the game shows, two more), `ret 0x10`. TH13's is fastcall with one. Callers push four and do not adjust `esp`; `0x44aa5c` reaches the same local through `[esp+0x2c]` before the call and `[esp+0x20]` after. The label is copied into the manager at `[+0x1c]` a byte at a time from `0x4554d0` |
| save call sites | `0x449a29`, `0x44aa5c`, `0x44b973`, `0x4617b8` — all genuine saves, all hooked. Three are inside the two functions that build the `th14_%.2d.rpy` filename |
| load | `0x455c20`, thiscall: manager in ECX, filename pushed, `ret 4` |

Load call sites:

| site | kind | what it does |
|---|---|---|
| `0x4549bc` | play, hooked | in the function that owns the game's one replay manager: stores it in `[0x4db688]`, writes the stage into `[+0x218]` and -1 into `[+0x210]`, dispatches here on mode 1 |
| `0x454b40` | play, hooked | the same function, mode 2 |
| `0x454fd3` | peek, not hooked | allocates `0x320` bytes, memsets, writes 2 into `[+0x10]`, loads into that, calls `0x454b60` to pull the header out, discards it |
| `0x45ee0f` | peek, not hooked | the same shape, inside the function that globs `th14_ud????.rpy` — the menu building its list |

The peek sites must stay unhooked. The load wrapper calls `restore_replay_settings()` and reads
simulation metadata from the file, and on a file the runtime does not recognise it shows a
message box — inside the game's loop, once per file. With all four hooked the wrapper ran 25
times (`th14_01.rpy` to `th14_25.rpy`) when the save menu opened and the game went down. The
peek sites stay in the signature list for identification. The same play/peek table for TH10–13
is in [DEVNOTES_RUNTIME.md §11](../DEVNOTES_RUNTIME.md).

Both wrappers live in `th14.c`, as TH13's load does, instead of adding two shapes to the shared
installer. `th14_replay_save_c` has the game function's signature, so the call site pushes four
and the wrapper pops four. `th14_replay_load_entry` is `push [esp+4]; push ecx; call
_th14_replay_load_c@8; ret 4`. Both were checked by disassembling the compiled runtime.

`data_dir`: `0x46a160` builds the path at startup into the global object at `0x4f5a18`:
`GetEnvironmentVariableA` with `APPDATA` into `[obj+0x2d]`, then `\ShanghaiAlice`, then `\th14`,
then a separator. The buffer is `0x4f5a45`; the compiler folds `esi` back to that constant at
`0x46a216`. Without `data_dir`, `replay_path` looks for `th14_01.rpy` beside the executable and
every appended chunk is silently lost.

Unverified: TH14's loader tolerating the extra `USER` chunk after the game's own data. TH10–13
tolerate it; TH14 is a different build of the loader.

`replay_check` sets the logic rate on playback from the rate in the file. A file with no HFR
chunk plays at 60. A replay recorded at one rate and played at another is not bit-identical in
general (§13), so playing at the recorded rate is the only guarantee.

## 11. Partially described profiles

Four crashes in the first four builds were zero profile fields dereferenced on the run path. The
resulting rules are shared-runtime behaviour; the TH14-specific facts:

| fault | cause | guard |
|---|---|---|
| `movss [edx],xmm0`, EDX = 0, in `hfr_runner`, reached from `0x46a99e` | `set_factor` storing through `addr.speed` = 0 | `set_factor` tracks the factor and skips the write; `subtick_active` also requires `poll_input` and `game_input` |
| `mov [edx],ecx` in `hfr_frame`, operands from the profile at `+0x78` / `+0x80` (`frame_context_ptr`, `frame_context_value`), after a 293 ms hitch | `update_only_tick` with none of its five addresses | it declines unless all five are present, and the frame hook then asks for at most one tick a frame; `window_pump` leaves the swap chain alone without `pp` (that path runs only with scaling off) |
| crash inside `th14.exe` after `menu: tick rate -> 360 (substep=1)` | sub-stepping enabled from the menu with no class table: every node is `MODE_FRAME`, minor ticks run nothing, and five presents of six draw state the update never advanced | `UI_SUBSTEP_AVAILABLE` is `class_count != 0`, `UI_SUBTICK_AVAILABLE` requires `poll_input` and `game_input`; the menu greys both out and says why; the setters refuse (ini, replay settings, stale values); `recompute_rate` does not leave 60 without classified systems |
| `mov (%edx),%edx` in `hfr_frame`, EDX from the profile at `+0x3c` (`game_input`), only with `debug=1` and a replay manager | the stats line's `input=%08x` in `limiter_stats` | `input_read` / `input_write` answer "no bits held" and drop the write on a zero address (covers the input word, pressed/released edges, the autofocus counter); `option_flags` is guarded at its one use |

A half-described profile must make unsupported features unavailable, not merely switched off:
anything reachable from the menu is reachable. `install()` audits the profile once and logs
`profile: <name> is not described; <feature> is unavailable` per gap, and turns `substep` off
with no class table and `subtick_input` off with no input path (the log otherwise claimed
`substep=1` and `logic rate: 360 ticks/s`). TH14 first printed six lines (`speed`,
`frame_context_ptr`, `cleanup_fn`, `poll_input`, `pp`, `player`), four after the catch-up tick
was described; `game_input` joined the audit later. Still printed: `poll_input`, `game_input`,
`pp`. The rule in `ADDING_A_GAME.md` — "anything a profile leaves out degrades rather than
breaks" — held for the install path and not for the run path before these guards.

## 12. Debug instruments

**Site census.** `E_count` / `site_census_report`: counters a profile's site hooks increment,
printed on the debug stats line. Installed only with `debug=1`, inside the game's own guards in
the shot-versus-enemy test `0x451400`:

| counter | site | |
|---|---|---|
| `tests` | `0x451463` | enemies tested against the shot array |
| `shots` | `0x4514ce` | shot slots examined |
| `notick` | `0x4514ce` | rejected: this shot's timer integer did not change on this tick |
| `cadence` | `0x4514dd` | rejected: the integer is not a multiple of the shot's interval |
| `hit` | `0x4514dd` | reached the shape test |
| `melee` | `0x4514dd` | of those, the swept shape (`test cl,2`) — a focus weapon's damage volume |
| `land` | `0x451618` | shots past the geometry |
| `dmg` | `0x451671` | total damage applied (`add [esi+0x14],ecx`) |

The two guards turned out to reject nothing once fixed; `land` and `dmg` measure damage instead
of opportunity. The harness sets `cfg.debug` before validating the patch plan, so debug-only
sites are frozen and checked like any other.

**`replay_trace`** (needs `debug=1`). Keeps the current logic rate across playback instead of
dropping to 60, and logs one line per game frame: the replay frame number, the player's
fixed-point position, and the `trace_state` fingerprint (`b=`, `e=`, `n=`).
`replay_trace_frame()` samples at the top of the pass that carries `g_major`, before the frame's
first tick — the frame boundary in both modes. Sampling at the end of the pass looks equivalent.
It is not: under sub-stepping that pass is the first sub-tick, a sixth of a frame at 360 Hz, so
every line differs by most of a frame of movement (observed: frame 18, `on=(94, 51200)` against
`off=(576, 51200)`; 94 = 576/6, a steady ~482 units behind).

**`th14_trace_state`.** FNV-style hash (raw bits, not values) over every bullet slot, live or
dead — a dead slot holds what the last bullet left, which is a function of the run's history —
and over the enemy list as the interpolation walks it (`+0x5244`, `+0x1234`, `+0x1238`). Third
field: count of slots with a non-zero flags word. Bullet fields mixed: `+0x20`, `+0x28`, `+0x2c`,
`+0x24`, `+0xc04`, `+0xc0e`, `+0x10ac`, `+0x12ec`, `+0x13c4`, `+0x13d8`. Hashing only `+0x20`
and the position reports a first divergence too late: a delay that starts a frame early changes
neither until something has moved.

**`th14_trace_dump`.** For frames between `replay_trace_from` and `replay_trace_to`, one `bd`
line per live slot, raw hex: slot index `i`, `s` (`+0x20`), `t` (`+0x24`), position and velocity
triples, the rest of the motion block, `g` (`+0xc04`), `c1`, `c2`, `st` (`+0xc0e`), `k`
(`+0x13c4`), `age` (prev, int, float), and the three rate pointers `r` (`+0x13e0`, `+0x10b4`,
`+0x12f4`). A null rate pointer on a live bullet is a timer running per tick. For bullets in
state 2 it also prints `+0x28` to `+0x610` as `bm` lines, 32 dwords a line with the offset in
front; a diff localises a difference to a 128-byte chunk, then a dword.

**`th14_gate_log`.** Hook on `0x41686a` (`cmp dword [esi+0x4d4],0`). Every state-2 bullet that
reaches it inside the trace window logs a `bg` line: replay frame, slot, `g_major`, tick number,
the gate's value, `st`, `k`, `c1`, age. About 33 × 6 × 6 lines in a six-frame window. XMM is
saved around the call, as in the speed stubs.

Method: play one file back twice (stock, sub-stepped) and diff the whole logs. Comparing a
stretch by eye produced one wrong conclusion here (§13).

## 13. Parity: measured results

### Damage

Report: the player does slightly more damage sub-stepped. After the guard and shot-rate fixes,
on one spell card with sub-stepping on and off: damage per landed hit 62.6 and 62.4; hit rate
per shot alive varied by about a quarter within each condition, more than between conditions.
No difference was measurable. No valid test has confirmed parity either.

### The stock-replay test is not a parity test

"Record with sub-stepping off, play back with it on" looks like a determinism test. It is not: a
replay with no HFR chunk has no recorded rate, so `replay_check` plays it at 60 and the test
compares stock with stock. It passed for that reason. With `replay_trace=1` the rate is kept and
the comparison is real. A replay recorded under stock conditions remains the useful regression
reference; one recorded sub-stepped is valid only against a simulation that matches the build
that made it. The test means nothing with sub-tick input on.

### Sub-stepped recording, stock playback: desync

One spell card recorded at 360 Hz with sub-stepping on, played back sub-stepped
(`replay playback started -> logic rate 360`, `substep=1`) and stock
(`logic rate: 60 ticks/s`, `substep=0`).

Player position:

```
frames 0..633   identical, every one
f=634           on=(-153, 51993)   off=(-195, 51993)
f=635           on=( 103, 51993)   off=(-195, 51993)
...
f=644           on=(2365, 51993)   off=(-195, 51993)
off ends at f=666; on runs on to f=1108
```

The stock run's player stops at her frame-633 position and the trace ends 442 frames early: a
death. The player position cannot localise a divergence. She is a pure function of the recorded
inputs, so two runs agree on her until the frame one of them kills her.

Fingerprint:

```
first difference   x        frame 634   (the death)
                   y        never
                   enemies  never
                   bullets  frame 372
                   count    frame 373
```

```
f=371  off b=ebfa108f n=200 | on b=ebfa108f n=200
f=372  off b=ac96e06e n=203 | on b=46dec4bf n=203
f=373  off b=9e194c45 n=206 | on b=3c0b9623 n=205
f=374  off b=3d228d2c n=209 | on b=3e28b593 n=207
f=375  off b=c96076d2 n=212 | on b=78f49a77 n=209
f=379  off b=e2549192 n=220 | on b=2ba7f82e n=216
```

- Enemies never differ in 667 frames: ECL, enemy positions and their RNG draws are identical.
  Not an RNG divergence.
- Bullets differ from 372 (with the narrow hash) with the live count still equal, so a bullet's
  state differs while every bullet still exists.
- From 373 the sub-stepped run has steadily fewer bullets (gap 1, 2, 3, 3, 3, 3, 4) at the same
  spawn rate (~2–3 a frame): bullets slightly further along cross the off-screen test a frame or
  two sooner. The thinner pattern is the recorded one, so the sub-stepped playback survives and
  the stock playback dies.

Per-bullet dump, frames 369–374:

```
f=372, 203 live slots in both runs
  c1 (the delay countdown's integer at +0x10ac):  115 slots one lower under sub-stepping, 88 equal
  every differing row has g=80000000    (the delay flag set)
  rows differing in anything else:      3
    i=1811   off s=00000013   on s=00000012
```

```
slot 1829   f369 60/60   f370 60/60   f371 60/60   f372 60/60   f373 60/59   f374 59/58
slot 1831   f369 60/60   f370 60/60   f371 60/60   f372 60/59   f373 59/58   f374 58/57
slot 1833   f369 60/60   f370 60/60   f371 60/59   f372 59/58   f373 58/57   f374 57/56
```

Two bullets begin counting each frame in descending slot order. Once started, the countdown runs
at exactly one a frame in both runs. Under sub-stepping each starts exactly one frame early and
stays one frame ahead. At 372 three bullets reach the end of the delay a frame early and flip
bit 0 of the flags word (`0x13` to `0x12`).

One bullet, with state and rate pointers:

```
       off                                              on
f369   c1=60 st=2 k=10 age=9,10,41200000               c1=60 st=2 k=10 age=9,10,41200000
f370   c1=60 st=2 k=11 age=10,11,41300000              c1=60 st=2 k=11 age=10,11,41300000
f371   c1=60 st=2 k=12 age=11,12,41400000              c1=60 st=2 k=12 age=11,12,41400000
f372   c1=60 st=2 k=13 age=12,13,41500000              c1=60 st=2 k=13 age=12,13,41500000
f373   c1=60 st=2 k=14 age=13,14,41600000              c1=59 st=1 k=14 age=13,14,41600000
f374   c1=59 st=1 k=15 age=14,15,41700000              c1=58 st=1 k=15 age=14,15,41700000
```

Every timer is bit-exact at frame boundaries (age floats `0x41200000`, `0x41300000`,
`0x41400000` = 10.0, 11.0, 12.0 in both runs). Rate pointers read `004d8f58, 004d8f58, 00000000`;
the null one belongs to the second countdown, which this bullet never starts. The only
difference is `st`: 2 → 1 one frame early. The state-2 handler falls through into the state-1
body at `0x416883` on the call that promotes, so the countdown takes its first step on that
frame.

The promotion, at the end of the state-2 handler `0x4167e4`:

```
41684f  cmp  dword [esi+0x13c4], 8
416856  jl   0x41686a
416858  push 0 ; mov ecx,esi ; call 0x416d70    ; did this bullet just hit the player
416861  cmp  eax, 1
416864  je   0x416c40                            ; hit -> done with this bullet
41686a  cmp  dword [esi+0x4d4], 0
416871  je   0x416c40                            ; still 0 -> stay in state 2
416877  mov  eax, 1
41687c  mov  word [esi+0xc0e], ax                ; -> state 1, and fall into the body
```

`k >= 8` holds from well before the window and `0x416d70` is the collision test, so the gate is
`[esi+0x4d4]`.

Motion-state dump for state-2 bullets, frames 369–374, differing dword offsets:

```
  +0x050 +0x054 +0x058 +0x060   195 each   heap pointers (0x12fbc9d0 vs 0x12da39d0) -- two processes
  +0x064 +0x068                 195 each   floats
  +0x520 .. +0x548              195 each   four (x,y) pairs, the draw quad (interpolated; expected)
  +0x4d4                          0        the gate
```

`+0x4d4` reads zero at every frame boundary in both runs for every state-2 bullet. It is
non-zero only within a frame.

Effect of the boundary gate at `0x416877` (whole-log diffs):

```
sub-stepped, with the gate against without:   first difference at frame 378
stock, with against without:                  none, at any frame
```

The gate is inert without sub-stepping and changes the sub-stepped run from 378 on, so some
promotions did happen on minor ticks. It does not touch the divergence at 372. Frames 370–377
are identical with and without it; concluding from those frames that the gate did nothing was
wrong. The build with the gate reported 64 verified signatures, 49 patches and 1149 bytes of
stubs, against 63, 48 and 1115 without.

Gate log, one bullet:

```
off  f=369 major=1 tick=402  gate=0 st=2 k=8 age=9
on   f=369 major=1 tick=2407 gate=0 st=2 k=8 age=8      <- boundary tick, age one behind
on   f=369 major=0 tick=2408 gate=0 st=2 k=8 age=8
on   f=369 major=0 tick=2409 gate=0 st=2 k=8 age=8
on   f=369 major=0 tick=2410 gate=0 st=2 k=8 age=8
on   f=369 major=0 tick=2411 gate=0 st=2 k=8 age=8
on   f=369 major=0 tick=2412 gate=0 st=2 k=8 age=9      <- and catches up on the last sub-tick
```

This is hazard 10 of §8: within the frame, the sub-stepped run's integers lag by one.

The two slots that differ at 372 with everything else equal are consequences, not causes:

```
i=1811  off  s=00000013 st=2 k=5 age=4,5,40a00000     a bullet five frames old
        on   s=00000012 st=0 k=0 age=-1,0,00000000    a brand new one in the same slot
i=1946  off  g=80000000 c1=0 st=1 k=74                delay expired, flag not yet cleared
        on   g=00000008 c1=0 st=1 k=74                delay expired, flag already cleared
```

1946 is a flag cleared a frame early because its countdown ran out a frame early; 1811 is a slot
recycled early. With the widened fingerprint the first reported frame will be earlier than 372.

Status: bullet delays start one frame early under sub-stepping. Ruled out: RNG, enemies, timer
arithmetic, a null rate pointer on the countdowns. Partly addressed: promotions on minor ticks
(the gate). Not identified: what makes `+0x4d4` non-zero on the boundary tick one frame earlier
than stock, and which integer-lag decision feeds it. `+0x4d4` is written through the motion
state; the writer has not been found. The comment above `gate_block(0x416877, ...)` in `th14.c`
still gives the read-after-write explanation as complete; the measurements above show it is not.

## 14. Tests

- `tools/test_th14_stubs.py` (Unicorn; `python tools/test_th14_stubs.py build/tests/th14.exe`):
  - `0x45101a` rewinds the shot cycle by a whole 14 at factors 0.25, 1.0, 1/6 and with
    `g_logical` 0.5; prev stays, the rate pointer is restored, ESP does not leak. A companion
    case calls the game's `timer_rewind` with a rate of 1/6 and asserts it subtracts 14/6 — the
    reason the stub exists.
  - `0x45131b` advances the weapon timer one whole frame on the boundary tick only, with prev
    set to the integer on every tick (run to `0x45136b`, before the stack-cookie check).
  - The whole shot cycle `0x450fb0` with shoot held (`0x4d6a90` = 1, player state `+0x684` = 1,
    `0x450ed0` replaced by `ret 4` and counted), 60 frames: all 15 steps (0..14) fire in steady
    state at 60, 240, 360 and 600 Hz, and the shot count stays within a fifth of the 60 Hz count.
- `tools/test_speed.h`: builds a real speed site and a real `movement_cvttss` site through the
  real installer, loads all eight XMM registers with sentinels, runs the site, checks arithmetic
  and registers. With the XMM save removed it reports
  `FAIL: the permanent store's stub clobbered xmm0 lane 1 (1.500000 -> 0.000000)`. Two bugs it
  pins: the speed stub clobbered xmm0; `movement_cvttss` re-emitted the instruction's memory
  operand with the modrm byte unchanged, so a site whose destination was ECX loaded the velocity
  into xmm1 and truncated whatever was in xmm0. `test.sh` builds the runtime with
  `-msse2 -mfpmath=sse`; without those flags the operation compiles to x87 in the harness and
  the clobber is invisible.
- `test_runner_undescribed()` in `tools/test_runner.h`: every optional address zeroed, both
  sub-step switches on, a boundary tick and a minor tick; asserts the catch-up tick declines with
  its five addresses zeroed and that a described game still has it. Removing a guard page-faults
  the harness. It also caught the interpolation pass reading `addr.anm_manager` unchecked. The
  `limiter_stats` guard test needs the fixture's replay manager non-NULL and a window longer than the
  five-second stats interval, or the line is never reached and the test passes without the fix.
- Inline-asm constraints are not trusted for convention bridges and SSE stubs: the compiled
  runtime is disassembled (`anm_get_vm` both forms, the two replay wrappers).

## 15. Open items

- **English and Steam builds.** Only the Japanese `th14.exe` is verified. `th14_signatures.h`
  expects the English build to match unchanged, as TH13's does (same code with an appended
  section); unchecked.
- **Sub-tick input is not wired.** `poll_input` `0x41e710` and `game_input` `0x4d6a90` are read
  (§4) but not in the profile, so the player samples input once per 60 Hz frame and the menu
  reports sub-tick input unavailable. It must keep the three-word, six-byte replay stream.
- **Bullet delay one frame early under sub-stepping** (§13): cause not identified; find the
  writer of bullet `+0x4d4`. The practical check is a whole-length fingerprint match between two
  playbacks of one file.
- **Other intra-frame readers.** How many other places read on a sub-tick something another
  system writes later in the frame, or an integer that lags (§8, hazards 10 and 11), is unknown.
  Reading cannot find them; the two-playback fingerprint can, for the paths a replay exercises.
- **Damage parity** unproven (§13).
- **Pause test**: supervisor flags at `0x4db558 + 0x80` not read; the runtime's test uses
  TH10–13's `0x70`.
- **`pp`** not described: the swap chain is not reset with scaling off.
- **`sprite_round_sites`** for internal resolution.
- **`vpatch_th14.dll`** conflict sites; thprac coexistence unverified against a running game.
- **`USER` chunk tolerance** of TH14's loader unverified (§10).
- **Remaining fifteen `MODE_FRAME` systems**: each needs its own counters found and hooked
  before it can be sub-stepped. Which sprite pass is world and which is UI is not established.
  `vm_script_off` unknown. What draws `bullet.anm` on layers 20 and 21 is unknown.
- **Stale source comments**: the file header and the dimming comment in `th14.c` (class table
  and items "not described"), the `draw` comment (`world_prio` "left at zero"), the replay
  comment ("not installed yet"), the duplicate "not yet written" signature group in
  `th14_signatures.h`, and the TH14 note in `src/identity.h` (magic "not asserted").
