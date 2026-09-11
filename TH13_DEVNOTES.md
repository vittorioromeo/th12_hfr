# TH13 (Ten Desires) port development notes

> Per-game record for the unified runtime. The shared design is in
> [ARCHITECTURE.md](ARCHITECTURE.md); what the port taught the runtime is condensed in
> [DEVNOTES_RUNTIME.md](DEVNOTES_RUNTIME.md) §7a and §8. This file holds *TH13 addresses,
> layouts, decisions and their reasons*, in enough detail to port the next game of this engine
> family (TH14 onward is TH13's engine with more changes of the same kind). The source of truth
> for every number is `src/games/th13.c` and `src/games/th13_signatures.h`; if they disagree
> with this file, the code is right and this file needs fixing.

Supported: **TH13 v1.00c**, Japanese `th13.exe` (image size `0xe9000`) and English `th13e.exe`
(`0xea000`). The English executable is the same code with an appended `.ipatch` section that
loads `th13e.dll`; every address holds for both and every frozen signature is byte-identical in
both, so `identify_image` accepts either size for one identity (`image_size_alt`). Build state:
**supported** since v0.4.0-test; the 360 Hz shot fix is v0.4.1-test (September 2026).

## 1. Engine differences from TH12, and what absorbed each

TH13 is TH12's engine (the `SpeedSite` design applied unchanged) with a handful of structural
changes. Each became a profile field read by the shared code, never a fork.

| Difference | Detail | Absorbed by |
| --- | --- | --- |
| `UpdateFunc` grew a field | callback argument at `+0x24` (TH12: `+0x20`) | `layout.node_arg = 0x24` |
| Runner keeps the next node in itself | `runner+0x50`, re-read after every callback and after every removal (TH12 kept it in a register) | `layout.runner_next = 0x50` (`next_store` / `next_load` in `update_runner.c`) |
| `remove_node` argument order | `(runner, node)` instead of `(node, runner)` | `remove_node_runner_first = 1` |
| Critical section gate | a byte flag at `0x4e49ed` (TH12: `misc_flags & 0x8000`) | `misc_flags = 0x4e49ed`, `critical_flag_mask = 0xff` |
| Runner ends at `+0x54` | | `layout.runner_ending = 0x54` |
| Replay load ABI | manager in EBX, filename in ECX, `0x448c40` | `th13_replay_load_entry` adapter (`push ecx; push ebx; call th13_replay_load_c@8`) |
| Replays and scores live in `%APPDATA%` | the game chdirs into `%APPDATA%\ShanghaiAlice\th13\` around every save/load; the string is at `0x4dd0d1` (game dir string at `0x4de0d1`) | `addr.data_dir = 0x4dd0d1`; `replay_path()` prefers it when non-empty |
| `MotionState` split into pre-step and step | pre-step (`0x4736a0`) recomputes velocity from speed/angle **times the game speed**; step (`0x473780`) adds it | **no hook** (TH12 hooked its step; here that double-scales — see §6) |
| Enemy struct reorganised | position/ids/flags in a sub-object at `+0x11ec`; every flag bit two places higher than TH12's | `th13_place_enemy`, `layout.enemy_*` (§5) |
| EnemyManager list head | `+0xb0` (TH12 `+0x68`) | `layout.enemy_list = 0xb0` |
| Timers are 16 bytes | `prev, int, float, speed*` — the speed pointer at `+0xc` as in TH10; `Timer::tick 0x401400`, `Timer::add 0x4732c0` | nothing new; the inline tick pattern is how every per-object timer below was found |
| Own F10 size cycle | TH13 cycles 640/960/1280/fullscreen itself | `native_size_cycle = 1` |
| D3DX | ships `d3dx9_43.dll` | `d3dx = "d3dx9_43.dll"` |
| Laser vtables grew two slots | update at slot 4 (TH12: slot 2) | nothing; noted so the next port does not match by vtable index |

## 2. Engine map

All addresses are absolute for v1.00c. Fields named as in `GameProfile`.

| Symbol | TH13 |
| --- | --- |
| Game speed | `0x4c0a28`, float (16 write sites, §3) |
| Update runner pointer | `0x4dc658`; next node `+0x50`, ending `+0x54` |
| Runner function | `0x470af0` (object in EBX); UpdateFunc registration helpers `0x470990` / `0x470ff0` (update) and `0x471050` (draw) |
| Remove update node | `0x470e90`, `(runner, node)` |
| Critical section / depth / byte gate | `0x4e48a8` / `0x4e49e0` / `0x4e49ed` |
| Vsync frame function / frame call sites | `0x45d570` / `0x45c5de`, `0x45c5fb`, `0x45c607` |
| Latency-sleep comparison | `0x45d69e` (7 bytes, last byte forced to `0x7f`) |
| Frame context pointer / flag / value | `0x4dcc18` / `0x4dcc1c` / `0x4dc9d0` |
| Frame duration | `0x4dcf58` |
| Scene cleanup function / object | `0x473590` / `0x4dcebc` |
| D3D device / present parameters / window flags | `0x4dc6a8` / `0x4dc794` / `0x4df0e0` |
| Raw input block / pressed | `0x4e49f0` / `0x4e49fc` (32-bit words, `0x130` bytes saved) |
| Raw input poll | `0x471620` |
| Game input / pressed / released | `0x4e4c08` / `0x4e4c14` / `0x4e4c18` |
| Autofocus counter / option flags | `0x4e4b84` / `0x4dc8a8` |
| GameManager pointer / callback | `0x4c2194` / `0x42cb90`; pause flags `+0x60` |
| Player pointer / callback | `0x4c22c4` / `0x443de0` (priority `0x12`) |
| EnemyManager pointer / callback | `0x4c2188` / `0x418ef0` (priority `0x15`); list `+0xb0`; boss/current enemy `+0x5c`; manager timer `+0x98/+0x9c/+0xa0` |
| AnmManager pointer / get-VM-by-id | `0x4dc688` / `0x46fb90` (`push id; edx = manager`) ; `AnmVm::update 0x462450` |
| ReplayManager pointer | `0x4c22c8`; mode `+0x10`, stage pointers `+0x20 + 4*s`, stage frame `+0x210`, stage number `+0x218` |
| Replay record / playback node | `0x448e30` / `0x448e40` |
| Replay save (native) | `0x4484d0`, `fastcall(filename, name)` + `push 1`; call sites `0x43f26b`, `0x440280`, `0x4412e0`, `0x454e15` |
| Replay load (native) | `0x448c40`, EBX = manager, ECX = filename; playback call site `0x447c1b` |
| Screenshot routine / call site | `0x43a950` / `0x45d856` (filename in EAX) |
| ftol / angle normalise / polar | `0x4971f0` / `0x472f30` / `0x474f00` |
| Player-vs-hitbox test / graze | `0x444260` (returns 1 hit, 2 graze) / `0x445900` |
| Enemy vs player shots | `0x446870` (§6) |
| ECL variable getters | int `0x420380`, float `0x420d00` (jump table at `0x420a04`, index = id + 10000) |
| Score / difficulty | `0x4be7c0` / `0x4be7c4` |
| Data directory / game directory strings | `0x4dd0d1` / `0x4de0d1` |

Identity: magic `t13r`, no legacy INI, 96 frozen signatures (`tools/th13_signatures.json`).

### vpatch conflict sites (`src/games/th13_conflicts.h`)

From the immediates in `vpatch_th13.dll` that land in code: `0x45d2f6` (frame limiter),
`0x42499b` (replay timing), `0x45d4b6` (Present call), `0x45de73` (the frame timing check after
Present). vpatch's TH13 build is loaded by its own `vpatch.exe` launcher, so the module check
catches it as well.

## 3. Speed sites (all 6-byte `fstp [0x4c0a28]`, `pop_float = 1`)

| Op | Sites |
| --- | --- |
| `SPEED_ONE_PERM` | `0x42ba24` stage start, `0x42bfb5` game start, `0x43a04b` supervisor reset, `0x443586` player death, `0x474f6c` speed object init |
| `SPEED_ONE_TEMP` | `0x40679d` stage 3D update, `0x4624c7` "unaffected by slow-motion" sprites |
| `SPEED_PAUSE_SET` | `0x43e572`, `0x43e6e4`, `0x43f702`, `0x43f843` |
| `SPEED_PAUSE_RESTORE` | `0x43e743`, `0x43f8d9`, `0x440949`, `0x4407ab` |
| `SPEED_ECL` | `0x41f24b` |

## 4. Node classification (from the registration scan, `tools/porting/scan_registrations.py`)

| Callback | Prio | Mode | Name |
| --- | --- | --- | --- |
| `0x40e780` | `0x17` | SUB | BulletManager |
| `0x443de0` | `0x12` | SUB | Player |
| `0x44ad40` | `0x11` | FRAME | Bomb |
| `0x42efb0` | `0x18` | SUB | ItemManager |
| `0x42fe30` | `0x16` | SUB | LaserManager |
| `0x438e70` | `0x19` | FRAME | Gui |
| `0x407680` | | SUB | Stage |
| `0x46f360` | `8` | SUB | AnmManagerWorld |
| `0x46f330` | | SUB | AnmManagerUI |
| `0x413250` | `0x1a` | FRAME | Spellcard |
| `0x418ef0` | `0x15` | FRAME | EnemyManager |
| `0x403d60` | `4` | FRAME | Effects |
| `0x42cb90` | | FRAME | GameManager |

Player (`0x12`) runs before EnemyManager (`0x15`), which matters for every guard in §6.

### Draw callbacks (the other list; from a `debug=1` trace, see DEVNOTES_RUNTIME §3b)

The draw runner is `0x470c30` (list at manager+0x40, dispatch `0x470c9e`), the sprite batch
flush `0x4679a0` (ESI = AnmManager), the sprite VM draw `0x46a700` (VM in EAX; loaded-ANM
pointer at VM+0x30, `slot << 16 | sprite` at +0x34, layer at +0x24, script index at +0x4aa; ANM slots: 0 text, 5
front, 7 bullet, 8 effect, 9 the player, 10 enemy, 25 astral). Priorities are decimal here. `L n` is the AnmManager's
layer thunk for sprite layer *n*; only free-standing VMs live in those lists — the managers
below draw their own VMs (24 callers of the VM draw `0x46a700`), which is why "bullets are
layer 15" is true of the scripts and useless for attributing draw calls.

| Prio | Callback | What |
| --- | --- | --- |
| 1 | `0x43c4b0` | Stage: binds the offscreen stage target, 3D viewport (116,2 408x476) |
| 2..9 | `0x403910`-ish / L0..L3 | 3D stage (VB, FVF 0x102), layer 2 = petals in 3D mode |
| 10 | `0x413260` | Spellcard: spell background |
| 11 | `0x46ed80` | AnmManager: world frame context |
| 12 | `0x43c600` | Stage: binds the world target — **`world_prio`**: the dim quad goes just before this, into the finished stage |
| 14 | `0x43c870` | Stage: copies the stage target into the world target (ONE/ZERO) |
| 15 | L6 | the divine spirits (`astral.anm`, additive) and bullet cancels; in trance, the stage texture re-blended DESTCOLOR/INVDESTCOLOR |
| 16 | `0x40e7e0` | BulletManager (back layer) |
| 21 | `0x418f30` | EnemyManager |
| 22 | L11 | the player's shots and options (`pl0X.anm` layer 11; the body draws at 23 with no layer) |
| 24..25 | L12, L13 | layer 12 is the hitbox (`pl0X.anm`) and the focus ring (`effect.anm`), excluded from every class; 13 more shots |
| 26 | `0x42eff0` | **ItemManager** (items rule) |
| 27 | `0x438eb0` | Gui |
| 29 | `0x42fea0` | LaserManager |
| 31 | `0x40e7b0` | BulletManager |
| 35..37 | L16, `0x40a2b0`, L17 | effects, hit markers |
| 38..42 | `0x43c6a0` `0x43c920` `0x43c740` `0x43c9d0` | Stage: world → stage target → world (effects), fullscreen copies |
| 44..71 | | interface, into the back buffer (`0x43c7e0`/`0x43ca50` at 52/53 do the final copy) |

## 5. Object layouts

**Player** (`0x4c22c4`): position `+0x5b8/+0x5bc`, state `+0x65c`, state timer
`+0x664/+0x668/+0x66c` (speed pointer `+0x670` → game speed; ticked inline at `0x443a8d` at the
end of the update, on every path), flags `+0x14698`, option gather counter `+0x1469c`,
option array `+0xa318` (8 entries), shot array `+0xaa5c`, 256 entries of `0x9c` bytes.

**Player shot entry** (`E` = entry; the update loop keeps `EDI = E + 0x68`): flags `+0` (bit 0
active, bit 1 circular hitbox, bit 2 "counts for the bomb/graze flag"), rate pair `+4 += +8`,
angle `+0xc += +0x10` (normalised), `MotionState` at `+0x1c` (position `+0x1c`, speed `+0x34`,
angle `+0x38`, velocity `+0x50`, mode bits `+0x5c`), countdown timer `+0x60/+0x64/+0x68` with
speed pointer `+0x6c`, damage `+0x74`, damage dealt `+0x78`, damage cap `+0x7c`, hit interval `+0x80`, hit callback `+0x88`.
Motion is the pre-step/step pair; the rate pair and angle are scaled by the factor
(`0x443691`); the countdown timer is ticked once per frame (`0x4436b4`, §6).

**Bullet** (`0x40db10`, EBX): flags `+0x20`, wait counter `+0x24`, ANM VM `+0x28`, second
counter `+0xbac`, timer `+0x133c/+0x1340/+0x1344`.

**Item** (`0x42e380`, ESI): VM `+0x10`, state `+0xba0`, y velocity `+0xbac` (gravity `+0.2`
per frame from `0x4aebd0`), state-5 countdown `+0xbb0`.

**Laser** (line `0x4315d0`, curve `0x4355f0`, beam `0x433470`; manager list `+0x5d0`, count
`+0x5d4`): per-object timer `+0x14/+0x18` (ticked inline by the manager after each update),
graze timer `+0x28/+0x2c` (line and curve), ex-wait counter `+0x5b4`, flags `+0x598`.

**AnmVm**: timer `+0x538/+0x53c`, position `+0x574`, offset position used by children `+0x3c`.

**Stage** (`0x406680`, EBX): distortion object `+0x406c`, distortion frame counter `+0x4068`.

**Enemy** (list node `{enemy, next, prev}` at manager `+0xb0`): sub-object at `+0x11ec` (`in`)
with position `in+0x44` (= enemy `+0x1230`), VM ids `in+0x120` (14 slots), sprite offsets
`in+0x168` (float triples), parent slot of each sprite `in+0x220`, flags `in+0x4030` (= enemy
`+0x521c`; `0x04000000` being deleted, `0x08000000` sprites positioned absolutely), hp
`+0x5138`. The ECL variable getter (`0x420380`) is the quickest way to this table: each case is
one field. Sprite placement (`0x41a8a0`) is `vm.pos(+0x574) = pos + offset[i] (+ parent
vm+0x3c)` with **no** (224,16) playfield offset — TH13 keeps enemies in playfield coordinates.

**AnmManager sprite quad builder** (`0x467350`, `this` = AnmManager, EAX = VM, stack arg =
mode): adds the VM position to the four corner vertices at `0x4e47d8..0x4e4830`, and when the
mode argument has bit 0 (`0x467d80`, the plain 2D draw — nearly every sprite) rounds each corner
with `frndint` at `0x4673f9`, `0x467407`, `0x467415`, `0x467423` before subtracting the half
texel. The AnmVm draw dispatch (`0x46a700`) selects the builder by `flags >> 25 & 0x1f`; modes
that pass 0 or 2 (rotated, 3D) never round; the vertex-list mode (`0x46a8b0`, lasers and
meshes) draws a strip from `vm+0x58c` with `vm+0x4ac` quads. Batches flush through
`0x4679a0` as a `DrawPrimitiveUP` triangle list, stride `0x1c` (`XYZRHW|DIFFUSE|TEX1`). These
are the `sprite_round_sites` for `video.internal_scale` (DEVNOTES_RUNTIME §3a).

**Timer** (16 bytes): `prev, int, float, speed*`. The inline tick is
`mov edx,[+4]; mov ecx,[+0xc]; mov [+0],edx; fld [ecx]; fcomp 0.99; ...; fcomp 1.01; ...` —
integer path (`int++`, `float += 1`) when the speed is within 1%, otherwise `float += speed;
int = ftol(float)`. Grep for that shape to find every per-object timer.

## 6. Per-object hooks (`th13_install_sites`), with the reasons

- **Replay load adapter** at `0x447c1b` (EBX/ECX ABI, §1).
- **Shot array rates** `0x443691` (16 bytes): `+4 += +8` and the normalised angle
  `+0xc += +0x10` are per-frame rates outside the `MotionState`; scaled by the factor.
- **Shot countdown timer** `0x4436b4` (9 bytes): ticked by the logical speed on the boundary
  tick, untouched on minor ticks. This is the 360 Hz fix. The enemy hit test counts a shot only
  when this timer's integer changed on the last tick and `int % interval == 0`; the enemy code
  runs on the boundary tick only; a sub-stepped timer's integer changes on whichever tick the
  float crosses a whole number, and at rates where `dt` is inexact in float32 (`1/6` at 360 Hz)
  that was never the boundary tick. At 120/240 Hz (`dt` exact) it aligned by luck, which is why
  the rig passed. TH12's test is the inverse (it *skips* on that tick), so TH11/TH12 do not
  need this.
- **Enemy hit-test guard** `0x446888`: "player state timer unchanged → no damage" replaced by
  the runner's float comparison across the last Player update (`g_ptf_prev` / `g_ptf_cur`),
  as in every game.
- **Movement residual** `movement_ftol(0x442d9b / 0x442dae, ftol 0x4971f0)`.
- **Death particles** `0x44341c` (`cmp [esi+0x668],3`): once per frame on the player's timer.
- **Option gather counter** `0x442f34` (`inc [edi+0x1469c]`): once per frame on the player's timer.
- **Bullet wait counters** `0x40e1fa`: both decrements once per frame on the bullet's timer.
- **Item gravity** `0x42e6fc`, `0x42e7be`: `+0.2` per frame → `+0.2 × speed`.
- **Item state-5 countdown** `0x42e3dd`: once per frame on the boundary (state-5 items do not
  tick their timer). The five `fstp` after it do not touch flags; `cmp esi,esi` leaves SF clear
  for the game's `jns`.
- **Laser ex-wait** `0x431728` (line, EDI), `0x435761` (curve, ESI), `0x4334c6` (beam, EDI):
  once per frame on the laser's `+0x14/+0x18` timer.
- **Line laser graze** `0x431a01`: once per frame on the graze timer `+0x28/+0x2c`. The curve
  laser computes the same `% 3` test and then does nothing with it; the beam has no graze
  branch; neither is hooked.
- **Stage distortion** `0x4067e4` (14 bytes) and its frame counter `0x406d6f` (11 bytes): the
  effect consumes RNG every frame; run on frame ticks only.
- **Constant `Timer::add` sites** `0x44647c` (player shot cycle `-14`) and `0x4629ef` (the ANM
  `wait N`, inlined into `AnmVm::update` — TH12 had a helper): `value × logical` instead of
  `value × logical × dt`. The other 18 `Timer::add` callers pass `-1.0` (rates) and are left alone.
- **Sprite corner rounding** `0x4673f9`/`0x467407`/`0x467415`/`0x467423` (`frndint` → NOP, only
  with `internal_scale > 1`): lets sprites sit on sub-pixel positions at the higher internal
  resolution. Screen captures go through `D3DXLoadSurfaceFromSurface` with a 640x480 source
  rect, scaled by the D3DX import hook.
- **Enemy death ring** `0x415e59`: shrink/fade once per frame on the effect's `+0xc/+0x10` timer.
- **Scrolling mesh VM callback** `0x46b9d0`: UV scroll once per frame on the VM's `+0x538/+0x53c`.

**Not hooked, deliberately.** `MotionState::step` — the pre-step already multiplies the
velocity by the game speed; the port's first version scaled the step too and moved every shot
and bullet at `dt²`. The player's `state_timer % 60` block (gone in TH13); the new every-3-frames
block at `0x443792` is guarded by the game itself (`int != prev && int % 3 == 0`, evaluated
inside the sub-stepped Player, where a once-per-frame integer change is exactly right). The UFO
attraction hook (no UFOs). The `0x40deac` bullet counter inside the script-wait loop (TH12 left
its equivalent alone too).

## 7. How the addresses were found (for TH14 and later)

Tooling is in `tools/porting/` with a README. What worked, in order of usefulness:

1. **Registration scan** for the node table: every `call` to the UpdateFunc registration helper
   with the `mov ebx, prio` and callback store before it. Cross-check with `new` sizes and the
   global each constructor stores into.
2. **The ECL variable getter** for the enemy layout, and the equivalent switch for the player
   (`-9991`/`-9990` read the player position) — one table, no guessing.
3. **The inline `Timer::tick` shape** for every per-object timer, and `Timer::add` callers
   for the constant-argument sites.
4. **Reading the TH12 hook's meaning** (object, field, gating timer) and finding the same
   operation in the decompiled TH13 function, rather than matching bytes. Instruction-sequence
   matching (`match.py`) found thunks and small helpers; decompiled-body similarity
   (`dmatch.py`) only narrowed the function.
5. **Ghidra headless** (11.3.2) for a full decompilation dump (`decomp13.c`), then `grep`.

What did not work: matching hook sites by byte shape across the two games; assuming a struct
shift is uniform (the enemy's ids moved by `+0x1ec`, its position by `+0x1bc`, its flags by
two *bits*); assuming an old hook still has a counterpart (§6's "not hooked" list).

## 8. Rig and validation record

- Rig: Wine 9 under Xvfb `:77`, `WINEPREFIX=/tmp/wp`, `WINEDLLOVERRIDES="dinput8=n,b;d3dx9_40=n"`,
  `th13.exe` with `th13.dat` and a sparse `thbgm.dat`; `th13e.exe` additionally needs
  `th13e.dat` and `th13e.dll`. The game's own dialog picks the window size (radio buttons at
  x=566, y≈498/510/522/534 for the four sizes at the rig's dialog placement; OK at 636,583).
  The rig renders at 25–35 presents/s; every present slot then runs one tick plus one catch-up
  tick, so sub-stepping *is* exercised (dt as configured) but wall-clock is ~0.6× real time.
  Replays under the rig land in `/tmp/wp/drive_c/users/root/AppData/Roaming/ShanghaiAlice/th13/`.
- Harness: `./test.sh th13.exe` and `th13e.exe` — 92 signatures, 52 patches, no overlaps;
  TH10/TH11/TH12 unchanged by the runner generalisation (the `INTERNAL ERROR: missing
  signature @004737a2` line the harness prints is its own negative test, not a fault).
- Live under the rig at `fps=120`, `240` and `360`: stage 1 gameplay, hits and kills, items,
  bombs, deaths, pause menu, replay save to `%APPDATA%` with the HFR chunk, playback with
  per-tick input applied.
- Owner's machine (360 Hz): v0.4.0 played but no shot hit an enemy — §6, fixed in v0.4.1.

Not yet exercised: bosses and lasers (stage 2+), spell practice, the Extra stage, exclusive
fullscreen, `th13e.exe` on Windows with `th13e.dll`, long sessions (float-timer accumulation
at non-exact `dt` is the same exposure every game has under this design).
