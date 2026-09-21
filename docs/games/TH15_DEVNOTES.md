# Touhou 15 — Legacy of Lunatic Kingdom

Reference for the TH15 profile in the shared x86 runtime, written as a delta against
[TH14_DEVNOTES.md](TH14_DEVNOTES.md): read that first. Implementation: `src/games/th15.c`,
`src/games/th15_signatures.h`, `src/games/th15_conflicts.h`, and `src/games/th14_family.h`
(shared with TH14); stub tests in `tools/test_th15_stubs.py`. Shared mechanisms are in
[DEVNOTES_RUNTIME.md](../DEVNOTES_RUNTIME.md).

## 1. Identity

| | |
|---|---|
| executable | Japanese `th15.exe`, v1.00b (`th15e.exe` is accepted by name, unverified) |
| SHA-256 | `67a642357c8777089f468aab9c7a0ae346ebdb62849d842a7b7b18d1e6910364` |
| ImageBase / SizeOfImage | `0x400000` / `0x125000` |
| entry / TimeDateStamp | `0x49269d` / `0x56121ea7` |
| platform | x86, Direct3D 9, `d3dx9_43.dll` |
| replay magic | `t15r` |
| replay directory | `%APPDATA%\ShanghaiAlice\th15\` (`addr.data_dir = 0x519bdd`) |
| conflict sites | none recorded (no `vpatch_th15.dll` has been read) |

56 frozen signatures, in TH14's three groups.

## 2. State

At TH14's level. Described: identification, the video path, the scheduler, the draw path and
dimming, the game speed, six sub-stepped systems, enemy and option interpolation, the replay
extension, the desync trace.

Not described: sub-tick input (`poll_input`, `game_input`), `pp`, `sprite_round_sites`,
script-level dim rules (`vm_script_off`), the English and Steam builds.

Tested under Wine (§8) and by `test-games.ps1 -Matrix -Drive` on Windows at 360 Hz: every case
passes, a driven stage holds 360 ticks/s. Not yet played by a person.

## 3. What is the same as TH14

The update runner and its calling conventions (`RUNNER_ARG_ECX`, `REMOVE_NODE_RUNNER_THIS`,
`frame_ctx_ecx`, `cleanup_this_ecx`, `screenshot_stack_arg`, `anm_get_vm_ecx`,
`frame_flag_value = 2`, `critical_flag_mask = 0xff`, `runner_return8_ends`), three frame
calls, the SSE speed writes, the SSE movement truncation, the replay save (stdcall, four
arguments) and load (`this` in ECX, filename pushed) routines, and the VM draw taking its VM on
the stack. The start-up resolution dialog is the game's own.

The runner's shutdown flag is at `+0x54` (`layout.runner_ending`): the destructor at `0x4709c0`
sets it and runs the list once more so that each node's clean-up is called, not its update.
Without it the input update ran after DirectInput was released (`th15.exe+0x2145`,
`th14.exe+0x1ebe`).

Every TH14 site has a TH15 counterpart except the laser manager's null-rate hook (§4).

## 4. What differs

### Timers hold a rate index, not a rate pointer

A timer is `{prev, int, float, rate_index}`. The index selects an entry of the pointer table
at `0x4ca620`; entry 0 is `&speed` (`0x4e73e8`). Every tick clamps an index of 1 or more back
to 0, so in practice every timer runs at the game speed.

- No timer can have a null rate, so TH14's laser-manager hook has no counterpart.
- There is no pointer to swap around `timer_rewind` (`0x44b870`). The function has two callers,
  both in the shot-cycle driver (`0x458e58` rewinds by 14, `0x458e95` by 119), so the table
  lookup inside it (`0x44b88f`) is replaced by `mov edx, &g_logical`.
- The two boundary-tick timers (`0x454ec4` shot cadence, `0x459156` weapon timer) load
  `ecx = &g_logical` on the boundary tick and skip the advance on the others, as TH14's do.
- The tick's integer path: a rate inside (0.99, 1.01) increments the integer and adds 1.0;
  anything else adds the rate and truncates.

### Graze slow-down

New mechanic. A graze sets the item manager's factor at `[itemmgr + 0xe5def0]` to 0.3; it
recovers by the constant at `0x4cfdf0` per pass at `0x44017b`, with no speed multiply. The item
manager is sub-stepped, so the stub adds `constant × g_factor`.

### Replay manager

The mode word is at `+0x0c` (TH14: `+0x10`), hence `layout.replay_mode` and the `REPLAY_MODE`
macro; stage `+0x214`, frame `+0x20c`, stages `+0x1c`. Two save sites (`0x4521d3`, `0x46ac14`),
two play sites (`0x45b9bc`, `0x45bb46`), two peek sites left alone (`0x45bfd3`, `0x467e9f`).
RNGs: replay `0x4e9a48`, cosmetic `0x4e9a40`.

### Sprite VM

The VM holds its ANM's slot index at `+0x28`, not a pointer; the loaded records are pointers at
`anm_manager + 0x187f4d8 + slot × 4` (name at `+4` of the record). Hence `draw.vm_slot_off`,
`draw.anm_table_off` and `draw.anm_slots`. Layer at `+0x24`, flags at `+0x18`, position at
`+0x5ec`. With these unset the census prints `? layer -1` for everything and `dim_vm_trace`
finds no pointer, because there is none.

## 5. Addresses

| | |
|---|---|
| speed | `0x4e73e8` (12 write sites: 6 permanent, 5 temporary, 1 ECL at `0x42d768` from XMM0) |
| update runner / its `ret 8` | `0x4014f0` / `0x4015fa`; list head `0x4e9a54` |
| frame function / calls | `0x4729c0` / `0x471a8d`, `0x471aab`, `0x471ab7` |
| frame context pointer, flag, value | `0x4e7ec0`, `0x4e7ec4`, `0x4e7c68` |
| cleanup | `0x403f30`, `this = 0x4e8170` |
| remove node | `0x4018a0` |
| critical section, count | `0x503c28`, `0x503d78` |
| latency compare / screenshot | `0x472af0` / `0x44cbf0`, called at `0x472c66` |
| device, window flags, misc flags | `0x4e77d8`, `0x51bbec`, `0x503d86` |
| raw input, pressed | `0x4e6d10`, `0x4e6d1c` |
| player / callback / timer / position | `0x4e9bb8` / `0x4559c0` / `+0x634` / `+0x624` |
| enemy manager / list | `0x4e9a80` / `+0x180`; enemy flags `+0x526c`, position `+0x1250`, skip mask `0x2000000` |
| bullet manager | `0x4e9a6c`; 2001 bullets of `0x1494` at `+0x98` |
| ANM manager / `get_vm` | `0x503c18` / `0x488510` |
| replay manager / save / load | `0x4e9bc4` / `0x45c460` / `0x45cc80`; callbacks `0x45ce90` (record), `0x45ceb0` (playback) |
| draw dispatch / flush / VM draw | `0x40168a` (node in EDI) / `0x47e3f0` / `0x4817d0` |

`0x4e9a68`, four bytes below the bullet manager pointer, is a different object; a
context-vote address mapper placed TH14's enemy manager there. The enemy manager's
constructor (`0x426471`) stores `0x4e9a80`.

## 6. Classes and sites

Sub-stepped: `0x487b40` and `0x487b10` (sprite passes, priorities 9 and 34), `0x41a5b0`
bullets (28), `0x441920` lasers (27), `0x4559c0` player (23), `0x440870` items (29).
Everything else is `MODE_FRAME`; the named ones are in `th15_classes`.

| site | length | what |
|---|---|---|
| `0x41966b`, `0x4199e7` | 11, 28 | bullet counter gates; ESI = bullet, timer prev `+0x1468`, int `+0x146c` |
| `0x419700` | 21 | bullet cancel effect |
| `0x419528` | 12 | bullet state promotion, gated to the frame (`gate_block`) |
| `0x43f8a3`, `0x43f8ca` | 12, 25 | item countdowns; EDI = item, timer `+0xc3c`/`+0xc40`, counter `+0xc74` |
| `0x44017b` | 16 | graze slow-down recovery × `g_factor` |
| `0x454a96` | 7 | player state dispatch (jump table `0x455950`) |
| `0x45455b`, `0x454563` | — | `movement_cvttss`, y then x |
| `0x454fc8` | 14 | blink guard |
| `0x454664` | 6 | focus counter (`gate_block`, EDI, `+0x62c`) |
| `0x4546e9` | 9 | option approach |
| `0x454e3b`, `0x454e5c` | 12, 10 | shot rates × `g_factor` |
| `0x454ec4` | 10 | shot cadence timer, boundary tick only |
| `0x4594ff` | 8 | shot-against-enemy guard (`g_ptf_prev`/`g_ptf_cur`) |
| `0x44b88f` | 7 | `timer_rewind` rate → `&g_logical` |
| `0x459156` | 10 | weapon timer, boundary tick only |

Interpolation: `th15_enemy_sprites = {0x120c, 0x124, 0x164, 0x224, 0x4000000, 0x17b, 0x0e}`;
options at player `+0x668`, VM position word `0x17b`.

## 7. Dimming

`world_prio = 19` (`enemy.anm` layer 8). TH14's map with the priorities above the player moved
up by one:

| prio | ANM / layer | class |
|---|---|---|
| 32 | item manager's draw (`0x4408a0`), `bullet.anm` L0 | `DIM_ITEMS` |
| 36 | bullet manager's draw (`0x41a5e0`), `bullet.anm` L0 | none |
| 27–31 | `effect.anm` L13–15 (focus ring, hitbox) | none |
| 27, 31 | `pl00.anm` L13, L15 | `DIM_PLAYER_SHOTS` |
| 28, 30 | `pl00.anm` L14 | none |
| 38, 43, 44, 77 | `effect.anm` | `DIM_EFFECTS` |

Seen and left alone: `bullet.anm` L20/L21 at 43/44 (as TH14), `text.anm`, `front.anm`,
`st01logo.anm`, `ascii.anm`.

## 8. Verification

All under Wine with a null ALSA device (`pcm.!default { type null }`): without an audio device
the unmodified game waits forever on its sound command queue (`0x51e0a4`) at "Now Loading".

- Native harness and `test_th15_stubs.py` pass.
- A stage played sub-stepped at 120 Hz: ticks/s at target, continuous fire, enemies die, items
  drop, dimming classes fade what they name.
- Title demo (`demo/demo0.rpy`, stage 3) with `replay_trace=1`:
  - `substep=0` twice: identical, 2817 frames.
  - `substep=1` at 60 fps (one tick a frame) against `substep=0`: identical, 2439 frames. The
    hooks are inert at one tick per frame.
  - `substep=1` at 120 fps against `substep=0`: bullets differ from frame 134, the first shot
    of the stage. The shot is aimed; the player is moving 5 px a frame; enemies are
    `MODE_FRAME` and run on the boundary tick, where the sub-stepped player has made half her
    step. Angle 0.288 against 0.282. That bullet reaches the side wall one frame later, the
    enemy's script branches on it, and the runs part from there. This is the ordering hazard
    common to TH10–15 (the player moves before the enemies read her), not a TH15 site.

Not done: a parity run with a stationary player, which would test the bullet and item sites
without the aim difference; a recorded-then-replayed sub-stepped run.

## 9. How the port was made

TH14's and TH15's listings were matched function by function (shingled opcode sequences),
then each TH14 site was located inside its matched function by diffing the two bodies. Globals
were mapped from the instructions that reference them. Mapping addresses by voting on
surrounding context was unreliable for thunks and for the player code, and produced the wrong
enemy manager (§5); read the constructor instead. A hazard scan for the §8 patterns of the
TH14 notes (integer counters, `int == N` gates, truncations, float adds with no speed
multiply) over the sub-stepped classes found the one new site (§4, graze slow-down).

## 10. Open

- Sub-tick input.
- `vm_script_off`.
- Stage distortion RNG (TH11–13 gate it; not looked for here or on TH14).
- English and Steam executables; `vpatch_th15.dll`.
