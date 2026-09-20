# TH10 (Mountain of Faith) developer notes

Shared design: [ARCHITECTURE.md](../../ARCHITECTURE.md). Runtime-side findings from the TH10
work (speed model, verification, rig traps): [DEVNOTES_RUNTIME.md](../DEVNOTES_RUNTIME.md).
The source of truth for every number is `src/games/th10.c` and `src/games/th10_signatures.h`;
where this file disagrees, the code is right.

Supported: **TH10 v1.00a**, Japanese `th10.exe` and English `th10e.exe`. Both are the same
code (image size `0x9c000`); every address below holds for both and every frozen signature is
byte-identical in both. Supported since v0.3.0-test (September 2026). The profile started as an
unvalidated commit (`66ade7f`, written by ChatGPT) and was validated, fixed and finished
afterwards.

Identity: magic `t10r`, legacy INI `th10_hfr.ini`, image size `0x9c000`, 82 frozen signatures
(`tools/th10_signatures.json` is the reviewable copy of `src/games/th10_signatures.h`).

## 1. Differences from the TH11/TH12 engine

Each difference is a profile field or a piece of `th10_install_sites`; the shared runner,
scheduler, replay and video code have no TH10 fork. Addresses are in §2, hooks in §5.

| Difference | Absorbed by |
| --- | --- |
| No single game-speed float: per-object `Timer`s carry a pointer to the shared speed at `+0x0c`, and the engine writes the speed with 10-byte sequences instead of TH11/TH12's 6-byte `fstp [speed]` | `SpeedSite {addr, len, op, pop_float}` table with per-site length; `pop_float=0` for sites that leave no value on the x87 stack (§3) |
| Runner `0x449c00` is `stdcall(runner)`, not "object in EBX" | `runner_arg = RUNNER_ARG_STACK` (`hfr_runner_stack_entry`) |
| Critical section entered unconditionally (`push 0x492274; call EnterCriticalSection`, no flag test) | `critical_flag_mask = 0` means "always lock" |
| Input words are `uint16`, focus is bit 4, input block `0x474e30` is `0x6a` bytes | `layout.input_width = 2`, `layout.focus_mask = 4`, `layout.input_size = 0x6a` |
| Frame limiter is inside the frame function | Two 6-byte waits NOPed; the shared frame hook paces |
| One frame call site, not three | `frame_calls = {0x438d31}` |
| Replay save is `fastcall(manager, filename, name)`; load takes the manager in ESI and the filename on the stack (`ret 4`) | `th10_replay_save` / `th10_replay_load_entry` adapters |
| Movement is 16.16 fixed point, `ftol(vel * speed)` | shared `movement_ftol` residual carry (the helper TH11/TH12 use) |
| Cartesian integration is a shared helper (`0x44c2aa`, player shots and hitboxes) | 25-byte scale stub (the equivalent of TH12's `MotionState::step` hook) plus a centipixel rounding bypass |
| Bomb / pressed / released edges re-trigger on minor ticks | `mask_minor_player_edges = 1` |
| Ships `d3dx9_31.dll` (2006), whose HLSL compiler rejects an early return inside an `if` and cannot build MMPX or Super-xBR | `d3dx = "d3dx9_31.dll"` for the texture hooks; the filter compiler is chosen by probing every `d3dx9_NN` for the features the filters use, not by version |
| Window dialog offers only 640x480 | `window_scale` setting and the F10 size cycle (§6) |
| Stage renderer leaves `D3DTSS_TEXTURETRANSFORMFLAGS` set on stage 0 | The scaler's shared state reset clears texture transform and `TEXCOORDINDEX` on stages 0–1 before every draw of ours (§6) |

## 2. Engine map

Absolute addresses for v1.00a. Fields are named as in `GameProfile` (`src/game_profile.h`).

| Symbol | TH10 |
| --- | --- |
| Game speed (shared float the timers point at) | `0x476f78` |
| Update runner pointer | `0x491be4`; ending flag at the default `+0x48`, so `runner_ending` is unset |
| Runner function | `0x449c00`, `stdcall(runner)`; its `ret 4` at `0x449d0e` is `runner_ret` (see DEVNOTES_RUNTIME) |
| Remove update node | `0x449f60` |
| Runner critical section / depth counter | `0x492274` / `0x49231c`, always entered |
| `misc_flags` | `0x491ff4` (kept for the shared code paths; not a lock gate here) |
| Vsync frame function / frame call site | `0x439390` / `0x438d31` |
| Frame context pointer / flag / value | `0x491fac` / `0x491fb0` / `0x491e94` |
| Frame duration | `0x4923a0` |
| Scene cleanup function / object | `0x44c150` / `0x492254` |
| D3D device / present parameters | `0x491c30` / `0x491d0c` |
| Raw input block / pressed | `0x474e30` / `0x474e36` (16-bit words, `0x6a` bytes saved around a sub-tick poll) |
| Raw input poll | `0x44a5f0` |
| Game input / pressed / released | `0x474e5c` / `0x474e62` / `0x474e64` |
| Autofocus counter / option flags | `0x474e5a` / `0x491d78` |
| GameManager pointer / callback | `0x477810` / `0x4187c0`; pause flags at `+0x58` |
| Player pointer / callback | `0x477834` / `0x426500`; state timer `prev/int/float` at `+0x474/+0x478/+0x47c` |
| EnemyManager pointer | `0x477704` (list head at the default `+0x68`) |
| AnmManager pointer / get-VM-by-id | `0x491c10` / `0x4491c0` |
| ReplayManager pointer | `0x477838`; mode `+0x10`, stage pointers `+0x1c + 4*s`, stage frame `+0x1c8`, stage number `+0x1d0` |
| Replay record and playback node | `0x42a3d0` (one callback serves both modes) |
| Replay save (native) | `0x429b60`, `fastcall(manager, filename, name)` |
| Replay load (native) | `0x42a200`, ESI = manager, filename on the stack, `ret 4` |
| Replay save call sites | `0x423f16`, `0x43399d` |
| Replay load call sites | `0x429257`, `0x42948c` (play, modes 1 and 2; hooked); `0x429765` (menu header peek; not hooked, §5) |
| Screenshot routine / call site | `0x420670` / `0x4392c1` |
| ftol | `0x463b2c` |
| Frame limiter waits (NOPed) | `0x4393b7`, `0x439488` (6 bytes each) |
| FPS-counter watchdog branch (patched) | `0x413508` (§6b) |

### vpatch conflict sites (`src/games/th10_conflicts.h`)

Found as TH11's were: every 32-bit immediate in `vpatch_th10.dll` that lands in the game's
code, kept when the code there has the right shape. `0x439397` (frame limiter) and `0x4134b8`
(replay timing) both call `0x439540`, the same one-timing-routine pattern as TH11's `0x446920`
and TH12's `0x4508b0`. Three sites load a device pointer for a Present call (`0x4399e9`,
`0x439a27`, `0x438d1d`). Which one vpatch takes is not known, so all three are listed: a site
vpatch leaves alone never trips, and the loaded-module check catches vpatch anyway.

## 3. Speed sites

TH10 has no `fstp [speed]` pattern; each write has its own length. `pop_float` is 1 only for
the ECL site, which leaves the new speed on the x87 stack.

| Site | Length | Operation |
| --- | --- | --- |
| `0x4178b1`, `0x417ca9`, `0x4201b7`, `0x425cc4` | 10 | `SPEED_ONE_PERM` (stage start, game start, supervisor reset, player death) |
| `0x4027ea`, `0x43ee84` | 10 | `SPEED_ONE_TEMP` (stage 3D update; "unaffected by slow-motion" sprites) |
| `0x422c20`, `0x42335f`, `0x4234ff` | 10 | `SPEED_PAUSE_SET` |
| `0x422c73` | 6 | `SPEED_PAUSE_RESTORE` |
| `0x423563` | 5 | `SPEED_PAUSE_RESTORE` |
| `0x411c3d` | 6 | `SPEED_ECL` (the ECL instruction that sets the game speed), `pop_float = 1` |

`install_speed_sites` (`src/core/install.c`) builds one trampoline per `(op, pop_float)` pair:
`fstp [shadow]` when a float is pending, then `pushf; pusha; call op; popa; popf; ret`. Each
site is patched with a call of the site's own length. The stubs this emitter produces for
TH11/TH12 were byte-compared against the earlier fixed-length emitter's and are identical.

## 4. Node classification

| Callback | Mode | Name |
| --- | --- | --- |
| `0x406770` | SUB | BulletManager |
| `0x426500` | SUB | Player |
| `0x405840` | FRAME | Bomb |
| `0x41ba00` | SUB | ItemManager |
| `0x41c480` | FRAME | LaserManager |
| `0x415ae0` | FRAME | Gui |
| `0x403050` | FRAME | Stage |
| `0x4485d0` | SUB | AnmManagerWorld |
| `0x4485e0` | SUB | AnmManagerUI |
| `0x40b050` | FRAME | Spellcard |
| `0x40d810` | FRAME | EnemyManager |
| `0x4187c0` | FRAME | GameManager |

LaserManager and Stage are FRAME in TH10 (they are sub-stepped in TH11 and TH12) because their
per-object gating hooks are not ported. Promoting either requires auditing its per-frame
counters first; the bug list in [TH12_DEVNOTES.md](TH12_DEVNOTES.md) says what to look for.

## 5. Per-object hooks (`th10_install_sites`)

- **Frame limiter waits** at `0x4393b7` / `0x439488` become NOPs. The shared frame hook paces
  and presents every scheduled slot; the game's own wait would fight it.
- **FPS-counter watchdog** at `0x413508`: `75 5b` becomes `eb 5b` (§6b).
- **Replay adapters.** `th10_replay_save` calls the native save and appends the HFR chunk.
  `th10_replay_load_entry` converts TH10's ESI/stack ABI into the shared stdcall hook
  (`push [esp+4]; push esi; call th10_replay_load_c@8; ret 4`). Hooked: save sites `0x423f16`,
  `0x43399d`; load sites `0x429257`, `0x42948c`. **`0x429765` must stay unhooked**: it is the
  menu reading a file's header into a throwaway manager (it writes 2 into `[+0x10]` first), not
  a replay starting. Hooked, it reads simulation metadata off every replay on disk when the list
  is built and raises a message box for any file the runtime does not recognise.
- **Integer counters gated on the object's own timer** (`gate_block`): `0x406584` (bullet,
  timer at `EBP+0x3f8`), `0x425aa9` and `0x426285` (player, timer at `EBP+0x474`). A gated
  block runs only on the tick where the object's integer timer changed: once per frame, at the
  object's own phase.
- **Frame-boundary gates** (`gate_block` with `timer = -1`): `0x425520` and `0x4251c1`, the
  option history, easing and callbacks. They are measured in original frames and have no timer
  of their own.
- **Movement residual**: `movement_ftol(0x42540a / 0x42541f, ftol 0x463b2c)` carries the
  truncation of `ftol(vel * speed)` across sub-steps, so a sub-divided displacement does not
  lose up to one unit per tick.
- **Item homing acceleration** (`0x41b28d`, `0x41b315`): the constant at `0x470c38` is a rate,
  so it is multiplied by the factor.
- **Cartesian integration** (`0x44c2aa`, 25 bytes, `esi+0xc..0x14` velocity onto `esi+0..8`):
  scaled by the factor, as TH12's `MotionState::step`. **Centipixel rounding** at `0x44c30f`
  is skipped when the factor is not 1; rounding every sub-step would bias every subdivided
  displacement.
- **Shot acceleration and turn** (`0x425dab` / `0x4283a7` and `0x425db5` / `0x4283b1`) and the
  two-axis `pos += vel` at `0x425dce` (16 bytes): rates, scaled.
- **Enemy hit-test guard** at `0x42863e` (`cmp eax,[ebp+0x474]; jne`): replaced by the runner's
  `g_ptf_prev != g_ptf_cur` comparison. The enemy hit test (`0x428630`) opens with the stock
  double-hit guard, "player state timer unchanged since last frame → no damage". Sub-stepped,
  the integer timer advances on the last minor tick of a frame, so on the boundary tick (the
  only tick the 60 Hz enemy code runs the test) it always reads unchanged and no player shot
  ever hits. The replacement tests "the float timer advanced across the last Player update".
  The same six-byte `cmp` exists in every game: `0x42863e` TH10, `0x434814` TH11, `0x439ef2`
  TH12, `0x446888` TH13. Port it first in any new game.
- **Constant `Timer::add` sites** `0x428243` (shot cycle timer) and `0x440e3d` (ANM wait):
  the argument is a script constant in frames, so the stub adds `value * logical` (stock
  semantics) instead of `value * logical * dt`.

Enemy interpolation is not wired (`place_enemy` is NULL, which disables it safely). TH10's
enemy layout is not mapped. [TH11_DEVNOTES.md](TH11_DEVNOTES.md) (enemies) says what to look
for, and the object layouts in [TH13_DEVNOTES.md](TH13_DEVNOTES.md) show how the ECL variable
getter gives the layout in one table.

## 6. Traps

- **A fault at `0x42b1e0` is a broken rig, not the patch.** TH10 aborts partway through init
  when a data file is short (a truncated `th10e.dat`; a sparse `thbgm.dat` is tolerated) and
  then dereferences a null in its own cleanup. Vanilla does the same. The exception handler's
  registers-and-stack backtrace shows it: the faulting frame's return address is in the game's
  data-load cleanup, not in any stub.
- **Scaling and filters do nothing at 640x480**, which is the only size TH10's dialog offers:
  every mode and filter is a 1:1 picture at the game's own size. `window_scale` (percent of the
  game's size, `-1` = largest whole multiple that fits the screen) sizes the client area at
  startup and from the menu's Display tab.
- **The overlay menu does not draw during gameplay unless texture transforms are reset** (it
  draws at the title screen). The stage renderer leaves `D3DTSS_TEXTURETRANSFORMFLAGS` enabled
  on stage 0 for its scrolling clouds, so the overlay's UVs go through the game's texture matrix
  and sample nothing. The scaler's shared state reset (`quad_states`), which every draw of ours
  runs after, clears it. Found by bisecting a run-time mask of state resets.
- **F10 does nothing in the stock game.** TH10 has no size-cycle handler (nor has any other
  supported game: `native_size_cycle = 0` in every profile). For a profile without
  `native_size_cycle` the runtime provides the cycle: the next preset larger than the current
  window, skipping ones the screen cannot hold (640x480 → 960x720 → 1280x960), then a
  patch-driven borderless fullscreen, then back to 1x. The step becomes `window_scale`, so Save
  keeps it. F10 is a system key: left to `DefWindowProc` it puts the window into keyboard-menu
  mode, which eats the next key and stalls the game, so `WM_SYSKEYDOWN/UP` for the configured
  `size_cycle_key` stop at our window procedure.
- **Positional tables shift when a game is inserted.** Inserting TH10 at the front of the
  identity table repointed TH11's and TH12's profiles at their neighbours' identities, and the
  bare config-defaults list shifted every default after an inserted field. Both use names now
  (designated initialisers, `GI_*` enum slots).

## 6a. Draw list, sprite VMs and dimming data

Mechanism: DEVNOTES_RUNTIME (dimming). Rules: `th10_dim_rules` in `src/games/th10.c`.

| Item | Value |
| --- | --- |
| Draw runner / dispatch | `0x449d40` / `0x449da3` (`mov ecx,[esi+0x20]; call [esi+8]`, six bytes) |
| Sprite batch flush | `0x442f50` (ESI = AnmManager, pointer at `0x491c10`, pending count at manager+0x3adac8) |
| Sprite VM draw | `0x4451c0` (VM in EAX, 0x3ac bytes) |
| VM fields | loaded-ANM pointer +0x308, layer +0x20, script index +0x38a (16-bit) |
| ANM slots seen | 3 capture, 5 the stage, 6 front, 7 bullet, 8 the player, 9 enemy |

TH10 draws straight into the back buffer; there is no offscreen stage. Draw priorities
(`debug=1` trace):

1/2 `0x420000`/`0x41fef0`, 4 GameManager, 5 `0x42a430`, 7 Stage 3D (`0x403060`), 9 layer 0,
10 Stage 2D (`0x403070`: the clouds, and the stage-enemy ANM's 3D-mode sprites), **11 layer 1
(`world_prio`)** with the dim quad drawn before it over the playfield viewport, 13 layer 2
(bullet.anm's layer-2 effects), 14 `0x409230`, 15..19 layers 3..7, 20 EnemyManager
(`0x40d820`), 21 layer 8 (the player's shots, pl0X.anm layer 8), 22 Player body, 23/24 layers
9/10, **25 ItemManager `0x41ba30`**, 26 layer 11, 27 LaserManager, 28 layer 12, 29
BulletManager, 32 Spellcard, 33 layer 13, 34 Bomb, 35 `0x42b9b0`, 36 layer 14, 37 `0x409270`,
38/48 `0x401520`/`0x401510` (the Effects object; 48 draws a constant nine quads of text),
40..47 interface.

- **bullet.anm**: items on layer 7. Bullets are drawn by the BulletManager (29) with their VMs'
  layer left at 0. Layer 13 (priority 33) holds the enemy death bursts (scripts 351-442, the
  ones that set `ins_68(13)` themselves) and other effects. In `th10_dim_rules`, scripts
  351-352 on layer 13 are the player's hitbox (two sprites turning about the player while
  focused) and are never dimmed; the rest of the file bar its item layer falls through to the
  effects class.
- **enemy.anm**: layer 4 (the lowest; 16 one-sprite additive scripts, row 224 of enemy.png) is
  spawn-in flashes and auras under the enemies on layer 5. They look like death effects. They
  are not: deaths are the bullet.anm layer-13 bursts. TH11 (enemy.anm 6 of 7/8, bullet.anm 15)
  and TH12 (7 of 8/9, 16) have the same shape.
- **Spell-card backgrounds** (`cdbg0Xa/b.png`, scripts with no layer) are not drawn by the
  sprite layers. `0x409230`, the callback at priority 14, draws them itself (a playfield-sized
  quad and a rotating second one) after the dim quad. The rule
  `{14,14,"stgenm*.anm",-1,-1, DIM_BACKGROUND}` fades their colour towards black instead.
  Layers 4-5 of `stgenm02.anm` look like the backgrounds. They are not: they are Hina's
  spinning body, and dimming them dims the boss. Layer 3 is the portrait cut-in, 14 the boss
  name.
- **Hitches.** `hitch:` lines in the log record any gap between presents over 40 ms with what
  that frame did (draw calls, forced batch flushes, sprite VM draws, textures upscaled), for
  stutter reports.

## 6b. FPS-counter clock watchdog (`0x413508`)

Unpatched symptom: recurring hitches and the FPS counter briefly reading zero whenever
presentation exceeds 65 FPS, even with 60 Hz simulation or a stock replay.

The native FPS counter `0x4134b0` samples the draw count every half second. A reading above
**65 FPS** increments `FpsCounter+0x1c`. On the second consecutive high reading it rebases the
native clock; on the fourth it zeros the QPC frequency at `0x492508/0x49250c`, and the native
clock (`0x439540`) falls back to `timeGetTime`. `timeGetTime` measures machine uptime while
the QPC path returns time relative to the game's origin, so the clock jumps forwards and
backwards, and the FPS measurement can round to `0.0fps`.

NOPing the early-return branch at `0x4393b7` does **not** remove the native deadline catch-up
loop at `0x4393d0`, which adds `1/60` second until its deadline catches up with the clock. A
one-hour jump takes 216,000 iterations before any update or draw; larger uptimes take more. In
diagnostic logs this is a long span before drawing with no long update callback or Present.

TH13's FPS update at `0x424990` also counts high readings but has neither the rebasing nor the
QPC-disable action; its draw callback at `0x424a90` only displays the result.

**Fix:** `75 5b` (`jne 0x413565`) at `0x413508` becomes `eb 5b` (`jmp`), always taking the
branch that clears the watchdog count (EDI is already zero there). The computed FPS, its
display and the slowdown accounting are unchanged. The two original bytes are frozen in both
signature tables and validated in the normal patch transaction. The patch is unconditional
(also with `substep=0` and during stock replays) because presentation can exceed 65 FPS
independently of the logic rate. No native clock replacement is needed. Log line:
`TH10 site patches installed (%u bytes of stubs); >65 FPS clock-reset watchdog disabled`.

**Regression test:** `tools/test_th10_stubs.py` runs the installed game-code fixture in
Unicorn with deterministic Win32 clock imports; its negative control restores the original
branch.

- Control, 360 FPS over eight simulated seconds with one hour of synthetic uptime: QPC is
  disabled, the clock jumps by 3599 seconds and moves backwards three times, and the counter
  reports 0.0 FPS for 540 frames.
- Patched: QPC stays enabled and time is monotonic at 60, 64, 65, 66, 120, 144, 240, 360 and
  1000 FPS. The 60 FPS result, including slowdown accounting, is identical to the original.
- The retained deadline loop is executed too: 216,000 iterations for the one-hour jump.
- Japanese and English fixtures pass; the shared native harness passes for TH11, TH12 and
  TH13, as do the TH11/TH12 machine-code tests.

**Native timing:** the original 25-byte deadline loop, copied into a native x86 benchmark with
its constant address relocated, stalls 28.27–28.37 ms on the owner's machine at 64.677 hours
of uptime (about 13,970,321 iterations). The ordinary 1/360-second increment takes one
iteration. The pre-fix diagnostic log shows recurring 30.1–30.8 ms frames before drawing, with
no slow update callback, sound update or Present.

**Live confirmation:** with the fix installed as `dinput8.dll` in a clean TH10 folder, the
owner tested at 360 Hz: **the hitches and the zero-FPS readings were both gone**. The previous
DLL and log are in that folder's `hfr-backups/20260912-030752-before-clock-fix/`; executable
and INI hashes were verified unchanged. Benchmark source, results and regression logs are
under ignored `build/tests/`.

## 7. Harness and rig

- The harness validates a provisional profile's patch plan when `HFR_VALIDATE_PROVISIONAL` is
  set (`g_validate_provisional`); TH10 came off provisional this way. The generalised runner
  enters the critical section whenever `critical_flag_mask` is 0, so a fixture whose section is
  zeroed memory hangs `test_runner`. The fixture initialises one and clears it afterwards.
- Wine: TH10's loader blocks on its sound queue when there is no audio device at all. A null
  ALSA device (`~/.asoundrc` with `pcm.!default { type null }` and
  `HKCU\Software\Wine\Drivers Audio=alsa`) is enough. A sparse `thbgm.dat` is fine; a truncated
  `th10e.dat` is not (§6). The English executable needs `th10e.dat`.
- `WINEDLLOVERRIDES="dinput8=n,b;d3dx9_31=n;d3dx9_40=n"`: Wine's own `d3dx9` substitutes an
  incomplete HLSL compiler and produces misleading shader failures.
- TH10's own window dialog is the only way to get a window under the rig; the picture features
  need `window_scale` or F10 to have anything to scale.

## 8. Validation and open items

Under Wine at `fps=240`: boots, plays stage 1 at a 240 Hz tick rate with 0 repeated frames,
records and plays back HFR replays with per-tick input reproduced (recorded rate read back as
240, `subtick applied > 0`), borderless window, resizing and every filter.

On the owner's machine (real 240 Hz display, PivotDX9 renamed away): high frame rate
confirmed; window scaling, F10 and enemy hits confirmed after the fixes in §5 and §6 ("TH10
looks perfect now" after v0.3.0-test); the clock watchdog fix confirmed at 360 Hz (§6b).

Not yet exercised: full runs with every shot type, stage transitions beyond stage 1 under the
rig, exclusive fullscreen, and the LaserManager/Stage sub-stepping promotion. Enemy
interpolation is not wired (§5).
