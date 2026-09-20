# What differs per game

Every game-specific behaviour in the tree, where it lives, and whether it could move into shared
code. Addresses and reasons are in the per-game notes under [games/](games/); this file is the
index. **Update it in the same commit as any change to a profile field, a per-game hook, or a
`g_game->` branch in shared code** — `tools/check_game_differences.py` fails the test suites when
a profile field or a profile is missing from it.

Shared code must not branch on a game's identity. It branches on profile fields
(`src/game_profile.h`), so "what differs" is, by construction, the profiles and the per-game
adapters in `src/games/`.

## 1. Engine and simulation model

| | TH08 | TH10 | TH11 | TH12 | TH13 | TH14 | TH15 | New Classic |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Runtime | x86 | x86 | x86 | x86 | x86 | x86 | x86 | x64 (`hfr64.c`) |
| Graphics | D3D8 → d3d8to9 (`d3d8`) | D3D9 | D3D9 | D3D9 | D3D9 | D3D9 | D3D9 | D3D11 |
| Loader | `dinput8.dll` | `dinput8.dll` | `dinput8.dll` | `dinput8.dll` | `dinput8.dll` | `dinput8.dll` | `dinput8.dll` | `dxgi.dll` |
| `d3dx` | `d3dx8.dll` (static) | `d3dx9_31` | `d3dx9_37` | `d3dx9_40` | `d3dx9_43` | `d3dx9_43` | `d3dx9_43` | — |
| Update structure | Chain of callbacks, walked by `th08_walk` | update runner | update runner | update runner | update runner | update runner | update runner | own lists |
| Simulation rate | 60 Hz fixed | display | display | display | display | display | display | 60 Hz fixed |
| Extra frames come from | prediction + interpolation (`install_presentation`) | sub-stepping | sub-stepping | sub-stepping | sub-stepping | sub-stepping | sub-stepping | interpolation |
| INI section | `[fixed60]` | `[hfr]` | `[hfr]` | `[hfr]` | `[hfr]` | `[hfr]` | `[hfr]` | `[fixed60]` |
| Frame entry | `frame_original`, `update_only` callbacks | `frame_fn` + `frame_calls` | same | same | same | same | same | own clock hooks |

## 2. What runs at the display's rate (`classes`)

| System | TH08 | TH10 | TH11 | TH12 | TH13 | TH14 | TH15 | New Classic |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Player | opt-in (`subtick`) | sub | sub | sub | sub | sub | sub | opt-in (`subtick`) |
| Enemy bullets | opt-in (`substep`) | sub | sub | sub | sub | sub | sub | opt-in (`substep`) |
| Lasers | opt-in (`substep`) | 60 Hz | sub | sub | sub | sub | sub | opt-in (`substep`) |
| Items | opt-in (`substep`) | sub | sub | sub | sub | sub | sub | 60 Hz |
| Stage / background | camera smoothed | 60 Hz | sub | sub | sub | 60 Hz | 60 Hz | interpolated |
| Sprite animation | interpolated quads | sub (World, UI) | sub | sub | sub | sub (Early, Late passes) | sub (Early, Late passes) | interpolated |
| Enemies | predicted quads | 60 Hz, **not interpolated** (`place_enemy` NULL) | interpolated | interpolated | interpolated | interpolated | interpolated | interpolated |
| Player options | predicted quads | 60 Hz | 60 Hz | 60 Hz | 60 Hz | interpolated (`place_options`) | interpolated (`place_options`) | interpolated |
| Sub-tick input | `th08_live_input` | yes | yes | yes | yes | **no** (`poll_input`, `game_input` unset) | **no** (as TH14) | own |
| Opt-in modes turn off during replay playback | yes | — | — | — | — | — | — | **no** |

## 3. Calling conventions and layout (profile fields)

All of these are data; the shared runner, frame shim and stubs read them.

| Field | TH10 | TH11 | TH12 | TH13 | TH14 | TH15 |
| --- | --- | --- | --- | --- | --- | --- |
| `runner_arg` | stack | EBX | EBX | EBX | ECX | ECX |
| `remove_node_abi` | node first | node first | node first | runner first | runner is `this` | runner is `this` |
| `frame_ctx_ecx`, `cleanup_this_ecx`, `screenshot_stack_arg`, `anm_get_vm_ecx` | 0 | 0 | 0 | 0 | 1 (newer compiler) | 1 |
| `frame_flag_value` | 1 | 1 | 1 | 1 | 2 | 2 |
| `runner_return8_ends` | 0 | 1 | 1 | 1 | 1 | 1 |
| `critical_flag_mask` | 0 (always locks) | `0x8000` | `0x8000` | `0xff` | `0xff` | `0xff` |
| `layout.runner_ending` | — | `0x48` | `0x48` | `0x54` | — | — |
| `layout.node_arg` / `runner_next` | `+0x20` / — | `+0x20` / — | `+0x20` / — | `+0x24` / `+0x50` | `+0x24` / `+0x50` | `+0x24` / `+0x50` |
| `layout.enemy_list` | — | `+0x68` | `+0x68` | `+0xb0` | `+0xd0` | `+0x180` |
| `layout.input_width` / `input_size` / `focus_mask` | 2 / `0x6a` / 4 | 4 / `0x130` / 8 | 4 / `0x130` / 8 | 4 / `0x130` / 8 | 4 / — / 8 | 4 / — / 8 |
| `mask_minor_player_edges` | 1 | 1 | 0 | 0 | 0 | 0 |
| `native_size_cycle` | 0 | 0 | 0 | 0 | 0 | 0 |
| Speed writes (`speed_sites`) | 12, mostly `mov` | 15 `fstp` | 15 `fstp` | 16 `fstp` | 12, SSE (`SpeedSrc` XMM/none) | 12, SSE |
| Speed model | timers point at a shared speed; per-site lengths | one global float | one global float | one global float | one global float | one global float; a timer holds an index into a rate table whose entry 0 is that float |
| `sprite_round_sites` (internal resolution) | — | — | — | 4 `frndint` sites | — | — |
| `layout.replay_mode` (the replay manager's mode word) | `+0x10` | `+0x10` | `+0x10` | `+0x10` | `+0x10` | `+0x0c` |
| `addr.latency_cmp` | — | yes | yes | yes | yes | yes |
| `provisional` | 0 | 0 | 0 | 0 | 0 | 0 |

TH08 sets none of these: it has no update runner. Its equivalents are constants inside
`src/games/th08.c`.

## 4. Replays

| | TH08 | TH10 | TH11 | TH12 | TH13 | TH14 | TH15 | New Classic |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| HFR extension | none (state is stock) | yes | yes | yes | yes | yes | yes | none |
| Save sites hooked (`replay_saves`) | — | 2, via `th10_replay_save` | 4 | 4 | 4 | 4 | 2 | — |
| Play sites hooked (`replay_load_calls`) | — | 2, via an ESI/stack adapter | 2 | 2 | 2, EBX/ECX adapter | 2 | 2 | — |
| Peek sites that must stay unhooked | — | `0x429765` | see notes | see notes | 3 | 2 | 2 | — |
| Magic | `T8RP` | `t10r` | `t11r` | `t12r` | `t13r` | `t13r` (reused) | `t15r` | — |
| Directory (`data_dir`) | game folder | game folder | game folder | game folder | `%APPDATA%\ShanghaiAlice\th13\` | `...\th14\` | `...\th15\` | — |
| Desync trace (`trace_state`, `trace_dump`) | own (`th08_trace`) | — | — | — | — | yes | yes | — |

## 5. Per-game hooks, by kind

The same engine assumptions break under sub-stepping in every game. Each row is one kind of
fix; the cell is where that game applies it (`—`: the game does not have the problem; `?`: not
investigated).

| Kind | Helper | TH10 | TH11 | TH12 | TH13 | TH14 | TH15 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Enemy hit-test guard reads the player's integer timer | `g_ptf_prev`/`g_ptf_cur` stub | `0x42863e` | `0x434814` | `0x439ef2` | `0x446888` | `0x451450` | `0x4594ff` |
| Fixed-point player movement loses its remainder | `movement_ftol` / `movement_cvttss` | yes | yes | yes | yes | yes (SSE variant) | yes (SSE variant) |
| Per-call counters inside sub-stepped objects (bullet wait, item countdown, laser ex-wait and graze, option and death counters, shot behaviours) | `gate_block`, or `E_timer_unchanged` in a stub: run only when the object's integer timer changed | yes | yes | yes | yes | yes | yes |
| Per-frame rates outside `MotionState` (shot acceleration and turn, item gravity, homing) | `emit_factor` (`fmul g_factor`) | yes | yes | yes | yes | yes | yes, and the graze slow-down recovery (`0x44017b`) |
| Script-constant `Timer::add` scaled by `dt` (shot cycle, ANM `wait`) | add `value × logical` | 2 sites | 2 sites | 2 sites | 2 sites | next two rows | next two rows |
| Shot-cycle rewind through the rate-scaled `timer_rewind` | rate-pointer swap around the call | row above | row above | row above | row above | `0x45101a` | `0x44b88f`: the rate lookup inside `timer_rewind` (no pointer to swap) |
| Shot countdown timer misses the boundary tick at inexact `dt` | boundary-tick stub | — | — | — (inverse test) | `0x4436b4` | `0x44e08d`, `0x45131b` | `0x454ec4`, `0x459156` |
| Stage distortion consumes RNG per call | frame-boundary gate | — | `0x402bc5` | `0x403145` | `0x4067e4` | ? | ? |
| Bullet state promotion gated to the frame | `gate_block` | — | — | — | — | `0x416877` (does not fix frame 372) | `0x419528` |
| Own frame limiter / FPS watchdog | `patch_bytes` | `0x4393b7`, `0x439488`, `0x413508` | — | — | — | — | — |
| Debug site census (`E_count`) | | — | — | — | — | 8 counters | — |

TH08's hooks are of a different kind (§1): the 60 Hz time gate, the two chain runners, the quad
draw site, the snapshot call, and — only with `substep=1` — five counter gates, the laser graze
gate, four `ExecuteScript` call sites and the behaviour block.

## 6. Dimming (`draw`)

| | TH08 | TH10 | TH11 | TH12 | TH13 | TH14 | TH15 | New Classic |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Attribution | draw-chain walker | runner dispatch | runner dispatch | runner dispatch | runner dispatch | runner dispatch, VM on the stack (`vm_stack_arg`) | as TH14; the VM names its ANM by slot (`vm_slot_off`, `anm_table_off`, `anm_slots`) | own map |
| `world_prio` | 8 | 11 | 11 | 12 | 12 | 19 | 19 | — |
| Rules | by callback | 10 | 9 | 10 | 12 | 8 | 7 | own |
| Script rules (`vm_script_off`) | — | yes | yes | yes | yes | **no** (0) | **no** (0) | — |
| `dim_special` | — | — | — | — | spirits | — | — | — |

## 7. Compatibility data

| | TH08 | TH10 | TH11 | TH12 | TH13 | TH14 | TH15 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| vpatch conflict sites (`*_conflicts.h`) | none | 5 | 4 | 4 | 4 | none | none |
| English / Steam executables | no | yes | yes | yes | yes | unverified | unverified |
| thprac, thcrap, THRotator | untested | yes | yes | yes | yes | untested | untested |
| Frozen signatures | 31 | 82 | 67 | 70 | 102 | 72 | 56 |

## 8. Game-specific branches outside `src/games/`

These are the places shared code knows about a kind of game. All test a profile field.

| Where | Test | Why |
| --- | --- | --- |
| `core/install.c` | `install_presentation` | suppress "not described" log lines; call the presentation installer |
| `core/install.c` | `d3d8` | read `d3d9ex` from `[fixed60]`; skip the D3DX9 import hooks |
| `core/texscale.c` | `d3d8` | mipmapped textures are eligible for upscaling |
| `backends/d3d9.c` | `d3d8` | show the window after converting exclusive presentation to windowed |
| `backends/d3d9.c` | none (always) | release the menu when a game creates a second device (TH08 does) |
| `ui/overlay.c` | `install_presentation` | map the menu's sub-step controls to `[fixed60]` |
| `core/symbols.h` (`REPLAY_MODE`) | `layout.replay_mode` | TH15's replay manager is four bytes shorter in front; 0 means `+0x10` |
| `core/dimming.c` | `draw.vm_slot_off` | TH15's sprite VM holds an ANM slot index, not a pointer; the name is read through `anm_table_off` |
| `core/window.c` | `native_size_cycle` | whether the patch supplies F10 |
| `test-games.ps1` | `$Traits` table | demo through replay hooks, stage-start log line, fixed logic, shot census |

## 9. Candidates for abstraction

Ordered by value. None is required for correctness.

1. **`place_enemy` is three near-copies.** TH14 and TH15 already share one function and a
   `struct Th14EnemySprites` of offsets (`src/games/th14_family.h`, with the option
   interpolation). TH12 and TH13 differ from it only in the same offsets; TH11 is the same
   without offsets and parents. Moving that struct into the profile removes about 50 more
   lines and makes TH10's missing enemy interpolation a data problem.
2. **The enemy hit-test guard is the same six-byte `cmp` in every game** and is always the
   first thing a port needs. Make it a profile field (`addr.hit_guard`, register, timer offset,
   the two exits) and emit it from shared code, like `movement_ftol`.
3. **Two fields no profile uses.** `replay_playing` is NULL in every profile: TH08 tracks
   playback inside its walker (callback `0x452550`). `native_size_cycle` is 0 in every profile:
   no supported game has an F10 of its own. Delete both, with the branches that read them, and
   re-add one if a game needs it.
4. **`fixed logic` is inferred from `install_presentation != NULL`** in three files. Add an
   explicit `int fixed_logic` (or `enum sim_model`) so that a future profile can have a
   presentation installer without being a 60 Hz game.
5. **Replay call-site adapters.** TH10 and TH13 wrap the loader for their register ABIs; the
   other three use the shared stdcall hook. A `replay_load_abi` enum, like `remove_node_abi`,
   would replace two hand-written thunks.
6. **Gate tables.** Most of `thNN_install_sites` is `gate_block(addr, len, skip, reg, timer)`
   and `emit_factor` calls. A static table per game (`struct GateSite[]`, `struct ScaleSite[]`)
   applied by one loop, as `speed_sites` already is, would make the per-game files mostly data
   and let `test-games`/the harness enumerate them.
7. **Stage-start log line for TH08.** `-Drive` cannot confirm TH08 entered a stage. A
   `stage first frame` line from the walker (the GameManager callback registering) would let
   the test judge it like the others.
8. **TH14 and TH15 script-level dim rules.** `vm_script_off = 0` disables script rules on those two;
   finding the offset brings it level with TH10–13.
