# TH10 (Mountain of Faith) port development notes

> Per-game record for the unified runtime. The shared design is in
> [ARCHITECTURE.md](ARCHITECTURE.md); what the runtime learned while taking on TH10 is in
> [DEVNOTES_RUNTIME.md](DEVNOTES_RUNTIME.md) §4, §7 and §8. This file is the place to look for
> *TH10 addresses, layouts, decisions and their reasons*. The source of truth for every number
> is `src/games/th10.c` and `src/games/th10_signatures.h`; if they disagree with this file, the
> code is right and this file needs fixing.

Supported: **TH10 v1.00a**, English `th10.exe` and Japanese `th10j.exe`. Both are the same
code (image size `0x9c000`); every address below holds for both, and every frozen signature is
byte-identical in both. Build state: **supported** since v0.3.0-test (September 2026). The
profile was started by ChatGPT (commit `66ade7f`, unvalidated), then validated, fixed and
finished here.

## 1. How TH10 differs from the TH11/TH12 engine, and what absorbed each difference

TH10 is the oldest engine the runtime supports and the one that stretched the multi-game
design the most. Each difference became a profile field or a per-game piece of
`install_sites`; none became a fork of the shared runner, scheduler, replay or video code.

| Difference | Where it shows | Absorbed by |
| --- | --- | --- |
| No single game-speed float | Per-object `Timer`s carry a pointer to the shared speed at `+0x0c` and the engine writes the speed through that pointer, as 10-byte sequences instead of TH11/TH12's 6-byte `fstp [speed]` | `SpeedSite {addr, len, op, pop_float}` table with per-site length; `pop_float=0` for the sites that do not leave a value on the x87 stack |
| Runner takes its object on the stack | `0x449c00` is `stdcall(runner)`, not "object in EBX" | `runner_stack_arg = 1` (`hfr_runner_stack_entry`) |
| Critical section entered unconditionally | `push 0x492274; call EnterCriticalSection` with no flag test | `critical_flag_mask = 0` means "always lock" (and the harness fixture initialises a section, see §7) |
| 16-bit input words, focus bit 4 | Input block `0x474e30` size `0x6a`, words are `uint16` | `layout.input_width = 2`, `layout.focus_mask = 4`, `layout.input_size = 0x6a` |
| Frame limiter inside the frame function | Two 6-byte sleeps/waits at `0x4393b7` and `0x439488` | NOPed by `install_sites`; the shared frame hook owns pacing and presents every scheduled slot |
| One frame call site, not three | `0x438d31` | `frame_calls = {0x438d31}` |
| Replay ABI differs | Save is `fastcall(manager, filename, name)` at `0x429b60`; load is `0x42a200` with the manager in ESI and the filename on the stack (`ret 4`) | `th10_replay_save` / `th10_replay_load_entry` adapters; three load call sites, two save call sites |
| Movement is 16.16 fixed point | `ftol(vel * speed)` at `0x42540a` / `0x42541f` via `0x463b2c` | shared `movement_ftol` residual carry (the same helper TH11/TH12 use) |
| Cartesian integration is a shared helper | `0x44c2aa`, used by player shots and hitboxes | 25-byte scale stub (the TH10 equivalent of TH12's `MotionState::step` hook) plus a centipixel rounding bypass at `0x44c30f` when the factor is not 1 |
| Player edge input must stay on the frame | Bomb / pressed / released edges on minor ticks re-trigger | `mask_minor_player_edges = 1` (TH11/TH12 do not need it) |
| Old D3DX | Ships `d3dx9_31.dll` (2006), whose HLSL compiler rejects an early return inside an `if` and cannot build MMPX or Super-xBR | `d3dx = "d3dx9_31.dll"` for the texture hooks; the filter compiler is chosen by probing every `d3dx9_NN` for the features the filters use, not by version |
| Window dialog offers only 640x480 | At the game's own size every scaling mode and filter is a 1:1 no-op | `window_scale` setting and the F10 size cycle (§5) |
| Stage renderer leaves a texture transform on | `D3DTSS_TEXTURETRANSFORMFLAGS` stays set on stage 0 after the scrolling clouds | The scaler's shared state reset clears texture transform and `TEXCOORDINDEX` on stages 0–1 before every draw of ours |

## 2. Engine map

All addresses are absolute for v1.00a. Fields named as in `GameProfile` (`src/game_profile.h`).

| Symbol | TH10 |
| --- | --- |
| Game speed (shared float the timers point at) | `0x476f78` |
| Update runner pointer | `0x491be4`; ending flag at the default `+0x48` (TH10's runner ends there, hence `runner_ending` unset) |
| Runner function | `0x449c00`, `stdcall(runner)` |
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
| Replay load call sites | `0x429257`, `0x42948c`, `0x429765` |
| Screenshot routine / call site | `0x420670` / `0x4392c1` |
| ftol | `0x463b2c` |
| Frame limiter waits (NOPed) | `0x4393b7`, `0x439488` (6 bytes each) |

Identity: magic `t10r`, legacy INI `th10_hfr.ini`, image size `0x9c000`, 80 frozen
signatures (`tools/th10_signatures.json` is the reviewable copy of `src/games/th10_signatures.h`).

### vpatch conflict sites (`src/games/th10_conflicts.h`)

Found the way TH11's were: every 32-bit immediate in `vpatch_th10.dll` that lands in the game's
code, kept when the code there has the right shape. `0x439397` (frame limiter) and `0x4134b8`
(replay timing) both call `0x439540`, the same one-timing-routine pattern as TH11's `0x446920`
and TH12's `0x4508b0`. Three sites load a device pointer for a Present call (`0x4399e9`,
`0x439a27`, `0x438d1d`); which one vpatch takes is not certain, so all three are listed — a
site vpatch leaves alone never trips, and the loaded-module check catches vpatch anyway.

## 3. Speed sites

TH10's speed writes are `SpeedSite` entries with explicit lengths. `pop_float` is 1 only for
the ECL site, which leaves the new speed on the x87 stack.

| Site | Length | Operation |
| --- | --- | --- |
| `0x4178b1`, `0x417ca9`, `0x4201b7`, `0x425cc4` | 10 | `SPEED_ONE_PERM` (stage start, game start, supervisor reset, player death) |
| `0x4027ea`, `0x43ee84` | 10 | `SPEED_ONE_TEMP` (stage 3D update; "unaffected by slow-motion" sprites) |
| `0x422c20`, `0x42335f`, `0x4234ff` | 10 | `SPEED_PAUSE_SET` |
| `0x422c73` | 6 | `SPEED_PAUSE_RESTORE` |
| `0x423563` | 5 | `SPEED_PAUSE_RESTORE` |
| `0x411c3d` | 6 | `SPEED_ECL` (the ECL instruction that sets the game speed), `pop_float = 1` |

The generalised emitter (`install_speed_sites` in `src/core/install.c`) builds one trampoline
per `(op, pop_float)` pair — `fstp [shadow]` when a float is pending, then
`pushf; pusha; call op; popa; popf; ret` — and patches each site with a call of the site's own
length. When the emitter was generalised from TH11/TH12's fixed-length form, the emitted stubs
were byte-compared before and after and were identical.

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

LaserManager and Stage run at frame rate in TH10 (they are sub-stepped in TH12): their
per-object gating hooks were not ported, so the safe classification is FRAME. Promoting either
means auditing its per-frame counters first (§6 of the TH12 notes says what to look for).

## 5. Per-object hooks (`th10_install_sites`)

Read with `src/games/th10.c` open; this is the *why* for each group.

- **Frame limiter waits** at `0x4393b7` / `0x439488` become NOPs. The shared frame hook paces
  and presents every scheduled slot; the game's own wait would fight it.
- **Replay adapters.** `th10_replay_save` calls the native save and appends the HFR chunk;
  `th10_replay_load_entry` re-shuffles TH10's ESI/stack ABI into the shared stdcall hook
  (`push [esp+4]; push esi; call th10_replay_load_c@8; ret 4`).
- **Integer counters gated on the object's own timer** (`gate_block`): `0x406584` (bullet,
  timer at `EBP+0x3f8`), `0x425aa9` and `0x426285` (player, timer at `EBP+0x474`). A gated
  block runs only on the tick where the object's integer timer changed, which by construction
  is once per frame and at the object's own phase.
- **Frame-boundary gates** (`gate_block` with `timer = -1`): `0x425520` and `0x4251c1`, the
  option history, easing and callbacks, which are measured in original frames and have no timer
  of their own.
- **Movement residual**: `movement_ftol(0x42540a / 0x42541f, ftol 0x463b2c)` carries the
  truncation of `ftol(vel * speed)` across sub-steps so a sub-divided displacement does not lose
  up to one unit per tick.
- **Item homing acceleration** (`0x41b28d`, `0x41b315`): the constant at `0x470c38` is a rate,
  so it is multiplied by the factor.
- **Cartesian integration** (`0x44c2aa`, 25 bytes, `esi+0xc..0x14` velocity onto `esi+0..8`):
  scaled by the factor, as TH12's `MotionState::step`. **Centipixel rounding** at `0x44c30f`
  is skipped when the factor is not 1, because rounding every sub-step would bias every
  subdivided displacement.
- **Shot acceleration and turn** (`0x425dab` / `0x4283a7` and `0x425db5` / `0x4283b1`) and the
  two-axis `pos += vel` at `0x425dce` (16 bytes): rates, scaled.
- **Enemy hit-test guard** at `0x42863e` (`cmp eax,[ebp+0x474]; jne`): replaced by the runner's
  `g_ptf_prev != g_ptf_cur` comparison. See §6 for the story.
- **Constant `Timer::add` sites** `0x428243` (shot cycle timer) and `0x440e3d` (ANM wait):
  the argument is a script constant in frames, so the stub adds `value * logical` (stock
  semantics) instead of `value * logical * dt`.

Enemy interpolation is not wired for TH10 (`place_enemy` is NULL, which disables it safely).
TH10's enemy layout was not mapped; TH11's notes (§"Enemies") say what to look for, and TH13's
§5 shows how the ECL variable getter gives the layout in one table.

## 6. Bugs met, in the order they were found

**The `0x42b1e0` fault that was never the patch.** The first session chased a crash at
`0x42b1e0` and suspected the device redirect. The rig had a truncated `th10e.dat` and a sparse
`thbgm.dat`; TH10 aborts partway through init when a data file is short and then dereferences a
null in its own cleanup — vanilla does the same. With the real data files it boots. The
registers-and-stack backtrace the exception handler logs since then is what made this obvious
the second time: the faulting frame's return address was in the game's data-load cleanup, not in
any stub. Confirm the rig before blaming the patch.

**"None of the scaling or upscaling works."** Everything installed and nothing changed on
screen, because the window was 640x480 — TH10's dialog offers nothing larger — and at the
game's own size every mode and filter is a 1:1 picture. `window_scale` (percent of the game's
size, `-1` = largest whole multiple that fits the screen) sizes the client area at startup and
from the menu's Display tab; the INI comment says why it exists.

**The menu that did not draw during gameplay** (fine at the title screen, and "open" by every
measure the log had). TH10's stage renderer leaves `D3DTSS_TEXTURETRANSFORMFLAGS` enabled on
stage 0 for its scrolling clouds; the overlay's UVs went through the game's texture matrix and
sampled nothing. Found by bisecting a run-time mask of state resets. Fixed in the scaler's
shared state reset (`quad_states`), which every draw of ours runs after.

**"F10 does nothing."** TH11 and later cycle 640x480 → 960x720 → 1280x960 → fullscreen on F10
themselves; TH10 has no handler at all. The runtime now provides the cycle for any profile
without `native_size_cycle` (next preset larger than the current window, skipping ones the
screen cannot hold, then a patch-driven borderless fullscreen, then back to 1x), and the step
becomes `window_scale` so Save keeps it. F10 is a system key: left to `DefWindowProc` it puts
the window into keyboard-menu mode, which eats the next key and stalls the game, so
`WM_SYSKEYDOWN/UP` for the configured `size_cycle_key` stop at our window procedure.

**"Player bullets do not collide with enemies at all"** (worked around by switching Player
sub-stepping off). The enemy hit test (`0x428630`) opens with the stock double-hit guard,
"player state timer unchanged since last frame → no damage". Sub-stepped, the integer timer
advances on the last minor tick of a frame, so on the boundary tick — the only tick the 60 Hz
enemy code runs the test — it always read unchanged. TH11 and TH12 replace that compare with
"the float timer advanced across the last Player update", which the runner tracks; the TH10 port
had every hook around this one and not it. Same six-byte `cmp` (`0x42863e` here, `0x434814`
TH11, `0x439ef2` TH12, `0x446888` TH13). Look for it first in any new game.

**Two positional lists that shifted underneath.** Inserting TH10 at the front of the identity
table silently repointed TH11's and TH12's profiles at their neighbours' identities, and the
bare config-defaults list shifted every default after an inserted field. Both are named now
(designated initialisers, `GI_*` enum slots).

## 7. Harness and rig notes specific to TH10

- The harness validates a provisional profile's patch plan when `HFR_VALIDATE_PROVISIONAL` is
  set (`g_validate_provisional`), which is how TH10 came off provisional with evidence rather
  than optimism. Doing that hung `test_runner` at first: the generalised runner enters the
  critical section whenever `critical_flag_mask` is 0, and the fixture's section was zeroed
  memory. The fixture now initialises one and clears it afterwards.
- Wine rig: TH10's loader blocks on its sound queue when there is no audio device at all. A
  null ALSA device (`~/.asoundrc` with `pcm.!default { type null }` and
  `HKCU\Software\Wine\Drivers Audio=alsa`) is enough. A sparse `thbgm.dat` is fine; a truncated
  `th10e.dat` is not (§6). The English executable needs `th10e.dat`.
- `WINEDLLOVERRIDES="dinput8=n,b;d3dx9_31=n;d3dx9_40=n"`: Wine's own `d3dx9` substitutes an
  incomplete HLSL compiler and produces misleading shader failures.
- TH10's own window dialog is the only way to get a window at all under the rig; the picture
  features need `window_scale` or F10 to have anything to scale.

## 8. Live validation record

Under Wine at `fps=240`: boots, plays stage 1 at a 240 Hz tick rate with 0 repeated frames,
records and plays back HFR replays with per-tick input reproduced (recorded rate read back as
240, `subtick applied > 0`), borderless window, resizing and every filter. On the owner's
machine (real 240 Hz display, PivotDX9 renamed away): high frame rate confirmed, then
window scaling, F10 and enemy hits fixed as in §6; "TH10 looks perfect now" after v0.3.0-test.

Not yet exercised: full runs with every shot type, stage transitions beyond stage 1 under the
rig, exclusive fullscreen, and the LaserManager/Stage sub-stepping promotion.
