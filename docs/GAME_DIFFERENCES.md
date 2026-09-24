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

|  | TH08 | TH10 | TH11 | TH12 | TH13 | TH14 | TH15 | TH18 | TH20 | New Classic |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Runtime | x86 | x86 | x86 | x86 | x86 | x86 | x86 | x86 | x86 | x64 (`hfr64.c`) |
| Graphics | D3D8 → d3d8to9 (`d3d8`) | D3D9 | D3D9 | D3D9 | D3D9 | D3D9 | D3D9 | D3D9 | D3D9 | D3D11 |
| Loader | `dinput8.dll` | `dinput8.dll` | `dinput8.dll` | `dinput8.dll` | `dinput8.dll` | `dinput8.dll` | `dinput8.dll` | `dinput8.dll` | `dinput8.dll` | `dxgi.dll` |
| `d3dx` | `d3dx8.dll` (static) | `d3dx9_31` | `d3dx9_37` | `d3dx9_40` | `d3dx9_43` | `d3dx9_43` | `d3dx9_43` | `d3dx9_43` | `d3dx9_43` | — |
| Update structure | Chain of callbacks, walked by `th08_walk` | update runner | update runner | update runner | update runner | update runner | update runner | update runner | the game's own runner, wrapped at its entry, node call and exit (`runner_wrap`) | own lists |
| Simulation rate | 60 Hz, with the player's movement, the bullets and the lasers sliced at the display's rate | display | display | display | display | display | display | display | display | 60 Hz fixed |
| Extra frames come from | sub-stepping of the player's movement, the bullets and the lasers; prediction + interpolation for the rest (`install_presentation`) | sub-stepping | sub-stepping | sub-stepping | sub-stepping | sub-stepping | sub-stepping | sub-stepping | sub-stepping | interpolation |
| INI section | `[hfr]` (`substep`, `subtick_input`); `[fixed60]` for smoothing (`interpolate`, `predict`) | `[hfr]` | `[hfr]` | `[hfr]` | `[hfr]` | `[hfr]` | `[hfr]` | `[hfr]` | `[hfr]` | `[fixed60]` |
| Frame entry | `frame_original`, `update_only` callbacks | `frame_fn` + `frame_calls` | same | same | same | same | same | `frame_fn` + `frame_calls`; the catch-up tick is the adapter's (`update_only`: the frame function's viewport select and cleanup around the runner) | `frame_fn` + `frame_calls`; the catch-up tick is the adapter's (`update_only`) | own clock hooks |

## 2. What runs at the display's rate (`classes`)

| System | TH08 | TH10 | TH11 | TH12 | TH13 | TH14 | TH15 | TH18 | TH20 | New Classic |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Player | movement sliced (`subtick_input`) | sub | sub | sub | sub | sub | sub | sub | sub | opt-in (`subtick`) |
| Enemy bullets | sub (`substep`) | sub | sub | sub | sub | sub | sub | sub | sub | opt-in (`substep`) |
| Lasers | sub (`substep`) | 60 Hz | sub | sub | sub | sub | sub | sub | sub | opt-in (`substep`) |
| Items | 60 Hz, smoothed (sliced, they integrate gravity and homing differently) | sub | sub | sub | sub | sub | sub | sub | sub | 60 Hz |
| Stage / background | camera smoothed | 60 Hz | sub | sub | sub | 60 Hz | 60 Hz | 60 Hz | 60 Hz | interpolated |
| Sprite animation | interpolated quads | sub (World, UI) | sub | sub | sub | sub (Early, Late passes) | sub (Early, Late passes) | sub (Early, Late passes) | sub (Early, Late passes) | interpolated |
| Enemies | predicted quads | 60 Hz, **not interpolated** (`place_enemy` NULL) | interpolated | interpolated | interpolated | interpolated | interpolated | interpolated | interpolated | interpolated |
| Player options | predicted quads | 60 Hz | 60 Hz | 60 Hz | 60 Hz | interpolated (`place_options`) | interpolated (`place_options`) | interpolated (`place_options`) | carried with the player between frames (`th20_option_display`) | interpolated |
| Sub-tick input (`addr.poll_input`, `addr.game_input`) | `th08_minor_input`, recorded per stage like the others' | yes | yes | yes | yes | yes | yes | yes | yes, through the adapter's own poll (`poll_raw`) | own |
| Replays carry the rate, settings and per-tick input | yes (`replay_playing`, own save and register hooks; magic `T8RP`) | yes | yes | yes | yes | yes | yes | yes | yes | **no** (its opt-in modes stay on during playback) |
| Game speed (`g_speed_pct`) | shared scheduler | shared scheduler | same | same | same | same | same | same | same | own clock (`fixed_clock_step_at`), fast forward up to the presentation rate over 60 |

## 3. Calling conventions and layout (profile fields)

All of these are data; the shared runner, frame shim and stubs read them.

| Field | TH10 | TH11 | TH12 | TH13 | TH14 | TH15 | TH18 | TH20 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `runner_arg` | stack | EBX | EBX | EBX | ECX | ECX | ECX | ECX |
| `remove_node_abi` | node first | node first | node first | runner first | runner is `this` | runner is `this` | runner is `this` | — (the game removes its own nodes) |
| `frame_ctx_ecx`, `cleanup_this_ecx`, `screenshot_stack_arg`, `anm_get_vm_ecx` | 0 | 0 | 0 | 0 | 1 (newer compiler) | 1 | 1 | `frame_ctx_ecx` 1; the others unused |
| `frame_flag_value` | 1 | 1 | 1 | 1 | 2 | 2 | 2 (unused: `update_only`) | 2 |
| `runner_return8_ends` | 0 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| `critical_flag_mask` | 0 (always locks) | `0x8000` | `0x8000` | `0xff` | `0xff` | `0xff` | `0xff` | — (the game's runner takes its own lock) |
| `layout.runner_ending` | — | `0x48` | `0x48` | `0x54` | `0x54` | `0x54` | `0x54` | — |
| `layout.node_arg` / `runner_next` | `+0x20` / — | `+0x20` / — | `+0x20` / — | `+0x24` / `+0x50` | `+0x24` / `+0x50` | `+0x24` / `+0x50` | `+0x24` / `+0x50` | — |
| `layout.enemy_list` | — | `+0x68` | `+0x68` | `+0xb0` | `+0xd0` | `+0x180` | `+0x18c` | `+0x10c` |
| `layout.input_width` / `input_size` / `focus_mask` | 2 / `0x6a` / 4 | 4 / `0x130` / 8 | 4 / `0x130` / 8 | 4 / `0x130` / 8 | 4 / `0x248` / 8 | 4 / `0x248` / 8 | 4 / `0x248` / 8 | 4 / — (`poll_raw` saves what it needs) / 8 |
| `layout.autofocus_frames` ("hold shot to focus") | — | 8 | 8 | 8 | 10 | 10 | 10 | 10; the option is bit `0x100` (`layout.autofocus_option`) |
| `mask_minor_player_edges` | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| `native_size_cycle` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| Speed writes (`speed_sites`) | 12, mostly `mov` | 15 `fstp` | 15 `fstp` | 16 `fstp` | 12, SSE (`SpeedSrc` XMM/none) | 12, SSE | 10, SSE: 9 stores and the script instruction's call to the setter (`0x435ea3`) | 16 calls to `Float::set`, hooked by the adapter (`speed_sites_own`) |
| Speed model | timers point at a shared speed; per-site lengths | one global float | one global float | one global float | one global float | one global float; a timer holds an index into a rate table whose entry 0 is that float | as TH15; the ECL instruction sets the speed through a setter (`0x43a200`) that AnmVm::run also uses, so the call site is hooked and not the setter | a `Float` object with a setter; a timer holds an index into a rate table whose entry 0 is that object |
| `sprite_round_sites` (internal resolution) | — | — | — | 4 `frndint` sites | — | — | — | — |
| `layout.replay_mode` (the replay manager's mode word) | `+0x10` | `+0x10` | `+0x10` | `+0x10` | `+0x10` | `+0x0c` | `+0x0c` | `+0x10` |
| `addr.latency_cmp` | — | yes | yes | yes | yes | yes | yes | — |
| `provisional` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `runner_wrap` (the game keeps its runner; three hooks instead of a replacement) | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 |
| `speed_sites_own` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 |
| `poll_raw` (the adapter polls the devices for sub-tick input) | — | — | — | — | — | — | — | `th20_poll_raw` |

TH08 sets none of these: it has no update runner. Its equivalents are constants inside
`src/games/th08.c`.

## 4. Replays

|  | TH08 | TH10 | TH11 | TH12 | TH13 | TH14 | TH15 | TH18 | TH20 | New Classic |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| HFR extension | yes | yes | yes | yes | yes | yes | yes | yes | yes | none |
| Save sites hooked (`replay_saves`) | own: the result screen's `SaveReplay` (`0x457471`, `th08_replay_save`); the file's own path | 2, via `th10_replay_save` | 4 | 4 | 4 | 4 | 2 | 2 | 2, via `th20_replay_save` (a method with four arguments) | — |
| Play sites hooked (`replay_load_calls`) | own: both `RegisterChain` calls (`0x43b3a7` play, `0x43b50b` record, `th08_register_chain`); the extension is read on a game's first registration to play | 2, via an ESI/stack adapter | 2 | 2 | 2, EBX/ECX adapter | 2 | 2 | 2 | 1, via `th20_replay_load` | — |
| Peek sites that must stay unhooked | none: the menu's peek calls the loader directly and registers nothing | `0x429765` | see notes | see notes | 3 | 2 | 2 | 1 (`0x461d6b`) | 1 (`0x508ad8`) | — |
| Magic | `T8RP` | `t10r` | `t11r` | `t12r` | `t13r` | `t13r` (reused) | `t15r` | `t18r` | `t20r` | — |
| Directory (`data_dir`) | game folder (the path the game passes) | game folder | game folder | game folder | `%APPDATA%\ShanghaiAlice\th13\` | `...\th14\` | `...\th15\` | `...\th18\` | `...\th20\` | — |
| Desync trace (`trace_state`, `trace_dump`) | own (`th08_trace`: bullets, lasers, items, stage and end markers, no line for a paused frame) | — | — | — | — | yes | yes | `trace_state` (bullets, items, lasers, the player's state and shots) | `trace_state` | — |
| Stage start (first frame of the stream) | the first frame the player runs after `RegisterChain` (the record node runs after her) | the replay node's first frame (`replay_stage_start`) | same | same | same | same | same | same | same | — |
| A pause at a rate that is not a multiple of 60 | the schedule continues from the last frame the player ran on (`th08_sched_resume`) | the slicing is moved to the replay frame's canonical place on the first frame after it (`pause_resync_begin`, `schedule_to_frame`) | same | same | same | same | same | same | same | — |
| The replay's own fast-forward ("run the list again") | counted by the walker; run by the shared `run_ff_frames` | counted by `hfr_runner`; the extra frames run as whole tick sequences after the presentation (`run_ff_frames`) | same | same | same | same | same | same | same, counted in `hfr_wrap_node` | — |

## 5. Per-game hooks, by kind

The same engine assumptions break under sub-stepping in every game. Each row is one kind of
fix; the cell is where that game applies it (`—`: the game does not have the problem; `?`: not
investigated).

| Kind | Helper | TH10 | TH11 | TH12 | TH13 | TH14 | TH15 | TH18 | TH20 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Enemy hit-test guard reads the player's integer timer | `g_ptf_prev`/`g_ptf_cur` stub | `0x42863e` | `0x434814` | `0x439ef2` | `0x446888` | `0x451450` | `0x4594ff` | `0x45f144` | `0x4c04bf`: the game asks "did the player's timer change" itself (its own slow-motion gate); answered per frame by `th20_player_ticked` |
| Fixed-point player movement loses its remainder | `movement_ftol` / `movement_cvttss` | yes | yes | yes | yes | yes (SSE variant) | yes (SSE variant) | yes (`movement_cvttss_xmm`: the velocity is truncated from XMM registers, `0x45b6f7`) | yes (SSE variant) |
| Per-call counters inside sub-stepped objects (bullet wait, item countdown, laser ex-wait and graze, option and death counters, shot behaviours) | `gate_block`, or `E_timer_unchanged` in a stub: run only when the object's integer timer changed | yes | yes | yes | yes | yes | yes | yes, and new: the position history the shot types read (`0x45b89d`), each shot's age (`0x45c4ff`), and the option and shot-type callbacks, called on the boundary tick only (`0x45bd0b`, `0x45ee20`) | yes |
| Per-frame rates outside `MotionState` (shot acceleration and turn, item gravity, homing) | `emit_factor` (`fmul g_factor`) | yes | yes | yes | yes | yes | yes, and the graze slow-down recovery (`0x44017b`) | yes, the graze slow-down recovery (`0x446819`), and the muzzle's motion step at a shot's creation, made with the logical speed (`0x45e6c9`) | yes: shot turn and acceleration, and a spawning bullet's per-call movement (`0x485cd1`) |
| Script-constant `Timer::add` scaled by `dt` (shot cycle, ANM `wait`) | add `value × logical` | 2 sites | 2 sites | 2 sites | 2 sites | next two rows | next two rows | next two rows | 5 calls of `Timer::operator+=`/`-=`, which scale by the speed (`th20_timer_jump`) |
| Shot-cycle rewind through the rate-scaled `timer_rewind` | rate-pointer swap around the call | row above | row above | row above | row above | `0x45101a` | `0x44b88f`: the rate lookup inside `timer_rewind` (no pointer to swap) | `0x452c10`: as TH15 | `0x505c5b`, `0x505d23` (`th20_timer_wrap`: the wrap also stands in for that frame's tick) |
| Shot countdown timer misses the boundary tick at inexact `dt` | boundary-tick stub | — | — | — (inverse test) | `0x4436b4` | `0x44e08d`, `0x45131b` | `0x454ec4`, `0x459156` | `0x45c4a5`, `0x45f068`; and a shot fired this frame is skipped on the minor ticks (`0x45c402`), because stock does not move it until the next frame | — (the game fires on "timer changed") |
| Stage distortion consumes RNG per call | frame-boundary gate | — | `0x402bc5` | `0x403145` | `0x4067e4` | ? | ? | ? | ? |
| Bullet state promotion gated to the frame | `gate_block` | — | — | — | — | `0x416877` (does not fix frame 372) | `0x419528` | `0x424007` | — |
| Own frame limiter / FPS watchdog | `patch_bytes` | `0x4393b7`, `0x439488`, `0x413508` | — | — | — | — | — | — | `0x419e64`, `0x419e85` |
| Debug site census (`E_count`) |  | — | — | — | — | 8 counters | — | — | — |

TH08's hooks are of a different kind (§1): the 60 Hz time gate, the two chain runners, the quad
draw site, the snapshot call, the replay manager's registration (two call sites) and the result
screen's save, and, for its sub-stepped projectiles ([TH08_DEVNOTES.md](games/TH08_DEVNOTES.md)
§9):

- the items' update, run on the frame tick only on the stock multiplier (`0x43127b`);
- three counter gates (`gate_block`: the off-screen grace and two manager counters);
- the off-screen test, made on the frame's last tick (`0x4314b3`);
- the laser graze gate;
- four `ExecuteScript` call sites, whose "finished" is given on the frame tick, or on the last
  tick for a spawning bullet a bomb cancelled;
- the behaviour block, run once a frame on the stock state.

## 6. Dimming (`draw`)

|  | TH08 | TH10 | TH11 | TH12 | TH13 | TH14 | TH15 | TH18 | TH20 | New Classic |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Attribution | draw-chain walker | runner dispatch | runner dispatch | runner dispatch | runner dispatch | runner dispatch, VM on the stack (`vm_stack_arg`) | as TH14; the VM names its ANM by slot (`vm_slot_off`, `anm_table_off`, `anm_slots`) | as TH15 | runner dispatch; the node is behind an iterator and the ANM name inside a `std::string` (`emit_node`, `anm_name`) | own map |
| `world_prio` | 8 | 11 | 11 | 12 | 12 | 19 | 19 | 16 | 20 | — |
| Rules | by callback | 10 | 9 | 10 | 12 | 8 | 7 | 9 | 8 | own |
| Script rules (`vm_script_off`) | — | yes | yes | yes | yes | **no** (0) | **no** (0) | **no** (0) | offset known (`0x24`), no rule uses it | — |
| `dim_special` | — | — | — | — | spirits | — | — | — | — | — |
| Own options (`toggles`, `toggle_count`; INI `[game]`) | — | — | — | — | — | — | graze tint and shake, graze glow | — | graze tint and shake | — |

## 7. Compatibility data

|  | TH08 | TH10 | TH11 | TH12 | TH13 | TH14 | TH15 | TH18 | TH20 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| vpatch conflict sites (`*_conflicts.h`) | none | 5 | 4 | 4 | 4 | none | none | none | none |
| English / Steam executables | no | yes | yes | yes | yes | unverified | unverified | unverified | Steam v1.00c only; the image is relocatable and the signatures are matched relocation-aware |
| thprac, thcrap, THRotator | untested | yes | yes | yes | yes | untested | untested | untested | untested |
| Frozen signatures | 35 | 82 | 67 | 70 | 102 | 75 | 61 | 58 | 75 |

## 8. Game-specific branches outside `src/games/`

These are the places shared code knows about a kind of game. All test a profile field.

| Where | Test | Why |
| --- | --- | --- |
| `core/install.c` | `install_presentation` | suppress "not described" log lines; call the presentation installer |
| `core/install.c` | `d3d8` | read `d3d9ex` from `[fixed60]`; skip the D3DX9 import hooks |
| `core/texscale.c` | `d3d8` | mipmapped textures are eligible for upscaling |
| `backends/d3d9.c` | `d3d8` | show the window after converting exclusive presentation to windowed |
| `backends/d3d9.c` | none (always) | release the menu when a game creates a second device (TH08 does) |
| `ui/overlay.c` | `install_presentation` | map the menu's sub-step controls to TH08's switches (`fixed_substep`, `subtick_input`) and save smoothing to `[fixed60]` |
| `core/symbols.h` (`REPLAY_MODE`) | `layout.replay_mode` | TH15's replay manager is four bytes shorter in front; 0 means `+0x10` |
| `core/dimming.c` | `draw.vm_slot_off` | TH15's sprite VM holds an ANM slot index, not a pointer; the name is read through `anm_table_off` |
| `core/dimming.c` | `draw.emit_node`, `draw.anm_name` | TH20's draw runner keeps the node behind an iterator and an ANM record's name in a `std::string`; the adapter emits the load and reads the name |
| `core/input.c` | `poll_raw`, `layout.autofocus_option` | TH20's poll is a method that rewrites far more than one input record; its "hold shot to focus" option is another bit |
| `core/install.c` | `runner_wrap`, `speed_sites_own`, `update_only`, `poll_raw` | suppress "not described" lines for what a wrapped runner does not need; leave the game's runner in place |
| `identity.h`, `core/install.c` | the image's base relocations | TH20 is the first relocatable executable: signatures are compared, and every profile address moved, by the load delta |
| `core/window.c` | `native_size_cycle` | whether the patch supplies F10 |
| `test-games.ps1` | `$Traits` table | demo through replay hooks, stage-start log line, fixed logic, shot census |

## 9. Candidates for abstraction

Ordered by value. None is required for correctness.

1. **`place_enemy` is three near-copies.** TH14, TH15 and TH18 already share one function and a
   `struct Th14EnemySprites` of offsets (`src/games/th14_family.h`, with the option
   interpolation, which takes the option array's stride and count). TH12 and TH13 differ from it only in the same offsets; TH11 is the same
   without offsets and parents. Moving that struct into the profile removes about 50 more
   lines and makes TH10's missing enemy interpolation a data problem.
2. **The enemy hit-test guard is the same six-byte `cmp` in every game** and is always the
   first thing a port needs. Make it a profile field (`addr.hit_guard`, register, timer offset,
   the two exits) and emit it from shared code, like `movement_ftol`.
3. **A field no profile uses.** `native_size_cycle` is 0 in every profile: no supported game
   has an F10 of its own. Delete it, with the branch that reads it, and re-add it if a game
   needs it. (`replay_playing` has its user now: TH08, whose replay manager is not the
   TH10–20 one.)
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
7. **Stage-start log line for TH08.** Done: the walker logs `stage N first frame`, the same
   line as the runner, and `-Drive` judges TH08 like the others.
8. **TH14 and TH15 script-level dim rules.** `vm_script_off = 0` disables script rules on those two;
   finding the offset brings it level with TH10–13.
