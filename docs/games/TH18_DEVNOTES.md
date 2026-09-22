# Touhou 18 — Unconnected Marketeers

Reference for the TH18 profile in the shared x86 runtime, written as a delta against
[TH15_DEVNOTES.md](TH15_DEVNOTES.md) (itself a delta against [TH14_DEVNOTES.md](TH14_DEVNOTES.md)):
read those first. Implementation: `src/games/th18.c`, `src/games/th18_signatures.h`,
`src/games/th18_conflicts.h`, and `src/games/th14_family.h` (shared with TH14 and TH15); stub
tests in `tools/test_th18_stubs.py`. Shared mechanisms are in
[DEVNOTES_RUNTIME.md](../DEVNOTES_RUNTIME.md).

## 1. Identity

| | |
|---|---|
| executable | Japanese `th18.exe`, v1.00a (`th18e.exe` is accepted by name, unverified) |
| SHA-256 | `6243e3624ae5100eaa5ded846e2d9b2d9e438ee7c20735e197c9c8170fb9627f` |
| ImageBase / SizeOfImage | `0x400000` / `0x174000` (not relocatable) |
| entry / TimeDateStamp | `0x48e5c9` / `0x607a2b08` |
| platform | x86, Direct3D 9, `d3dx9_43.dll` |
| replay magic | `t18r` |
| replay directory | `%APPDATA%\ShanghaiAlice\th18\` (`addr.data_dir = 0x568c61`) |
| conflict sites | none recorded (no `vpatch_th18.dll` has been read) |

58 frozen signatures, in TH14's three groups.

## 2. State

At TH15's level. Described: identification, the video path, the scheduler, the draw path and
dimming, the game speed, six sub-stepped systems, enemy and option interpolation, the replay
extension, the desync trace.

Not described: `pp` writes, `sprite_round_sites`, script-level dim rules (`vm_script_off`), the
English build, the ability cards beyond leaving them at 60 Hz (§4).

Verified under Wine against the title demo and a recorded stage (§8). On Windows it has been
played by the project's owner at 360 Hz and reported working; the automated Windows tests have
not been run on it.

### History

A first TH18 adapter (branch `codex/th18-support`) kept the game's simulation at 60 Hz and
interpolated its sprite quads and stage camera, the way TH08 is handled. That is the right
model for TH08, whose engine has no shared speed; TH18 is TH15's engine three games on, with
the same runner, timers, input object and replay manager, and so it is ported the way TH15
was: sub-stepped, with the enemies interpolated. The quad interpolation extracted from TH08 in
that work stays in `src/backends/fixed_quad.h`, used by TH08.

## 3. What is the same as TH15

Everything structural: the update runner (`RUNNER_ARG_ECX`, `REMOVE_NODE_RUNNER_THIS`, list at
`+0x18`, node argument at `+0x24`, `runner_next +0x50`, `runner_ending +0x54`,
`critical_flag_mask 0xff`, `runner_return8_ends`), the frame function with its two calls, the
SSE speed writes, timers holding a rate index into a table whose entry 0 is the speed, the
`0x248`-byte input object, the replay manager with its mode word at `+0x0c`, the replay save
(stdcall, four arguments) and load (`this` in ECX, filename pushed) routines, the VM draw
taking its VM on the stack and naming its ANM by slot, the enemy's sub-object with 14 VM ids,
offsets and parent slots, and the options as an array of fixed-point positions with two VM
ids each.

Every TH15 site has a TH18 counterpart. The address mapping was done by instruction-context
voting (`tools/porting`), which found every site outside the player, and by diffing the player
and shot functions body against TH15's for the rest.

## 4. What differs

### The catch-up tick is the adapter's

The frame function (`0x472fd0`) does more around the runner than TH15's: it flushes the sprite
batch (`0x47e730` on the ANM manager), selects viewport context 2 on the supervisor
(`0x41b330(0x4ccdf0, 2)`), runs the pass, and runs the end-of-pass cleanup (`0x402b30` on
`0x4cd884`) on the runner's answers 0 and -1. `th18_update_only` does the same, so the profile
sets `update_only` rather than the five frame-context addresses.

### The speed setter

The ECL instruction that sets the game speed (`0x435ea3`) calls a setter (`0x43a200`,
`movss [speed], xmm1`) rather than storing itself. The call is the `SPEED_ECL` site, with the
value in XMM1. The setter must not be: `AnmVm::run` restores the speed it saved through the
same setter (`0x47b832`, `0x47b882`), and a setter that took every value as the logical speed
compounded the sub-step factor into it until the game stood still at the first stage.

### The player

The life-state timer is at `+0x634`/`+0x638`/`+0x63c` and the fixed-point position at
`+0x62c`/`+0x630` (TH15: `+0x62c`/`+0x630`/`+0x634` and `+0x624`/`+0x628`). The state word is
`+0x476ac`, the dispatch at `0x45bec3` (table `0x45ca8c`, tail `0x45c3d9`).

- **Movement truncates from registers.** The velocity, already multiplied by the speed, is in
  XMM1/XMM2 when it is truncated into the position (`0x45b6f7`: `cvttss2si ecx,xmm1;
  cvttss2si edx,xmm2`). TH15's helper re-emits a memory operand; `movement_cvttss_xmm`
  (`src/core/site_helpers.c`) is the register form, borrowing two other XMM registers and
  giving them back. Both truncations are one 8-byte site.
- **A 33-entry position history** (`+0x47800`, two dwords each) is shifted while the player
  moves (`0x45b89d`) and read by the shot types (entry 16 at `0x45ddf0`). Gated to once a
  frame on the state timer, like the focus counter (`0x45b8e4`, `+0x477e8`).
- **Option and shot-type callbacks.** Each of the four options (`0xf0` bytes at `+0x670`) and
  each shot object (`0xf8` bytes, 512 at `+0x1570`) carries a callback from its shot type's
  table (`+0xe8` and `+0xd8`), called once a frame by the game (`0x45bd15`, `0x45ee2d`): the
  homing aim with its per-call turn limit, the charge ramps, the target search. None
  multiplies by the speed. Called on every tick they ran twice a frame at 120 Hz, the homing
  shots turned twice as fast and enemies died a frame or two early, which the demo trace showed
  as a steady three-bullet gap. Both calls are gated to the boundary tick (`0x45bd0b`,
  `0x45ee20`).
- **A shot's age.** The shot entries (`0x9c` bytes, 1024 at `+0x20574`) count their age at
  `+0x88`, decremented in the player's shot loop after the timer (`0x45c502`). Once a frame
  (`0x45c4ff`).
- **A shot fired this frame does not move.** The shot objects fire after the loop, so stock
  first moves a new shot the frame after it appears. Sub-stepped, the minor ticks after the
  spawn moved it, and every shot flew half a frame ahead of stock for its whole life — 12 px
  at the shot speed, seen as volleys leaving the screen a frame early. The loop's head
  (`0x45c402`) skips, on a minor tick, an active entry whose age is still 0.
- **The muzzle step.** When a shot object is created (`0x45e320`), its muzzle position is
  advanced one motion step (`0x402bf0`, position += velocity × speed) before the shot type
  reads it. A creation is a once-a-frame event, so the call (`0x45e6c9`) is made with the
  logical speed in the speed global; left alone every shot was born 12 px short.
- The rest is TH15's, moved: blink guard `0x45c63a`, option approach `0x45bd2f` (the counter
  is EBX and is a constant 30, so the check always passes), shot rates `0x45c41b`/`0x45c43c`,
  shot cadence timer `0x45c4a5` (the block also loads XMM4, kept on both paths),
  shot-against-enemy guard `0x45f144`, `timer_rewind` rate lookup `0x452c10` (callers
  `0x45ece1`, `0x45ed3a`), shot object timer `0x45f068`.

What is still half a frame off: an enemy's hit test against the player's shots runs in the
enemy manager on the boundary tick, when a sub-stepped shot has made half its step. A hit
that stock lands at the end of frame N lands at the boundary of N+1 when the shot entered the
hitbox in the second half of the step: at most one frame late, and the same in TH15. The
mirror of the aim hazard in TH15 §8.

### Ability cards

New in TH18. The card manager (`0x408a90`, priority 22) and the per-card objects run once a
frame (`MODE_FRAME`); the player calls into the card list on her state changes (`0x45c0d1`,
`0x45c114`) from paths that run on the boundary tick. Their sprites (`ability.anm`) are drawn
in the player's band and left alone by dimming. Not audited for anything that reads the
sub-stepped systems per call.

### Bullets, lasers, items

TH15's sites, moved: bullet counter gate `0x424851` (ESI = bullet, timer `+0xf80`/`+0xf84`;
28 bytes covering the wait counter and the collision countdown), state promotion `0x424007`,
item countdowns `0x445af3` and `0x445b78` (EDI = item, timer `+0xc4c`/`+0xc50`, counter
`+0xc84`), graze slow-down recovery `0x446819` (`[manager+0xe6bb14]`). TH15's bullet cancel
site has no counterpart: the tail block covers it. Lasers need no site (as TH15).

## 5. Addresses

| | |
|---|---|
| speed | `0x4ccbf0` (10 sites: 4 permanent, 5 temporary, the ECL call at `0x435ea3` from XMM1) |
| update runner / its `ret` | `0x4012e0` / `0x4013f5`; runner pointer `0x4cf294` |
| frame function / calls | `0x472fd0` / `0x471c4e`, `0x471c5a`; the inlined third path is bypassed at `0x471a9e` |
| catch-up tick | `th18_update_only`: flush `0x47e730` on `0x51f65c`, viewport `0x41b330(0x4ccdf0, 2)`, cleanup `0x402b30` on `0x4cd884` |
| remove node | `0x4015a0` |
| critical section, count | `0x521660`, `0x5217b0` |
| latency compare / screenshot | `0x4730be` / `0x453f40`, called at `0x473367` |
| device, pp, window flags, misc flags | `0x4ccdf8`, `0x4ccee4`, `0x56ac70`, `0x5217be` |
| raw input, pressed | `0x4ca210`, `0x4ca21c` |
| player / callback / timer / position | `0x4cf410` / `0x45caa0` / `+0x63c` / `+0x62c` |
| enemy manager / list | `0x4cf2d0` / `+0x18c`; enemy flags `+0x635c`, position `+0x1270`, skip mask `0x2000000` |
| bullet manager | `0x4cf2bc`; 2001 bullets of `0xfa0` at `+0xec`, state `+0xf68`, position `+0x638` |
| item manager | `0x4cf2ec`; `0x1258` items of `0xc94` at `+0x14`, state `+0xc74`, position `+0xc30` |
| laser manager | `0x4cf3f4`; count `+0x798` |
| ANM manager / `get_vm` | `0x51f65c` / `0x488b40` |
| replay manager / save / load | `0x4cf418` / `0x461e90` / `0x462680`; callbacks `0x462940` (record), `0x462a50` (playback); `0x462c30` is speed control only; save calls `0x459ab3`, `0x46aa65`; load calls `0x461965`, `0x461ab3`; the header peek at `0x461d6b` stays unhooked |
| input | one `0x248` object at `0x4ca210`: `poll_input` `0x401c50`, `game_input` `0x4ca428` (latched at `0x46295a`), pressed `0x4ca434`, released `0x4ca438`, option flags `0x4cd014` (autofocus bit `0x200`), autofocus counter `0x4ca3a4` (threshold 10) |
| draw dispatch / flush / VM draw | `0x401490` (node in EDI) / `0x47e730` / `0x481210` |
| sprite VM | layer `+0x18`, ANM slot `+0x20`, slot table at manager `+0x312072c`, 33 slots |

## 6. Classes and sites

Sub-stepped: `0x488250` and `0x488220` (sprite passes, priorities 11 and 34), `0x424e70`
bullets (29), `0x448870` lasers (28), `0x45caa0` player (23), `0x446ec0` items (30).
Everything else is `MODE_FRAME`; the named ones are in `th18_classes`.

| site | length | what |
|---|---|---|
| `0x471a9e` | 6 | the window loop's inlined frame path becomes a jump past it |
| `0x424851` | 28 | bullet counter gates; ESI = bullet, timer prev `+0xf80`, int `+0xf84` |
| `0x424007` | 13 | bullet state promotion, boundary tick only |
| `0x445af3`, `0x445b78` | 13, 25 | item countdowns; EDI = item, timer `+0xc4c`/`+0xc50`, counter `+0xc84` |
| `0x446819` | 16 | graze slow-down recovery × `g_factor` |
| `0x45bec3` | 7 | player state dispatch (jump table `0x45ca8c`) |
| `0x45b6f7` | 8 | `movement_cvttss_xmm`, x from XMM1 into ECX, y from XMM2 into EDX |
| `0x45b89d` | 11 | position history shift: the loop's set-up, gated on the state timer |
| `0x45b8e4` | 6 | focus counter (`gate_block`, EDI, `+0x634`) |
| `0x45c63a` | 14 | blink guard |
| `0x45bd2f` | 9 | option approach |
| `0x45bd0b`, `0x45ee20` | 10, 10 | option and shot-object callbacks, boundary tick only |
| `0x45c402` | 9 | shot loop head: a shot fired this frame waits for the next boundary tick |
| `0x45c41b`, `0x45c43c` | 12, 10 | shot rates × `g_factor` |
| `0x45c4a5` | 18 | shot cadence timer, boundary tick only |
| `0x45c4ff` | 8 | shot age, boundary tick only |
| `0x45e6c9` | 5 | the muzzle step at a shot's creation, called with the logical speed |
| `0x45f144` | 8 | shot-against-enemy guard (`g_ptf_prev`/`g_ptf_cur`) |
| `0x452c10` | 7 | `timer_rewind` rate → `&g_logical` |
| `0x45f068` | 10 | shot object timer, boundary tick only |
| `0x459ab3`, `0x46aa65`, `0x461965`, `0x461ab3` | 5 | replay save and load calls |

Interpolation: `th18_enemy_sprites = {0x122c, 0x124, 0x164, 0x224, 0x4000000, 0x17c, 0x0c}`
(sub-object `+0x122c`, VM position `+0x5f0`, parent contribution `+0x30`; `0x42ff80`); four
options of `0xf0` at player `+0x670`, VM position word `0x17c`.

## 7. Dimming

`world_prio = 16` (the first `enemy.anm` draw, layer 6). From the census of the stage 3 demo:

| prio | ANM / layer | class |
|---|---|---|
| 3, 9 | `st03wl.anm` L0, `text.anm` L32, `effect.anm` L2 | background (under the quad) |
| 14, 15 | one quad (`0x455610`) and `text.anm` L35 (`0x455530`), gated on `0x4ccf9c` | background |
| 16, 20, 21, 24 | `enemy.anm` L6, L8, L9, L11 | none |
| 28, 32 | `pl00.anm` L13, L15 | `DIM_PLAYER_SHOTS` |
| 29, 31 | `pl00.anm` L14 | none |
| 28–32 | `effect.anm` L13–15 (focus ring, hitbox), `ability.anm` L13–15 (card effects) | none |
| 33 | item manager's draw (`0x446f00`), `bullet.anm` L0 | `DIM_ITEMS` |
| 38 | bullet manager's draw, `bullet.anm` L0 | none |
| 46, 47 | `bullet.anm` L20, L21 (lasers) | none |
| 35, 40, 43, 46, 47 | `effect.anm` L16–21 | `DIM_EFFECTS` |
| 60+ | `title.anm`, `abcard.anm`, `front.anm`, `ascii.anm`, `sig.anm` | interface |

## 8. Verification

All under Wine with a null ALSA device, software rendering, the title demo (`demo/demo0.rpy`,
stage 3) with `replay_trace=1`. The trace line carries the player's fixed-point position, the
bullets' count and quantised position sum, the items' count and sum, the laser count and the
player's life state; a second line fingerprints the player's shots (count, position sum, the
sums of their timers and age counters, the focus counter).

- Native harness and `test_th18_stubs.py` pass (`test.sh` with `th18.exe`, `th14.exe`,
  `th15.exe`, `th20.exe`).
- `substep=1` at 60 fps against `substep=0`: identical on every field, 3151 frames, shots
  included. The hooks are inert at one tick per frame.
- `substep=1` at 120 fps against `substep=0`: the player's position is identical for 1369
  frames, until the diverged simulation kills her; the shots' count, timers and ages agree
  frame for frame with stock apart from the hit-lag volleys (§4) and their positions differ by
  one quantum here and there; items differ by one at their exits; bullets differ by one for a
  frame or two from 328 and part for good around 1209, an enemy dying a frame earlier. Before
  the shot-type callbacks and the muzzle step were found, the bullets sat three below stock
  from frame 333 and the player died at 1258.
- A stage 1 run driven by `xdotool` (fire held, left and right for 12 s), saved from the pause
  menu and played back from the title: at 120 Hz and at 144 Hz the playback agrees with the
  recording on every field, shots included, on every frame after the stage's intro (787 and
  663 frames). The intro's 60 frames differ in the player's life state and in seven idle shot
  entries; the same run in stock mode at 60 Hz differs there too, so that is the game's own
  (its frame counter starts before the spawn animation ends, and the playback starts after).
- Dimming: `dim_background=60`, `dim_items=50`, `dim_effects=50`, `dim_player_shots=50` fade
  what they name and nothing else (screenshot under Xvfb).

On Windows (the owner's machine, 360 Hz, D3D9Ex): played and reported working, bosses and
lasers included.

Not done under Wine: lasers in play (the demo has none, the driven run reaches none), a stage
with a boss. Not done anywhere: the English executable, `test-games.ps1` on Windows.

## 9. How the port was made

TH15's listing was matched to TH18's by instruction-context voting for every TH15 site
(`tools/porting`); the player function and the shot code were then diffed body against body
because the compiler reordered them. The new per-frame logic — the position history, the
callbacks, the shot age, the muzzle step — was found from the demo trace: each left a
signature (a steady bullet gap, shots half a frame ahead, ages one behind) that a per-shot dump
on the frames around it pinned to an instruction.

## 10. Open

- `vm_script_off`; `pp` writes (the resolution dialog is the game's own, as TH15).
- The ability cards' per-frame code has not been audited for reads of the sub-stepped systems.
- Stage distortion RNG (TH11–13 gate it; not looked for here, TH14 or TH15).
- The English executable.
