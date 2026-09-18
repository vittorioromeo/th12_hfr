# Touhou 14 — Double Dealing Character

Record of the port, written as it was made. The executable read here is the Japanese
`th14.exe`, v1.00b: ImageBase `0x400000`, SizeOfImage `0x101000`, entry `0x487453`,
TimeDateStamp `0x520dc559`. x86, Direct3D 9, `d3dx9_43.dll` — the same family as TH10–13, so
this is a profile and not a backend.

## 1. State

Described: identification, the whole video path, and the scheduler. The game runs at the
display's rate with its simulation at 60 Hz.

Not described: the UpdateFunc class table, the dimming map, replay extension, sub-tick input
and the per-frame hooks sub-stepping needs. The class table being empty is not a gap that
misbehaves — `node_mode` answers `MODE_FRAME` for a callback it does not recognise, so every
system runs exactly once per 60 Hz tick — and `install()` says so in the log.

## 2. The frame path

Read out of the binary, and cross-checked against TH13's shape site by site.

| | TH14 | TH13 |
|---|---|---|
| update runner | `0x401280`, ends `0x40138a` | `0x470af0`, ends `0x470c04` |
| draw runner | `0x4013b0`, ends `0x40149a` | |
| `update_runner` (the global) | `0x4db51c` | `0x4dc658` |
| `remove_node` | `0x401630` | `0x470e90` |
| frame calls | `0x469a27`, `0x469a45`, `0x469a51` | `0x45c5de`, `0x45c5fb`, `0x45c607` |
| `frame_fn` | `0x46a950` | `0x45d570` |
| `latency_cmp` | `0x46aa80` | `0x45d69e` |
| `device` | `0x4d8f68` | `0x4dc6a8` |
| `window_flags` | `0x4f7a54` (fullscreen bit `0x40`) | `0x4df0e0` (bit `0x10`) |
| critical section / count / gate byte | `0x4f56d0` / `0x4f5808` / `0x4f5815` | `0x4e48a8` / `0x4e49e0` / `0x4e49ed` |
| `raw_input` / `raw_pressed` | `0x4d6878` / `0x4d6884` | `0x4e49f0` / `0x4e49fc` |
| `replay_manager` | `0x4db688` | `0x4c22c8` |
| screenshot routine / call | `0x445000` / `0x46abf6` | `0x43a950` / `0x45d856` |

`node_arg` is `+0x24` and `runner_next` is `+0x50`, both as TH13.

The three frame calls sit in one `if/else if/else` at `0x469a03`, exactly as TH13's do: the
first is the automatic-latency path, the second the frame function this profile describes, the
third the window manager's fast update. The frame function calls the update runner at
`0x46a99e` (`mov ecx,[0x4db51c]; call 0x401280`), which is where `runner_fn` and
`update_runner` are read off each other rather than guessed separately.

`latency_cmp` is `cmp byte [0x4d9159],1` inside `0x46aa70`, a small routine the frame function
calls rather than the inline block TH13 has. The patch turns the `1` into `0x7f`, so the
comparison never matches and the game's own Sleep-based limiter never runs.

## 3. Four calling conventions changed

TH14 is TH13's engine rebuilt with a newer MSVC, and the rebuild changed how four things are
called. This is the reason the game needed backend work and not only a profile, and it is worth
recording as a checklist, because TH15 onwards will differ again.

1. **The update runner takes its object in ECX** (`mov ebx,ecx` at `0x40128e`). TH11–13 have it
   already in EBX; TH10 takes it on the stack. `runner_arg` names which, and
   `update_runner.c` has a thunk for each.
2. **The three frame functions are thiscall** — `mov ecx,0x4f5a18; call ...`, and each ends in a
   plain `ret`. TH10–13 push the context and the callee returns with `ret 4`. `hfr_frame` is
   `__stdcall`, so wiring it into a TH14 call site unchanged would have read a garbage context
   and lost four bytes of stack every frame. `frame_ctx_ecx` selects a `fastcall` wrapper —
   with one pointer argument, fastcall and thiscall are the same code.
3. **`remove_node` is a method**: the runner in ECX, the node pushed, `ret 4` at `0x4016e4`.
   TH10–12 pass `(node, runner)` in ECX/EDX and TH13 swapped them, so this is a third form and
   `remove_node_abi` is an enum rather than the boolean it replaced.
4. **The screenshot routine is stdcall with the filename pushed** (`lea eax,[ebp-0x108]; push
   eax; call 0x445000`), where TH10–13 pass it in EAX and the stub could ignore it entirely.
   `screenshot_stack_arg` makes the stub forward the argument and clean it up.

The first three are all "the compiler started using ECX". The fourth is the same change one
level down. Reading any one of them as a one-off would have been a mistake; they are the same
event.

## 4. thprac

thprac's TH14 support hooks `0x40138a` for its update pass and `0x40149a` for its render pass —
the last instruction of each runner, exactly as in TH10–13. That is the one-instruction
conflict §5e of the runtime notes is about, and it is the reason `runner_ret` is filled in here
from the first commit rather than after someone reports a practice menu that does not open.
Not yet verified against a running game.

## 5. The first run: a null write in set_factor

The first build faulted before the title screen, at `hfr_runner+…`, `movss [edx],xmm0` with
EDX zero, reached from the frame function's `call 0x401280` at `0x46a99e`. That is
`set_factor` storing the game speed through `addr.speed`, which this profile does not describe
yet, once per node per tick.

The harness had already caught it — and it was silenced. `test_runner` drives the runner
through the profile's own globals, faulted on the same instruction for the same reason, and
the response was to hand the fixture a piece of memory to point at instead of asking why a
described game never hit it. A fixture convenience was written where a guard belonged, and the
signal it was giving was thrown away. `test_runner_undescribed()` is the test that should have
been written: every optional address zeroed, both sub-step switches on, a boundary tick and a
minor tick, and it fails on the instruction above if the guard is removed.

What the guards do now:

- `set_factor` keeps tracking the factor and does not make the write when `addr.speed` is zero.
- `subtick_active` additionally requires `poll_input` and `game_input`, which it calls and
  writes through.
- `install()` switches off what the profile cannot support rather than leaving it on and
  inert: with no class table, `substep` goes off, so the logic rate stays at 60 rather than
  leaving it and taking minor ticks no system can use; with no input path, `subtick_input`
  goes off. The log said `substep=1` and `logic rate: 360 ticks/s` on a game where nothing
  could be sub-stepped, which is a claim the patch should not make about itself.

The general rule ADDING_A_GAME.md already states -- "anything a profile leaves out degrades
rather than breaks" -- was true of the install path and not of the run path.

## 6. The second run: the catch-up tick

It then started, ran, and faulted a moment later on `mov [edx],ecx` in `hfr_frame`, with EDX
and ECX loaded from the profile at `+0x78` and `+0x80` -- `frame_context_ptr` and
`frame_context_value`. That is `update_only_tick`, the extra update the frame hook makes to
catch up after a hitch, and the log shows the hitch that triggered it: 293 ms between presents.
It sets up the game's frame context by hand through three addresses and may call the game's own
end-of-pass cleanup through two more, none of which this profile has.

Fixing one address at a time was clearly not working -- two builds, two faults, both found by
reading a fault address out of a log and disassembling our own DLL. So the pattern is fixed
rather than the instance:

- `update_only_tick` declines when its five addresses are not all there, and the frame hook
  stops asking for more than one tick a frame once it does. A hitch then simply is not caught
  up, which at a 60 Hz logic rate costs a frame nobody sees.
- `window_pump` leaves the swap chain alone rather than resetting it from `pp` it does not
  have. That path only runs with scaling off, which is why it had not been hit yet.
- **`install()` now audits the profile by name.** One table of "this address, this feature",
  walked once at startup, logging each gap. For TH14 today it prints six lines -- `speed`,
  `frame_context_ptr`, `cleanup_fn`, `poll_input`, `pp`, `player` -- which is the list that
  would have replaced both of these debugging sessions with a glance at the log.

The test goes with it: `test_runner_undescribed` now also calls the catch-up tick with those
five zeroed and asserts it declines, and asserts that a described game still has it. Removing
the guard page-faults the harness, which was checked both times rather than assumed.

## 7. The third run: a switch the game could not honour

The third build started, ran at 360 Hz, and then crashed inside `th14.exe` itself rather than
in the patch. The log says why, two lines earlier: `menu: tick rate -> 360 (substep=1)`. The
sub-step toggle was still there to be pressed.

`install()` had turned `substep` off, but off is not the same as unavailable. With it back on
the logic rate leaves 60 and the runner starts taking minor ticks -- and with no classified
systems every node is `MODE_FRAME`, so a minor tick walks the whole list and calls none of it.
Five presents out of six then draw from state the game's own update never advanced, and it is
the game that faults, not us.

So availability is now a property of the profile and not of the setting, the way it already was
in the New Classic backend:

- `UI_SUBSTEP_AVAILABLE` answers `class_count != 0` and `UI_SUBTICK_AVAILABLE` answers whether
  `poll_input` and `game_input` are described. The menu greys both out and says why.
- The setters refuse them anyway, so an ini, a replay's recorded settings or a stale value
  cannot turn on what the profile cannot support.
- `recompute_rate` will not leave 60 without classified systems, which is the last line: even
  if something set the flag, the rate cannot follow.

The lesson, and it is the same one three times in a row: **a half-described profile must make
the features it cannot support unavailable, not merely switched off.** Anything reachable from
the menu is reachable.

## 8. The fourth run: the debug line itself

With `debug=1` the fourth build reached a stage and then faulted in `hfr_frame` on
`mov (%edx),%edx` with EDX from the profile at `+0x3c` -- `game_input`. That is the stats
line's `input=%08x`, which only prints with debug on and only when the game has a replay
manager, which is why the first three runs never reached it.

This one is fixed at the bottom rather than at the use. Every input word the runtime touches
goes through `input_read` / `input_write`, so those answer "no bits held" and drop the write
when the address is zero; that covers the game input word, the pressed and released edges and
the autofocus counter in one place. `option_flags` is guarded at its one use, and `game_input`
joins the install-time audit.

The test that goes with it cost two attempts, both instructive. The first called
`limiter_stats` with the fixture's replay manager left NULL, so the line was never reached and
the test passed with the guard removed. The second used a one-second window when the block runs
every five. A test that does not fail without the fix is not a test, and checking that it does
is not optional -- it caught both.

## 9. The update list

The callback census (`debug=1`) on a first stage. Priority is the registration priority; the
call counts are cumulative over the sample, so the ones that appear late are the ones a stage
creates.

| priority | callback | seen |
|---|---|---|
| 1 | `0x444890` | always |
| 3 | `0x4447b0` | always |
| 4 | `0x40b8e0` | always |
| 6 | `0x459f30` | always |
| 8 | `0x47e7f0` | always |
| 27 | `0x41ee80` | always |
| 29 | `0x47e7c0` | always |
| 9 | `0x448bd0` | in a stage |
| 11 | `0x436d70` | in a stage |
| 12 | `0x455e40` | in a stage |
| 13 | `0x40eb70` | in a stage |
| 17 | `0x457ee0` | in a stage |
| 18 | `0x44ec60` | in a stage |
| 20 | `0x411eb0` | in a stage |
| 21 | `0x422a60` | in a stage |
| 22 | `0x43a6a0` | in a stage |
| 23 | `0x417610` | in a stage |
| 24 | `0x439750` | in a stage |
| 26 | `0x41cb50` | in a stage |
| 28 | `0x431a40` | in a stage |
| 30 | `0x455e60` | in a stage |

`0x47e7c0` and `0x47e7f0` are an adjacent pair registered from adjacent sites (`0x47aa5b`,
`0x47aac8`), which is the shape of TH13's two ANM managers (`0x46f330`, `0x46f360`). That is a
resemblance and not yet a reading; nothing is classified on it. Every one of these is
`MODE_FRAME` until it has been identified from its own code, because a callback put in the
wrong class is §7 of the runtime notes -- a system stepped six times a frame with its own
timers still counting in whole ones -- and that failure is silent.

## 10. Identifying the systems: method, and where it stops being safe

The class table is the remaining feature work, so it got a real attempt. Three independent
lines of evidence, and they are worth writing down because the next game will want them.

**The registering constructor's ANM files.** Each system's constructor loads its sprites by
name and registers its update callback, so the string references in the function that contains
the `mov [esi+8], <callback>` store name the system. `tools/`-style scan of the census:

| priority | callback | registered in | strings there |
|---|---|---|---|
| 4 | `0x40b8e0` | `0x40b530` | `ascii.anm`, `ascii_960.anm`, `ascii_1280.anm` |
| 6 | `0x459f30` | `0x459620` | `title.anm`, `title_v.anm` |
| 22 | `0x43a6a0` | `0x43a350` | `bullet.anm` |
| 23 | `0x417610` | `0x416110` | `bullet.anm` |
| 27 | `0x41ee80` | `0x41eb80` | `bullet.anm`, `effect.anm` |
| 28 | `0x431a40` | `0x42ea30` | `front.anm` |
| 8, 29 | `0x47e7f0`, `0x47e7c0` | both `0x47a780` | -- |

**Normalised code shape against TH13's named callbacks.** Same source, different compiler, so
immediates and absolute displacements are masked and the mnemonic/operand sequence compared.
One trap: these callbacks are thin thunks that end in a `jmp` to the real body, and a shape
that stops at the first `jmp` compares wrappers with wrappers -- three different TH14
callbacks scored 1.00 against TH13's Stage before the matcher followed the tail jump.

**Registration priority.** Useful for ordering, not for identity: TH13's BulletManager is
priority 23 and TH14's bullet-ish callback is 27, so the numbers shifted.

Where that leaves it:

- `0x417610` (priority 23) is **BulletManager** -- 0.91 shape match against TH13's, and its
  constructor loads `bullet.anm`. Two independent signals.
- `0x47e7f0` and `0x47e7c0` are the two **ANM managers**, registered from one function as in
  TH13, world and UI respectively (0.62 on the world one).
- `0x43a6a0` is probably **LaserManager** -- 0.52, and lasers draw from `bullet.anm` too.
- Everything else is a guess, and a guess here is not worth having.

### Why nothing was classified at first, even the certain ones

Two reasons, and the second is the one that matters.

`BulletManager` as `MODE_SUB` is not "sub-stepped bullets"; it is sub-stepped bullets *plus*
the per-frame hooks that keep everything the callback touches once a frame counting in whole
frames. TH13 needed three hundred lines of those (§ its own notes: the shot array's two rates,
the countdown whose integer drives the hit cadence, the death particles, the option counter,
the bullet wait counters, item gravity). Without the equivalent for TH14, classifying the
bullet manager would step it six times a frame with its own timers still counting in ones,
which is §7 of the runtime notes and is silent.

And classifying anything at all makes `class_count` non-zero, which makes sub-stepping
available again in the menu -- the state the third build crashed in. So the table stays empty
until the systems it names have the hooks that make them safe to step.

### One more dependency, now checked

The same reasoning caught a second trap before it was written. `addr.speed` is
**`0x4d8f58`** -- identified from the save/override/restore pattern at `0x424780`, which is
TH13's `SPEED_ONE_TEMP` shape exactly, and it has twelve write sites. It is *not* in the
profile, because the runtime writes the game speed every tick and the game writes it too, and
those two only compose because every one of the game's writes is a described speed site that
folds the runtime's factor in. A profile with `speed` and no sites would hold the game at 1.0
and quietly disable its own slow-motion and pause. `install()` now says so if anyone tries.

## 11. The catch-up tick and the draw path

Two more pieces, both read the same way: find where TH13 does the thing, find the same shape
in TH14, freeze the instructions it was read from.

**The frame context.** `update_only_tick` reproduces what the frame function does before it
runs the update list. TH13: `mov edi,0x4dc9d0; mov [0x4dcc18],edi` and a `1` in `0x4dcc1c`.
TH14: `mov [0x4d9640],0x4d93e8` at `0x46a965` and `mov [0x4d9644],2` at `0x46a994`. So
`frame_context_ptr=0x4d9640`, `frame_context_value=0x4d93e8`, `frame_flag=0x4d9644` -- **and
the flag value is 2, not the 1 the runtime had hard-coded**. Checked rather than assumed: the
stores to that word across the binary pair `0x4d93e8` with `2` every time, exactly as TH13's
pair `0x4dc9d0` with `1`. It is now a profile field, because a wrong value there does not
crash, it tells the engine it is running in a context it is not.

**The cleanup.** `mov ecx,0x4d98ec; call 0x403bb0` at `0x46a9a7`, against TH13's
`mov esi,0x4dcebc; call 0x473590`. Same function, different register -- the fourth thing this
rebuild moved into ECX -- so `cleanup_this_ecx` joins the other three.

With those the catch-up tick works, and the audit's six lines are down to four.

**The draw dispatch.** `mov ecx,[edi+0x24]; mov eax,[edi+8]; call eax` at `0x40141a` in the
draw runner, the same three instructions TH13 has at `0x470c9e` with the node in ESI. The
sprite batch flush and its manager come off the frame function's first act
(`mov ecx,[0x4f56cc]; call 0x475eb0`), which is where TH13's were read from too
(`mov esi,[0x4dc688]; call 0x4679a0`).

`world_prio` is left at zero on purpose. Which priority the world starts at is read off a
running game, and a wrong one fades the wrong half of the screen. Zero makes the menu report
dimming as unavailable -- but the dispatch is still wrapped, and **that is what makes the debug
draw trace run**. Which is the point: the trace prints what draws at each priority, and that is
the evidence the class table in §10 is waiting for. Describing the draw path is how the update
path gets identified.

## 12. The draw list, and what it settles

With the dispatch wrapped, a stage's draw trace gives the other half of the picture: every draw
callback with its priority, and what each one actually put on screen. Nothing was corrupted by
the wrap, which is the first thing that trace had to establish.

Draw callbacks pair with update callbacks by address -- each manager's two thunks sit together,
usually 0x10 apart -- so the two lists compose:

| system | update (prio) | draw (prio) | what the draw trace shows |
|---|---|---|---|
| text / HUD digits | `0x40b8e0` (4) | `0x40b900` (72) | 3796 prims, one texture, full screen |
| stage / background | `0x436d70` (11) | `0x436d80` (2) | drawn first, into the offscreen target |
| bullets | `0x417610` (23) | `0x417640` (35) | 1316 prims, one texture, in the play area |
| player | `0x44ec60` (18) | `0x44ec70` (28) | ~10 quads, one texture |
| enemies | `0x439750` (24) | `0x439780` (31) | 270 prims, one texture |
| GUI | `0x431a40` (28) | `0x431a50`, `0x431a60` (48, 45) | `front.anm` in its constructor |
| replay record / playback | `0x455e40` (12), `0x455e60` (30) | `0x455eb0` (62) | -- |
| ANM managers | `0x47e7f0` (8), `0x47e7c0` (29) | the `0x47e0f0`..`0x47e550` layer callbacks | -- |

**`0x417610` is the bullet manager on three independent signals now**: a 0.91 code-shape match
against TH13's, `bullet.anm` in its registering constructor, and 658 sprites from one texture
in the play area on a stage frame. `0x436d70` is the stage on two (0.59 shape, drawn first).
`0x40b8e0` is the text renderer on two (`ascii.anm`, and 3796 prims of glyphs).

The replay pair is worth the note: `0x455e60` is the playback node because OpenInputLagPatch
patches `0x455e82`, twenty-two bytes into it, to skip replay speed control.

Rendering goes through two offscreen targets and only reaches the game's own from priority 52,
which is why every world draw in the trace reports an offscreen viewport.

## 13. `vm_draw`, and the fifth thing that moved

The per-VM draw is the lever that would finish both remaining pieces: it is what makes the
trace print each VM's ANM file and sprite layer, which is what the dim rules are written
against *and* what would confirm the systems above by name rather than by prim count.

It is `0x478f60` -- 0.71 against TH13's `0x46a700`, same prologue, and called both from the
ANM manager's layer worker and from each system's draw. But:

```text
TH13 0x46a700:  push ebp; mov ebp,esp; and esp,-8; sub esp,0x1c; push ebx
                mov ebx,eax                 <- the VM arrives in EAX
                mov eax,[ebx+0x594]
TH14 0x478f60:  push ebp; mov ebp,esp; and esp,-8; sub esp,0x18; push esi
                mov esi,[ebp+8]             <- the VM arrives on the stack
                mov edi,ecx                 <- ... and `this` in ECX
                mov eax,[esi+0x5bc]
```

So the VM is a stack argument here, where every game so far has passed it in a register, and
`draw.vm_reg` had no way to say that. That is the fifth thing this rebuild moved, and it is now
`draw.vm_stack_arg`, in the same family as `runner_arg`, `frame_ctx_ecx`, `cleanup_this_ecx`
and `screenshot_stack_arg`. The wrap borrows EAX to read `[esp+8]` and gives it straight back;
`mov` sets no flags, which is the one thing that stub may not disturb.

### The three field offsets read themselves

The VM struct grew -- `+0x594` became `+0x5bc` -- so TH13's `vm_anm_off`, `vm_layer_off` and
`vm_script_off` are not transferable. They are left at **zero**, and that is not a gap: with
them zero no VM is classified, so no rule can match the wrong thing, and `dim_vm_trace` scans
each VM's first three hundred words for a pointer to a loaded ANM record -- validated by the
name ending in `.anm` -- and logs the offset it finds:

```text
draw      vm anm pointer at +0x30: slot 3 bullet.anm
```

Which is the whole trick. The offsets get read off a running game instead of guessed, the same
way the class table and `world_prio` will be. Guessing `vm_anm_off` would have printed
*plausible* ANM names that were not the ones being drawn, and both the dim rules and the class
table would then have been written against fiction -- the one failure here that nothing
downstream could catch.

## 14. What the VM trace said

`vm_anm_off` is **0x30** and `vm_layer_off` is **0x24** -- the same two places TH13 keeps them,
which the trace found rather than TH13's numbers being assumed. Every one of 280 VMs across a
stage reported its ANM pointer at +0x30, with no other offset ever matching.

And with those, the draw list says what each priority actually puts on screen:

| prio | callback | ANM / layer |
|---|---|---|
| 3 | `0x40eb80` | `st01wl.anm` L0 |
| 5 | `0x47e0f0` | `title.anm`, `front.anm` L0 |
| 9 | `0x47e110` | `effect.anm` L2 |
| 19 | `0x47e1f0` | `enemy.anm` L8 |
| 27, 28, 29, 30 | `0x47e230`, `0x44ec70`, `0x47e240`, `0x47e250` | `pl00.anm` L13, L14, L15 |
| 31, 35 | `0x439780`, `0x417640` | `bullet.anm` L0 |
| 42, 43 | `0x47e320`, `0x47e2b0` | `effect.anm` L20, L21 |
| 49, 51 | `0x47e2c0`, `0x47e2d0` | `front.anm` L22, `st01logo.anm` L23 |
| 54+ | the rest | `title.anm`, `ascii.anm`, `front.anm`, `text.anm` |

**Which corrected a guess.** §12 read priority 31 (`0x439780`, 270 prims from one texture) as
enemies. It draws `bullet.anm` -- it is the laser or cancel path, not enemies, and the actual
enemies are drawn by an ANM layer callback at priority 19. That is exactly the kind of error
prim counts produce and names do not, and it is the reason the class table waited for this.

It also confirms the player: `0x44ec70` at priority 28 draws `pl00.anm`, so `0x44ec60` is the
player's update -- which the shape matcher had put first at a thoroughly unconvincing 0.27.

### The rules, and what is left unclaimed

`world_prio` is 19, the first world object. Four rules: bullets never fade, `effect.anm` layer 2
fades as effects under the world (TH13 has the same rule at its priority 8), the rest of
`effect.anm` fades, and the whole of `pl00.anm` does not.

**The player's shots**, last of the five classes, separated by draw rate rather than by name --
every one of these streams is `pl00.anm` so a name settles nothing. Against roughly 5054 frames
of play: her own callback at priority 28 drew 5054 times, about once a frame, while layer 13 at
priority 27 drew 77658 times and layer 15 at priority 30 drew 65172, fifteen and thirteen times
a frame. A screenful of shots looks like that; a character does not. So layer 14 is her, 13 and
15 are what she fires, and anything else of hers is left alone -- which is TH13's three-rule
shape with TH14's layers in it.

One thing is deliberately unclaimed. `DIM_SPECIAL`: TH13 has one because of its
divine spirits, and nothing in TH14 has been found to compete with bullets for attention the
way those do, so the profile names no special class and the menu does not offer one.

### Items: four rounds, and what actually found them

TH14's items are drawn from **`bullet.anm`**, by their own manager, at draw priority **31** --
which is why three stages of looking for an item ANM found nothing. The update is the census's
priority 24 (`0x439750`, body at `0x438550`) and the draw is `0x439780`.

What settled it was reading the update's body rather than counting anything:

```text
438716  movss xmm0,[0x4d8f58]      ; the game speed
43871e  mulss xmm0,[0x4c1918]      ; 0.03
...
43892f  addss xmm0,[0x4c1968]      ; += 0.2 into [edi+0xbfc], once a frame
438937  movss [edi+0xbfc],xmm0
```

`+= 0.2` a frame into a per-entity fall speed is TH13's item model instruction for instruction.

The three rounds before that are the lesson. **Prim counts said "enemies"** (270 prims of one
texture at priority 31) -- wrong, the enemies are at 19 on `enemy.anm`. **ANM names said "the
laser or cancel path"** -- closer, but still wrong, because a name identifies a texture and not
a system when two systems share one. **A constants scan said `0x438530`** -- which turned out to
be a four-line destructor: the scan walked a fixed 0x800 window from a call-target start and had
run straight past the end of the function into `0x438550`, where the constants really were. The
window found the right constants and attributed them to the wrong function, and every step after
that inherited the error.

Each method was sound and each was defeated by the same thing: a signal that is one remove from
the question. The count is a remove from the system, the texture is a remove from the system,
and a scan window is a remove from a function. Reading the instructions that do the work is not.

One rule was written along the way on the guess that `bullet.anm` on layers 20 and 21 was the
items; `dim_items` faded nothing, and the next stage produced no such row at all, so it was
removed rather than reassigned.

**A fifth rule, after the first report.** Fading the effects also faded the focus ring and the
hitbox, because those are drawn from `effect.anm` rather than from `pl00.anm` -- layer 14 at
priority 29, between `pl00.anm`'s layers 13 and 15 -- and the general `effect.anm` rule caught
them. TH13 has exactly this carve-out (`effect.anm` layer 12, inside its world band, commented
"the focus ring") and it was dropped when these rules were written from the same file. The band
is widened to the player's three layers rather than pinned to the one the trace caught, because
anything `effect.anm` draws inside the player's own band is the player's furniture, and being
wrong that way costs an effect near the player that does not fade -- against a hitbox that
disappears exactly when it is being looked at.

`UI_DIM_CLASSES` used to answer "all of them", which was true of TH10-13 and is not true of a
game whose rules are still being written. It now reports the classes the profile's rules
actually mention, so the menu offers background and effects for TH14 and does not offer sliders
that would do nothing.

## 15. The game speed, and the twenty-five writes to it

Sub-stepping *is* the game speed. `set_factor` writes `g_logical * dt` into the game's own speed
multiplier and then runs the update, so nothing can be `MODE_SUB` until that variable is
described -- and it cannot be described without every one of the game's own writes to it being
described too, or the runtime and the game overwrite each other once a tick. This section was
written while that was still outstanding; §15b is where it stops being.

`addr.speed` is `0x4d8f58`, settled three ways: the save/override/restore idiom at `0x424780`,
the sixty-one `mulss` reads of it across the gameplay code, and the item manager multiplying
its per-frame step by it at `0x438716`.

### Twenty-five writes, not twelve

The first enumeration searched for `movss [0x4d8f58], xmm*` and found twelve. The game also
writes the speed as `mov dword [0x4d8f58], imm32` -- thirteen more, storing `1.0` or `0.0` --
and searching for one encoding and reporting the result as "the write sites" would have shipped
a speed model with half its sites missing, which is silent. Same shape of error as the scan
window in section 14: the tool answered exactly what it was asked, and the question was wrong.

| | sites |
|---|---|
| `movss [speed], xmmN`, 8 bytes | `0x40dac5` `0x411780` `0x4247a6` `0x4247fa` `0x429796` `0x43a6f1` `0x449097` `0x44a165` `0x44af39` `0x44b0e5` `0x46feff` `0x472d4c` |
| `mov dword [speed], 1.0f`, 10 bytes | `0x40747a` `0x40da9a` `0x435b44` `0x436100` `0x444a00` `0x448eb6` `0x448ff8` `0x449eff` `0x44a0b5` `0x44df30` `0x46fe8a` |
| `mov dword [speed], 0.0f`, 10 bytes | `0x43a6dd` `0x46ff09` |

### Three idioms, and why they cannot share one treatment

- **Absolute stores.** `speed = 1.0` at a stage start, `speed = 0.0` to freeze something. These
  clobber the runtime's factor and must become "the game means N; record it and store N x factor".
- **Save, set, restore.** `movss xmm,[speed]` into a local, an absolute set, a call, then the
  local written back -- `0x40da9a`/`0x40dac5` and `0x43a6dd`/`0x43a6f1` are both this. The set
  needs the treatment above; the restore must not get it, because what it writes back is the
  already-composed value, and running it through the same stub would multiply the factor in a
  second time.
- **Read, modify, write.** `0x424777`: read the speed, scale it, clamp it, store it back. That
  composes with the factor on its own and wants no patch at all.

Which is why a uniform rule does not exist, and why a heuristic does not either -- "does it read
the speed just before?" labels the *set* of a save/set/restore triple as self-composing, because
the save is the read it sees. Each of the twenty-five has to be read.

### And the backend needed two more shapes

`install_speed_sites` patched a 6-byte `fstp [speed]` and, where the operation needs the stored
value, captured it off the x87 stack. TH14 stores from an XMM register or from an immediate, so
`SpeedSite`'s `pop_float` became `src` (`enum SpeedSrc`), and the capture is `fstp [g_fpu_tmp]`,
`movss [g_fpu_tmp],xmmN` with N from the profile, or nothing at all. `SPEED_SRC_FPU` is 1 so
that the four profiles written before the enum existed still say what they said.

The stub also grew the eight XMM registers. It stands in for one instruction, not for a call,
so the game's next instruction expects every register it had; `pushad`/`pushfd` covered the
general ones, and on TH10-13 that was enough because the code around an `fstp` is x87. On TH14
the patched instruction sits in SSE code, and the operation behind the stub is C compiled
`-mfpmath=sse`. `tools/test_speed.h` is what says so: it loads all eight with sentinels, calls a
real patched site through the real installer, and checks every one came back --

    FAIL: the permanent store's stub clobbered xmm0 lane 1 (1.500000 -> 0.000000)

which is what it reports with the save removed. Getting that failure needed a fix to `test.sh`
as well: the harness was building the runtime without `-msse2 -mfpmath=sse`, so the operation
compiled to x87 there and the test could not have seen the clobber. A harness that builds the
code under test with different code-generation flags is not testing the code that ships.

### Which sites are patched, and which are correct untouched

Twelve of the twenty-five are described in `th14_speed_sites`; the other thirteen are left
alone, each for a reason that holds on its own.

| site | treatment | why |
|---|---|---|
| `0x40747a` `0x435b44` `0x436100` `0x444a00` `0x44df30` | `SPEED_ONE_PERM` | the new logical speed really is 1.0 |
| `0x40da9a` `0x448eb6` `0x448ff8` `0x449eff` `0x44a0b5` `0x46fe8a` | `SPEED_ONE_TEMP` | 1.0 for the duration of something the game restores after |
| `0x429796` | `SPEED_ECL`, value in xmm0 | the script instruction that sets the speed |
| `0x40dac5` `0x449097` `0x44a165` `0x44af39` `0x44b0e5` `0x4247fa` `0x472d4c` | none | writes back the raw global the game saved, which is already the scaled value |
| `0x43a6dd` `0x46ff09` | none | stores 0, and 0 x factor is 0 |
| `0x43a6f1` | none | the restore half of the freeze at `0x43a6dd` |
| `0x4247a6` | none | `speed*(1-k)` clamped to [0, 1]; multiplicative, and the clamps cannot bind while the factor is at most 1 |
| `0x46feff` | none | `saved*(1-k)` from the value `0x46fe50` read at entry |
| `0x411780` | none | unreachable: a one-instruction `movss [speed],xmm1; ret` with no call, no jump and no data reference anywhere in the image |

`SPEED_ONE_TEMP` rather than `SPEED_PAUSE_SET` for the six is the point worth keeping. The pause
pair exists for games whose restore writes a value the runtime has to reconstruct; TH14's
restores write back the raw global, so patching the set alone is both necessary and sufficient,
and patching the restore too would fold the factor in twice.

## 15b. The class table: the two sprite passes, and nothing else

`0x47e7f0` (priority 8) is `jmp 0x47e6c0`; `0x47e7c0` (priority 29) is a flag test on the
supervisor at `0x4db558` that either returns 1 or falls into `jmp 0x47e5e0`. The two bodies are
the same walk over two different lists of the *same* manager object -- `[this+0xfe8210]` and
`[this+0xfe8208]` -- calling `0x46fe50` per VM and re-bucketing the survivors by layer. Both are
`MODE_SUB`.

What makes that safe without a single per-frame hook is that a sprite VM's motion is in units of
the game speed and nothing else in it counts frames on the side. `0x473139`, `0x47318e`,
`0x4731e0`, `0x473238` and `0x47325c` are `mulss xmm0,[0x4d8f58]` in the interpolator, and
`0x474e7b`, `0x474f1b`, `0x474fbb`, `0x475054`, `0x475166`, `0x475276`, `0x47fdc9`, `0x47fe22`
store the speed's *address* into interpolator fields. Six passes at a sixth of the speed is the
same motion at six times the resolution.

Systems that step their own VMs by calling `0x46fe50` themselves -- about a hundred and ninety
call sites -- are reached from `MODE_FRAME` callbacks and still advance once a frame. So the
world's sprites move in 60 Hz steps while the menu, the HUD and anything animating out of a
script does not, which is exactly the claim the install log now makes:

    sub-stepping: 2 of 21 identified systems step with the display (AnmSpritesEarly, AnmSpritesLate).
      everything else steps once per 60 Hz frame, so what it draws moves in 60 Hz steps however high the frame rate is.

**The names are labels.** TH13 calls its pair world and UI. Which of TH14's two is which is not
established: the late one is behind a gate that skips it on a supervisor flag, which is the
shape of the pass that stops when the game does, but that is a signal and not a reading. They
get the same treatment either way, so they are named for when they run rather than for what
they might be.

The menu's "Sub-stepped subsystems" list now shows only `MODE_SUB` classes. A class table names
every callback the census identified so that there is something to report against, and most are
`MODE_FRAME`; a checkbox against one of those cannot do anything, and an inert checkbox in a
panel meant for narrowing down a problem is worse than no checkbox, because it makes a system
look ruled out when it was never stepped. That change also applies to TH10-13, whose lists lose
the entries that were never sub-stepped.

## 15c. Sub-stepping the gameplay: what this engine makes easy, and what it does not

**The timers carry a rate pointer.** A timer here is `{prev, int, float, const float* rate}` and
its tick is `float += rate ? *rate : 1; int = (int)float; prev = the int before`. The
constructors store `&0x4d8f58` -- the game speed -- into that rate field in a hundred and
ninety-nine places. So under sub-stepping those timers advance by a fraction of a frame per tick
by themselves and each integer crosses a whole number on exactly one tick per frame. That is why
TH14's hooks are a dozen lines where TH13's were three hundred: most of what TH13 had to hook
was timers with no such pointer. Every offset below was read off the constructor that sets it,
not guessed from TH13's.

What a rate pointer cannot fix, and so what every hook here is one of:

1. **An integer the update decrements or increments itself.** Bullet `[+0x24]` (0x416a7a and
   0x416d09) and `[+0xbfc]` (0x416d14); the player's focus counter `[+0x1830c]` (0x44d924).
2. **A block gated on a timer's integer *being* N**, which stays true for a whole frame and so
   runs on every tick of it. The bullet's cancel-effect spawn at `cmp eax,3` (0x416b0f); the
   player's whole death/respawn state machine.
3. **A truncation.** The player's position is fixed point in 1/128 of a pixel at `[+0x5ec]` and
   `[+0x5f0]`, advanced by `cvttss2si` of a velocity the game has already scaled by the speed
   (0x44d774, 0x44d77c). Six sixths of a velocity truncate to less than one whole, and a
   velocity under six units a frame truncates to nothing at all -- the player would simply not
   move. `movement_cvttss` carries the residual, and does nothing at all when the factor is 1,
   because the game's own slow-motion truncates the same way it always did.
4. **An exponential approach.** The options close a fixed proportion of the distance to the
   player each frame -- `(target - pos) * [+0x182bc] / 100` at 0x44d9aa, with the blend at 30.
   Thirty percent six times a frame is eighty-eight percent a frame, and the options would sit
   on the player instead of trailing her, which is a gameplay difference and not a cosmetic one.
   The approach runs on frame boundaries only. What that costs is options moving in 60 Hz steps
   while the player does not; making them smooth means interpolating between the two frame
   positions, the way `place_enemy` does for enemies, and that is separate work.
5. **A guard the game writes in terms of its own prev/int pair.** The invincibility blink at
   0x44e16f asks "did the state timer's integer change, and is it a multiple of 3". Sub-stepped,
   the answer is yes on one tick of six, so the flash would appear for a sixth of a frame. It is
   replaced with the same question asked of the timer's *float* before and after this tick's
   Player call (`g_ptf_prev`/`g_ptf_cur`), which is true every tick, so the "multiple of 3" then
   holds for a whole frame as it did. TH13 replaces the identical guard at 0x446888.

### Settled by a replay: the sub-stepped simulation is the same simulation

The "player does slightly more damage" report ran for three rounds. Two of them found real bugs
-- the two unscaled rates in the shot array, and before that the pair of guards that stopped the
player damaging anything at all -- and neither changed the symptom. The counters eventually said
why: on the same spell card, with sub-stepping on and off, damage per landed hit was 62.6 and
62.4, and the hit rate per shot alive varied by about a quarter *within* each condition. The
scatter between samples of the same condition was larger than anything between the conditions.
There was nothing to find.

What settled it is a test worth keeping for every game: **record a replay with sub-stepping off,
play it back with it on.** Touhou replays are deterministic, so if the sub-stepped simulation
differs anywhere at all -- a timer, an RNG draw, a position -- the run diverges and the player
dies somewhere she did not. It played through identically, which is a stronger statement than any
counter: the sub-stepped simulation is not close to the stock one, it *is* the stock one at every
frame boundary.

That direction matters. A replay recorded under stock conditions is valid by definition and stays
useful as a regression reference for later builds; one recorded under sub-stepping is only valid
against the build that made it. And the test means nothing with sub-tick input on, because that
deliberately samples input more often than the replay format records -- which is exactly what the
replay extension exists to handle, and the reason it has to come before sub-tick input rather
than after.

### 16c. The determinism test was not testing determinism

A replay recorded at 360 Hz with sub-stepping on desynchronised when played back, and chasing
that turned up something worse than the desynchronisation: **the test in §16b proved nothing.**

`replay_check` sets the logic rate on playback from the rate recorded in the file. A replay with
no HFR chunk -- which is every TH14 replay, because the extension is not installed -- has no
recorded rate, so playback drops to 60. That is the right default: a stock replay should play in
the simulation that made it. But it means "record a stock replay, play it back with sub-stepping
on" silently turned the sub-stepping back off. The test compared stock with stock and could only
ever have passed.

So what is actually known is the opposite of what § 16b claimed. The one real experiment -- record
*with* sub-stepping, play back at the forced 60 -- **desynchronised**. The sub-stepped simulation
and the stock one are not the same simulation, somewhere. Everything §16b concluded is withdrawn,
and the "player does slightly more damage" report is open again, with no evidence against it.

`replay_trace` (debug only, off by default) is what makes the test real: it keeps the current
logic rate across playback instead of dropping to 60, and writes one line a game frame with the
replay's frame number and the player's fixed-point position. Play one file back twice -- once
stock, once genuinely sub-stepped -- and diff the two logs. The first line that differs is the
frame the simulation diverged on, which localises the bug to a frame instead of to a system.

The lesson is not about replays. It is that a test which cannot fail is worse than no test,
because it is reported as evidence. This one passed for a reason that had nothing to do with what
it claimed to measure, and it was believed for three exchanges.

### The two rates nothing was scaling, found by someone playing

Reported, not measured by a test: the player felt like she was doing slightly more damage than the
unmodified game. That is a hard symptom to chase, because "slightly more damage" is not a thing
any one line of code does -- but it is exactly what a shot that *aims better* looks like.

The shot array's update carries two rates it applies itself, outside the MotionState and outside
any timer: `[-0x64] += [-0x60]` and the angle `[-0x5c] += [-0x58]`, from the loop cursor at
`0x44e01f` and `0x44e040` (shot `+0x14 += +0x18`, `+0x1c += +0x20`, the second wrapped). Nothing
multiplies either by the game speed. Sub-stepped, they advanced six times a frame, so a homing
shot's aim converged six times as fast and landed shots that would have missed. Both increments
are now scaled by the sub-step fraction. TH13 has the same pair and the same fix at `0x443691`,
which is the second time in this port that a system TH13 had already solved was missed because
the search was for a *shape* -- a raw integer counter, a prev/int guard -- and this one is neither:
it is a float rate hiding among float rates that are correctly scaled.

The lesson to carry to the next game: "is every per-frame quantity scaled?" is not answered by
finding the counters. A float `a += b` with no speed multiply is as much a per-frame quantity as
an integer decrement, and it is much harder to see, because the four lines above and below it look
identical and *are* scaled.

### The options: interpolated too, for the reason they were not sub-stepped

The options chase the player by a fixed proportion of the remaining distance each frame, so
sub-stepping them changes how far they trail (§ above), and the approach was left on the frame
boundary. That made them the one thing in TH14 still moving in 60 Hz steps. They get the same
answer the enemies get: the logic stays once a frame and the *sprites* are placed between the two
frame positions.

Eight options of `0xe4` at `player+0xd6ec`: active flag at `+0x00`, position as fixed point in
1/128 of a pixel at `+0x5c` and `+0x60`, the two ANM VM ids at `+0xb0` and `+0xb4`. All read off
the option loop's own tail at `0x44db3d`, which writes exactly those two VMs from exactly those
two integers, scaled by the 1/128 at `0x4c1900`, with z zero.

`place_options` is a second, optional profile hook called from the same place as `place_enemy`,
with the frame-boundary flag; the adapter keeps its own tracking, because eight slots is an array
and not a list. Making the interpolation pass run for a profile that has one hook and not the
other is what turned up a real gap: it read the sprite manager through `addr.anm_manager` before
checking that the profile has one. `test_runner_undescribed` -- the fixture that zeroes every
optional address -- faulted on it immediately, which is the third time that fixture has caught
exactly this class of thing.

### Enemies: interpolated, not sub-stepped

Enemies do not become MODE_SUB, in TH14 or in TH13. Their behaviour is an ECL script, and an
interpreter stepped six times a frame is not a faster enemy, it is a different game. What moves
smoothly instead is their *sprites*: `enemy_interp` places them every tick between the last two
frame positions, which is the same one-frame lag everything sub-stepped already has.

Everything it needs was readable from the game's own placement, `0x424810`, called as
`lea ecx,[ebx+0x11f0]` from the enemy update at `0x42476b` and `0x4247ef`:

| | |
|---|---|
| `addr.enemy_manager` | `0x4db52c` — the manager the shot-versus-enemy test uses at `0x451463` |
| `layout.enemy_list` | `0xd0`, walked as `{enemy, next}` by the update at `0x422974` |
| the sprite sub-object | `enemy+0x11f0`: position `+0x44`, 14 VM ids `+0x124`, offsets `+0x164` (three floats each), parent slots `+0x224`, flags `+0x4054` |
| `layout.enemy_flags` | `0x5244` = `0x11f0 + 0x4054` — and that is exactly the word the manager tests before updating an enemy, which is what makes the whole chain check out |
| `layout.enemy_position` | `0x1234` = `0x11f0 + 0x44` |
| `layout.enemy_skip_mask` | `0x02000000`, the manager's own "skip this enemy" bit |
| absolute-position bit | `0x04000000` in the same word (TH13's is `0x08000000`: the bits shifted by one as well as the offsets) |
| the VM position | `+0x59c` (TH13: `+0x574`); a parent's contribution at its VM `+0x3c`, the same three words TH13 reads |

The sub-object offset came out the same from two directions, which is the check worth having: the
placement is reached as `enemy+0x11f0` and the enemy update reaches the id array directly as
`lea esi,[ebx+0x1314]` — and `0x11f0 + 0x124` is `0x1314`.

One convention had moved again, the sixth: `anm_get_vm` (`0x47f0a0`) takes the manager in **ECX**
where TH10-13 take it in EDX, so the profile says `anm_get_vm_ecx` and `interpolation.c` picks the
register. Both forms were checked by disassembling the compiled runtime rather than trusting the
inline-asm constraints, which is how the two SSE stubs earlier in this port went wrong.

### Lasers: one line, because the engine already had the mechanism

The laser manager's list walk is `0x43a570`, behind the callback `0x43a6a0`; four laser classes
with vtables at `0x4be2fc`, `0x4be364`, `0x4be3cc` and `0x4be434`, updated through `[vtable+0x10]`
(`0x440910`, a no-op `0x443c50`, `0x43e040`, `0x43be20`). Everything about them was already
right: the motion multiplies by the game speed (`0x43bcd3`, `0x43bf8d`, `0x43e0a5`, `0x43e0c8`,
`0x4426f5` …), every timer inside a laser has a rate pointer aimed at the speed, there is not one
integer the updates count by hand, and not one "the timer is exactly N" gate. The updates never
touch the player either, so the question that caught out the shots -- who reads this once a
frame -- has no bad answer here.

The one thing wrong was the *base* timer the manager ticks for every object it owns (prev
`+0x18`, integer `+0x1c`, float `+0x20`, rate `+0x24`, ticked at `0x43a603`). The laser classes
leave that rate pointer null, and the manager's answer to a null rate is to add a whole `1.0` --
six times a frame, so the phases that compare against it (`0x43e1bd`, `0x43e207`) would each have
lasted a sixth as long.

The fix is to give it a rate, which is the engine's own mechanism used as intended. Not the game
speed: that would fold in the slow-motion this particular timer is deliberately not subject to.
The sub-step fraction alone -- `g_factor` -- and six ticks of a sixth come to exactly the 1.0 a
frame was worth. With no sub-stepping the fraction is 1.0 and the game's own "within 1% of 1.0,
call it 1.0" test at `0x43a614` makes the result bit-for-bit what it was. Keeping the *float*
continuous matters as much as the integer here: one of the laser classes interpolates its width
from it (`0x43e240`), and a laser growing in 60 Hz steps is a thing you would see.

### Items: two counters, and nothing else

The item update (`0x438550` behind `0x439750`, EDI = item, stride `0xc18`, timer prev `+0xbc8`,
integer `+0xbcc`, float `+0xbd0`, rate `+0xbd4`, ticked in the per-item tail at `0x438d0c`) was
the cheapest system yet. Its motion is already `pos += vel * speed` (`0x4386b6`) and even its
gravity is `speed * 0.2` (`0x438716`), so an item falls at the same rate however often it is
stepped. Only two things count frames by hand: the despawn countdown in state 5 (`0x4385c3`) and
the "wait, then start falling" countdown in state 1 (`0x438631`). Both are gated on the item's
own timer having crossed a whole frame, and the second keeps the game's "already expired" branch
*ahead* of the gate, so an item that is falling is still updated every tick -- it is only the
counting that happens once a frame.

Who reads items once a frame, which is the question this port now asks of every new MODE_SUB
callback: nobody. The manager collects against the player inside its own update rather than the
player reaching into the items, so there is no once-a-frame reader to get out of step with.

### The two guards that stop a sub-stepped player doing damage

`0x451400` is "test this enemy against every one of the player's shots", called by the enemy
with its position and radius. It has two guards, and both fail silently under sub-stepping in
the same way: the question is asked on the boundary tick, because the enemy code is MODE_FRAME,
and the thing it asks about now changes on some *other* tick.

**The player's state timer, at the top of the function** (`0x451450`). "The integer did not
change -> return 0". Sub-stepped, that integer changes on one tick of six, and at 360 Hz, where
dt is not exact in float32, that is never the boundary tick. The function returns 0 every frame
and nothing the player fires ever hits anything. It is replaced with the same question asked of
the timer's *float* before and after this tick's Player call, which is what `g_ptf_prev` and
`g_ptf_cur` are for -- true on every tick, as "the timer advanced this frame" was before.

**Each shot's own timer, inside the loop** (`0x4514ce`): "this shot's integer changed this tick,
and is a multiple of the shot's interval at +0x80". That is the shot's hit cadence in frames.
Here the fix is at the other end -- where the timer is ticked, in the player's own tail
(`0x44e08d`, a countdown by its rate pointer, which is the game speed). It is ticked by the
logical speed on the boundary tick, one whole frame's worth, and not at all on the others, with
prev set to the integer so the guard reads "unchanged". The total per frame is unchanged and so
is the shot's lifetime; what changes is that the integer now crosses on the tick that asks.

TH13 has this exact pair at `0x446888` and `0x4436b4`, and its notes say what it costs to miss
it. Ours cost a build: the player moved beautifully and could not kill anything. The lesson that
generalises is that the failure is not in the system being sub-stepped, it is in the *other*
system that reads it once a frame -- so the question to ask of every new MODE_SUB callback is
not "does this still work" but "who reads this, and how often".

The shot array itself: 256 entries of 0xa4 at `player+0xde28`, with the timer at +0x60 (prev),
+0x64 (integer), +0x68 (float), +0x6c (rate), the interval at +0x80 and the active flag at +0x00.
The hit test walks it as `[player+0xde18]` with the cursor at +0x74; the player's tail walks the
same array as `[player+0xde90]`.

### The weapon timer, and a census for "it does nothing"

Reimu's focus weapon -- the spinning broom -- kept moving and stopped damaging anything after the
player was sub-stepped, while her ordinary shots were fine. The two go through different state.

The player owns 256 *weapon* objects at `player+0x6c8`, walked by `0x451380` and updated by
`0x4510b0`, which calls the weapon type's own update (a table of six callbacks per type, at
`0x4d5908`, `0x4d5928`, `0x4d5948`) and then ticks the weapon's timer at `0x45131b` -- prev
`+0x18`, integer `+0x1c`, float `+0x20`, rate `+0x24`, the rate pointing at the game speed. Every
weapon type's update asks that integer "did it change, and is it a multiple of N": the cadence on
which the weapon fires, retargets, and drives the damage volume it owns in the shot array
(`weapon+0xc0`, whose flags it sets to the swept shape at `0x451b17`). It is the same rule as the
shot timer's, and it gets the same treatment -- advanced by the logical speed on the boundary
tick and not at all on the others, with the integer and float written back unchanged. The
weapon's motion is a MotionState stepped every tick by `0x4510b0`, so nothing moves in steps.

**And a census, because guessing was not working.** "The player fires and nothing dies" has four
possible answers inside `0x451430` and no way to tell them apart by reading. `E_count` and
`site_census_report` are a handful of counters a profile's site hooks can increment, printed on
the debug stats line; TH14 installs six of them, only when the ini asks for debug, inside the
game's own guards:

| | |
|---|---|
| `tests` | enemies tested against the shot array |
| `shots` | shot slots examined |
| `notick` | rejected: this shot's timer integer did not change on this tick |
| `cadence` | rejected: the integer is not a multiple of the shot's interval |
| `hit` | reached the shape test |
| `melee` | of those, the ones with the swept shape -- a focus weapon's damage volume |

A build that installs code only when a person's ini says so is a path nothing else exercises, so
the harness now sets `cfg.debug` before validating the patch plan: these are patches into the
game like any other and are frozen and checked like any other.

### Gate the dispatch, not the arms

The player's life state machine (`[+0x684]`, table at 0x44ebf4) has five arms and only state 1
is alive. The other four count whole frames, spawn effects on exact frame numbers and draw on
the RNG. Gating each of those would be a dozen hooks in code that is hard to test; gating the
dispatch is one (0x44dbf8), and it says what is meant: on a tick that is not a frame boundary,
anything but state 1 goes straight to the update's tail. The tail is where the sprite VMs are
stepped, so it still runs every tick and the death animation is still smooth -- it is the
sequence's *logic* that stays at 60 Hz.

### Two hand-written stubs, and why they are tested by running them

`install_speed_sites`' XMM save and `movement_cvttss` are the patch's hand-encoded SSE, and both
had a bug that disassembled to something plausible. The speed stub clobbered xmm0 because the
operation behind it is C compiled `-mfpmath=sse` and nothing was saving it. `movement_cvttss`
re-emits the original instruction's memory operand from its modrm byte, and took the byte
unchanged -- so a site whose destination was ECX loaded the velocity into *xmm1* and truncated
whatever the game had left in xmm0. Reading the emitted bytes back is what found both, and
`tools/test_speed.h` is what keeps them found: it builds a real site, runs it through the real
installer, calls it, and checks the arithmetic and the registers. Neither test passes with its
fix removed, which is the only thing that makes it a test.

### Where the addresses came from

| | |
|---|---|
| `addr.player` | `0x4db67c` -- `mov eax,[0x4db67c]` then `[eax+0x184b4]`, a field the player update writes through EDI |
| `addr.player_callback` | `0x44ec60`, which is `jmp 0x44dbd0` |
| `layout.player_timer` | `0x694`, the float of the life-state timer at `0x68c`; its rate pointer at `+0x698` is set to the game speed at 0x44dd2a |
| the bullet's timer | prev `+0x13c0`, int `+0x13c4`, ticked by the manager after every update (0x4171b7); rate set at 0x416f82 |
| the bullet's second timer | prev `+0x13d4`, int `+0x13d8`, ticked by the update itself at entry; rate set at 0x416fdb |
| the game manager | `0x4db558`, flags at `+0x80` -- the word every update callback's gate tests. Not in the profile yet: the runtime's pause test is written against TH10-13's bit assignments (`0x70`) and TH14's are not those, so it needs reading before it is described. |

## 17. The replay extension

What it is for: a replay file that says which rate and which simulation settings recorded it, so
that playback can restore them -- and so that sub-tick input, which samples input more often than
the replay format records, has somewhere to put the extra stream. It has to come before sub-tick
input for that reason, and because the determinism test in §16b is worthless once a replay can be
recorded that no build but the recording one can play.

TH14 moved both conventions, again:

| | |
|---|---|
| save | `0x455490`, **stdcall with four stack arguments** (filename, name, two more), `ret 0x10`, where TH13's is fastcall with one. Hooked at `0x449a29`, `0x44aa5c`, `0x44b973`, `0x4617b8` |
| load | `0x455c20`, **thiscall** -- manager in ECX, filename pushed, `ret 4`. Hooked at `0x4549bc`, `0x454b40`, `0x454fd3`, `0x45ee0f` -- four sites where the profile has one slot |
| `layout.replay_stage` | `0x218` |
| `layout.replay_frame` | `0x210` |
| `layout.replay_stages` | `0x20`, eight stage records |

The three offsets are the same numbers TH13 has, and they were read rather than copied: `0x455f7f`
stores the stage index at `+0x218` and, in the very next instruction, indexes the array at `+0x20`
with it. Copying TH13's numbers would have got the same answer and proved nothing.

Both wrappers live in `th14.c` rather than becoming two more shapes in the shared installer, which
is what TH13's load already does. The save needs no assembly at all: giving the wrapper the game
function's own signature means the call site pushes four arguments and the wrapper pops four, so
the stack is right by construction. The load needs three instructions -- push the caller's
filename, push ECX, call a stdcall C function, and `ret 4` because that is the game function's own
epilogue. Both were checked by disassembling the compiled runtime, which is now the habit for
anything where a calling convention is being bridged.

### It is not installed, and why

Pressing "save replay" took the game down, and the log says what happened before it did: the
wrapper ran twenty-five times in a row, once for `th14_01.rpy` through `th14_25.rpy`.

The four call sites that reach `0x455c20` are not four ways of starting a replay. At least one of
them -- `0x45ee0f`, inside the function that globs `th14_ud????.rpy` -- is the menu **reading every
file's header to build its list**. Hooking that means `restore_replay_settings()` and a chunk read
for each file the moment the menu opens, which is wrong on its face and fatal in practice. The
same doubt applies to the save sites: three of the four are inside the two functions that build
the `th14_%.2d.rpy` filename, which is what a *menu* does.

So the hooks are backed out and the addresses stay, frozen and checked. They identify the build
just as well unwritten. What is missing is not an address, it is a distinction the call sites do
not make on their face -- "play this replay" against "look at this replay" -- and that has to be
read before anything is hooked again. The obvious next step is to read `0x455c20`'s own body for a
parameter or a manager field that separates the two, rather than to find a fifth call site and
hope.

**And the diagnosis cost more than it should have.** The crash produced no exception report at
all. The reporter had a budget of four, and TH14 raises four harmless first-chance access
violations inside KERNEL32 before the title screen, so the budget was spent before anything went
wrong. It now reports once per *distinct* address with twelve slots, which is the behaviour it
should always have had: a budget exists to stop one failing instruction filling the log, not to
stop the log ever mentioning a second place.

**Still unverified, for when the hooks go back in:** every replay TH14 saves would carry an extra
`USER` chunk after the game's own data. TH10-13 have done this for several releases and the format
tolerates it, but TH14 is a different build of the loader.

### The input path, read from the same function

Sub-tick input's addresses came out of the replay record node at `0x455040`, which is where the
game latches its input for the frame:

| | |
|---|---|
| `addr.poll_input` | `0x41e710`, thiscall with the raw input object `0x4d6878` in ECX |
| `addr.game_input` | `0x4d6a90` -- the word the record node writes and the player's movement reads at `0x44d33e` |
| the previous frame's input | `0x4d6a94`, which pressed and released are derived from |
| two further recorded words | `0x4d6a9c` and `0x4d6aa0` |

Three words at six bytes a frame is the replay's input format, and sub-tick input has to keep
producing exactly that while sampling more often.

## 16. What a trace still has to supply

The dimming rules and the class table came out of the patch's own log while a stage was
running, not out of the executable: the registered update list names every callback, and the
per-callback sprite census is what the dim rules are written against (DEVNOTES_RUNTIME §3b).
Both are now in the profile, the class table with two of its twenty-one entries `MODE_SUB`
(§15b). The remaining nineteen stay `MODE_FRAME` until each one's own counters have been found
and hooked; guessing is how a bullet manager ends up stepping at 360 Hz with its own timers
still counting in frames — §7 of the runtime notes is what that costs. TH13 needed about three
hundred lines of per-frame hooks for that, and TH14's are not written.

Also outstanding: the English and Steam builds (only `th14.exe` is recognised so far),
`vpatch_th14.dll`'s conflict sites, `sprite_round_sites` for internal resolution, the replay
magic (`t14r` is the obvious guess and nothing reads it yet), and `data_dir` — TH13 needed one
because it saves replays under `%APPDATA%`, and whether TH14 does has not been checked.

## 18. The first desync trace, and why it pointed at the wrong thing

The user recorded a spell card at 360Hz with sub-stepping on, then played that one file back
twice with `replay_trace=1` -- once sub-stepped, once stock. Both runs are genuine: the `on` log
says `replay playback started -> logic rate 360` with `substep=1`, the `off` log says
`logic rate: 60 ticks/s` and `substep=0`, so `replay_check`'s 60Hz override was really out of the
way this time.

Lining the two up by the replay's own frame number:

```
frames 0..633   identical, every one
f=634           on=(-153, 51993)   off=(-195, 51993)
f=635           on=( 103, 51993)   off=(-195, 51993)
...
f=644           on=(2365, 51993)   off=(-195, 51993)
off ends at f=666; on runs on to f=1108
```

The `off` run's player stops dead at the position she held on frame 633 and its trace ends 442
frames early. That is not a divergence in movement, that is a death: the player froze, the death
animation played, and the replay ended. So the stock playback of a file recorded sub-stepped kills
the player at frame 633, and the sub-stepped playback of the same file does not.

Two things follow, and the second is the useful one.

First, the replay is not corrupt and the input stream is not the problem. 633 frames -- ten and a
half seconds, including plenty of movement -- match *exactly*, to the fixed-point unit. Whatever
differs between the two simulations is small enough to leave the player's integrated position
bit-identical for that long.

Second, and this is the part I had backwards for a while: the player position was never going to
find it. She is a pure function of the recorded inputs. Feed the same button presses to either
simulation and she traces the same path whether or not the bullets around her agree, right up to
the frame one of the simulations decides she has been hit. Two runs agreeing on the player says
nothing at all about whether they agree; the first frame they disagree on is the frame she dies,
which is the symptom, not the cause. The cause is upstream, in something the trace could not see,
and it could have been there since frame 1.

### 18a. What the trace samples, and the offset that was not a divergence

An earlier pass over the same logs appeared to show a divergence at frame 18, `on=(94, 51200)`
against `off=(576, 51200)`, with the `on` run holding a steady ~482 units behind through every
stretch of movement. 94 is 576/6, and 482 is five sixths of a frame's travel at that speed. That
was the instrument, not the game: `replay_trace_frame()` ran at the *end* of the update pass, and
under sub-stepping the pass that carries `g_major` is the frame's first sub-tick -- one sixth of a
frame at 360Hz. The stock run was being sampled at the end of its frame and the sub-stepped run a
sixth of the way into it, so every line differed by most of a frame of movement and nothing
smaller than that was readable.

It now samples at the top of the pass instead, before the frame's first tick runs, which is the
frame boundary in both modes. (The correct alignment is also why the run above shows 633 exact
matches where the earlier one showed a difference at frame 18 -- that pass had keyed the two logs
against the wrong playback segment as well.)

### 18b. Fingerprinting what actually kills her

`trace_state` in the profile, filled for TH14 by `th14_trace_state`, hashes the two systems that
decide whether the player is hit:

- The bullets. ZUN builds them in one call at `0x416510`:
  `array_construct(manager+0x8c, stride 0x13f4, count 0x7d1, ctor 0x416070)`, with the manager at
  `*(void**)0x4db530` and its total allocation `0x9bf6d0` = `0x8c + 2001 * 0x13f4`, which is the
  arithmetic that confirms the three numbers. State word at `+0x20`, position floats at `+0x28`
  (the same `lea ecx, [esi+0x28]` the movement code passes around).
- The enemies, walked exactly as the interpolation walks them.

Every slot is hashed, live or dead. A dead slot still holds the bytes the last bullet to occupy it
left behind, and those bytes are a function of the run's history too, so a stale slot that differs
between two runs is a divergence that the live slots have already forgotten. Raw bits are mixed,
not values: the question is whether two runs are identical, not whether they are close.

The third field is a count of slots with a non-zero state word -- a cruder signal than the hash,
but one that reads at a glance and that says whether the two runs are even spawning the same
number of things.

The next pair of logs should show `b=` and `e=` parting company well before frame 633, and the
frame they part on is the frame to go and read.

## 19. The desync is in the bullets, at frame 372

The second pair of logs, with the fingerprint in and the sampling point fixed:

```
first difference   x        frame 634   (the death, as before)
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

Three facts, and each of them narrows it.

**The enemies never differ.** Not once in 667 frames. The ECL scripts, the enemy positions, and
everything they draw from the RNG are identical in the two runs. So this is not an RNG divergence
and not a sub-stepping error in the enemies -- which is what the decision not to sub-step them,
and to interpolate their sprites instead, was supposed to buy, and it is nice to see it hold under
a bit-exact test.

**The bullets first differ at frame 372, and the live count is still equal on that frame.** 203 in
both. So frame 372 is not a bullet that failed to spawn or one that was culled early; it is a
bullet whose *state* -- a position, a velocity, a timer -- came out different while every bullet
that should exist still exists. The count only parts company on the following frame.

**From 373 the sub-stepped run has steadily fewer bullets**, and the gap widens: 1, 2, 3, 3, 3, 3,
4. Both runs are spawning at about the same rate (~2-3 a frame, a stream), so this is not a spawn
that stopped; it is bullets leaving slightly early, which is what you would expect downstream of a
position or velocity that came out wrong on 372 -- the off-screen test at `0x416620` culls on
`pos ± vel * 0.5` against `[-192, 192]` by `[-64, 448]`, so a bullet that is a little further along
than it should be crosses that line a frame or two sooner.

Fewer bullets is also why the *sub-stepped* run survives and the stock one dies. The player is not
being killed by the mod; she is being killed by the replay being played back in a simulation that
is not the one that recorded it, and the recording simulation is the one with the thinner bullet
pattern.

### 19a. One hypothesis, checked and dead

The obvious suspect was a countdown. A bullet carries two of them, at `+0x10a8` and `+0x12e8`,
each the mirror image of the game's own timer -- `-= rate` where the timer does `+= rate`, same
rate-pointer field, same "if the rate is between 0.99 and 1.01, skip the multiply" fast path at
`0x41698e` and `0x416a2c`. Both have a branch that subtracts a whole 1.0 when the rate pointer is
null, and a per-frame quantity on a per-tick path is exactly the class of bug that has bitten this
port three times already.

It is not this. `0x4190bb` stores `&0x4d8f58` into `+0x12f4` directly, and the other timer is
initialised through the generic helper at `0x408b00`, which stores the same pointer into its
`+0xc` -- that is `+0x10b4`. Both scale. The null-rate branch is not reached by a bullet.

Worth recording because the check cost one look at `0x408b00` and would otherwise have cost a
build, a round trip, and a wrong fix.

### 19b. `trace_dump`

So: stop reasoning and look at the bullet. `trace_dump` writes one line per live slot for the
frames between `replay_trace_from` and `replay_trace_to` -- slot index, state word, the timer at
`+0x24`, the position and velocity triples, the rest of the motion block, the flag word at `+0xc04`
and both countdowns' integers, all raw hex so the comparison is exact rather than approximate.

The slot index is what makes this work. The bullets are an array of 2001 fixed slots, not an
allocation, so slot 37 is slot 37 in both runs for as long as the runs have agreed -- which is
precisely the situation a window opened just before the first difference is in. Diff the two logs
across the window: the lines that differ name the bullet, and the column that differs names the
field that went wrong.

## 20. A delay that starts one frame early

The per-bullet dump over frames 369-374, diffed:

```
f=372, 203 live slots in both runs
  c1 (the delay countdown at +0x10ac):  115 slots one lower under sub-stepping, 88 equal
  every differing row has g=80000000    (the delay flag set)
  rows differing in anything else:      3
    i=1811   off s=00000013   on s=00000012
```

And following individual slots across the window:

```
slot 1829   f369 60/60   f370 60/60   f371 60/60   f372 60/60   f373 60/59   f374 59/58
slot 1831   f369 60/60   f370 60/60   f371 60/60   f372 60/59   f373 59/58   f374 58/57
slot 1833   f369 60/60   f370 60/60   f371 60/59   f372 59/58   f373 58/57   f374 57/56
```

Two bullets begin counting each frame, in descending slot order -- a stream. The countdown itself
is fine: once it starts it runs at exactly one a frame in both runs, so nothing is accumulating
wrong and no float is drifting. What differs is *when it starts*. Under sub-stepping every one of
them starts exactly one frame early, and stays exactly one frame ahead for the rest of its life.

That is a much better-shaped fact than "the bullets diverge". It is not a rate error, it is a
single discrete event landing on the wrong frame, 115 times over.

Downstream: at frame 372 three bullets reach the end of that delay a frame early and flip bit 0 of
their state word (`0x13` to `0x12`), which is the difference the fingerprint caught. From there
positions differ, bullets that are a little further along cross the off-screen test at `0x416620`
a frame or two sooner, the live count starts falling behind, and by frame 634 the stock playback
of a sub-stepped recording is in a thicker pattern than the one that was recorded and the player
is hit.

Worth saying plainly: the sub-stepped run is not the broken one. It is the one the replay was
recorded in, and it plays its own recording back correctly. The stock playback is the one being
fed inputs from a simulation it is not running.

### 20a. What is left to identify

A bullet the update skips entirely is one whose state word at `+0xc0e` is outside 1..5 -- the
jump table at `0x4167dd` dispatches on it and anything else lands at `0x416c40`, past every timer
in the function. So a "parked" bullet is one in state 0, and the event that starts its countdown
is the state word changing. That change is what happens a frame early.

Three timers could plausibly drive it and all three are correctly scaled, which is why the dump is
the way to settle it rather than more reading:

- the bullet's age timer at `+0x13d4`, rate pointer at `+0x13e0`, set to `&0x4d8f58` at `0x416fdb`
  and `0x417961`;
- the delay countdown at `+0x10a8`, rate at `+0x10b4`, set by the helper at `0x408b00`;
- the second countdown at `+0x12e8`, rate at `+0x12f4`, set at `0x4190bb`.

The dump now carries the state word, `+0x13c4`, the age timer, and all three rate pointers. A null
rate pointer read off a live bullet is a timer running per tick instead of per frame, and seeing
it on the bullet settles in one line what reading the constructors only settles for the paths the
constructors happen to be on.

### 20b. And the fix for the symptom, whatever the cause turns out to be

Even once this is found and fixed, a replay recorded at one rate and played back at another will
not be bit-identical in general -- sub-stepping changes the order in which things happen inside a
frame, and some of that is not recoverable. The fix for what the user actually hit is the replay
extension from §17: stamp the recording's logic rate into the file and play it back at that rate.
That makes a recording play in the simulation that made it, which is the only thing that can be
promised. The work here is worth doing anyway, because a sub-stepped game whose bullet delays are
a frame short is a slightly different game from the one ZUN wrote, replay or no replay.

## 21. It is the state word, and the gate is `+0x4d4`

Fourth pair of logs, with the state word and all three rate pointers in the dump. Following one of
the transitioning bullets:

```
       off                                              on
f369   c1=60 st=2 k=10 age=9,10,41200000               c1=60 st=2 k=10 age=9,10,41200000
f370   c1=60 st=2 k=11 age=10,11,41300000              c1=60 st=2 k=11 age=10,11,41300000
f371   c1=60 st=2 k=12 age=11,12,41400000              c1=60 st=2 k=12 age=11,12,41400000
f372   c1=60 st=2 k=13 age=12,13,41500000              c1=60 st=2 k=13 age=12,13,41500000
f373   c1=60 st=2 k=14 age=13,14,41600000              c1=59 st=1 k=14 age=13,14,41600000
f374   c1=59 st=1 k=15 age=14,15,41700000              c1=58 st=1 k=15 age=14,15,41700000
```

Everything countable is identical. The age timer's float is `0x41200000`, `0x41300000`,
`0x41400000` -- exactly 10.0, 11.0, 12.0 -- in both runs, every frame, so the dyadic sub-step
sequence is doing its job and the accumulation is bit-exact. `k` is identical. The rate pointers
read `004d8f58, 004d8f58, 00000000`: the two timers that are in use both carry the game speed, and
the null one belongs to the second countdown, which this bullet never starts.

The only thing that differs is `st`, the state word at `+0xc0e`. It goes 2 to 1 one frame early,
and the countdown starts with it -- which is not a coincidence: the jump table at `0x4167dd` sends
state 2 to `0x4167e4`, and that handler *falls through* into the state-1 body at `0x416883` on the
same call it promotes the bullet. So the frame the state changes is the frame the countdown takes
its first step, and everything after is one frame ahead for good.

### 21a. The gate

`0x4167e4`, the state-2 handler, ends:

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

`k >= 8` holds from well before the window and `0x416d70` is the player collision test (it calls
`0x44ee80`/`0x44efa0` and sets state 3 on a hit), so neither of those is it. The gate is
`[esi+0x4d4]`: while it is zero the bullet stays in state 2, and it becomes non-zero one frame
early under sub-stepping.

Nothing in the bullet code writes `+0x4d4`. It is written through the motion state the bullet
carries at `+0x28` -- `+0x4d4` is `motion+0x4ac`, a near neighbour of the `motion+0x4a0` that
`0x416e3a` sets to 1 on a player hit.

### 21b. The last instrument

So dump the motion state. For bullets in state 2 only -- which is exactly the set in question --
the dump now prints `+0x28` through `+0x610` as 32 dwords a line with the offset in front. Diffing
two runs over the window localises the difference to a 128-byte chunk and then to a single dword,
and a dword at a known offset in a known object is an address to look up rather than another guess.

## 22. It was a read-after-write across the update list

The motion-state dump, diffed over frames 369-374 for the bullets in state 2 in both runs:

```
differing dword offsets, counted over every state-2 bullet and frame
  +0x050 +0x054 +0x058 +0x060   195 each   heap pointers (0x12fbc9d0 vs 0x12da39d0) -- two processes
  +0x064 +0x068                 195 each   floats
  +0x520 .. +0x548              195 each   four (x,y) pairs, the draw quad
  +0x4d4                          0        the gate
```

Most of that is noise: the pointers differ because these are two runs of the process, and the quad
is where the bullet is drawn, which under sub-stepping is an interpolated position and *should*
differ. The line that matters is the one that is not there. `+0x4d4` -- the word the promotion at
`0x41686a` is gated on -- reads **zero at every frame boundary, in both runs, for every bullet that
is still in state 2**. It is never observed non-zero, in either run.

That is the answer, and it is not an arithmetic answer.

Nothing in the bullet code writes `+0x4d4`; it belongs to the motion state the bullet carries at
`+0x28`, and it is written **later in the frame than the bullet update runs**. At one tick a frame
the write always lands after the check, so the bullet fails the gate, waits, and is promoted on the
next frame -- and by the time anything looks at the word again, at the next frame's boundary, it
has been consumed and cleared. Sub-stepped, the check runs another five times before the frame is
out, and the second of them sees the write the first one missed. The bullet is promoted inside the
frame it was supposed to wait through, and since the state-2 handler falls through into the
state-1 body on the same call, its delay countdown takes its first step there too. One frame ahead,
for the rest of its life.

So it was never the timers. Every timer was exact -- the age float read 10.0, 11.0, 12.0 to the
bit. It was the *order of operations within a frame*, which sub-stepping changes by construction:
a system that runs six times a frame can see, five times out of six, state that a system running
once a frame produces after it.

This is a fourth hazard class, and the one that does not announce itself. The first three are
arithmetic -- an integer the update moves itself, a block gated on "the timer's integer is N", a
truncation -- and they can all be found by reading the code that does the arithmetic. This one is
invisible in the code that reads the value. `cmp dword [esi+0x4d4], 0` is a correct instruction; it
is correct six times a frame; what is wrong is that it is *asked* six times a frame, and the answer
changes partway through.

### 22a. The fix

There is no arithmetic fix, because there is no arithmetic. Gate the promotion to the frame
boundary:

```c
gate_block(0x416877, 12, 0x416c40, 0, -1);
```

Twelve bytes -- `mov eax, 1` and `mov word [esi+0xc0e], ax` -- behind the existing frame-boundary
gate, skipping to `0x416c40` on a tick that is not one. The bullet then asks the question exactly
once a frame, at the same point in the update list where stock asks it, and gets the answer stock
gets. The state-2 motion at `0x4167e4` is untouched and still runs every tick, so a bullet that is
entering still moves smoothly; only the discrete promotion is pinned to the boundary, which is
where a discrete event belongs.

This is the same shape as the player's state dispatch at `0x44d924` and, for that matter, the
decision not to sub-step the enemies at all: the continuous part runs at the display rate and the
decisions run at 60Hz.

### 22b. What to check next

The general question this raises is how many other places read, on a sub-tick, something another
system writes later in the frame. The census machinery can answer it for a specific suspect but
cannot find them, and neither can reading -- the read looks correct. The practical test is the one
that found this: play one replay back in both modes with the fingerprint on and see whether the
bullet and enemy hashes now agree for its whole length. If they do, there are no others on any
path that replay exercised, which is a great deal more than reading could establish.

## 23. The gate was not it

Two playbacks with the frame-boundary gate in, against the two without it:

```
                 before the gate                after the gate
f=372   off b=ac96e06e n=203   on b=46dec4bf n=203      identical
f=373   off b=9e194c45 n=206   on b=3c0b9623 n=205      identical
f=374   off b=3d228d2c n=209   on b=3e28b593 n=207      identical
```

Bit-identical, all 667 frames, both runs. The DLL is the new one -- 64 verified signatures against
63, 49 patches against 48, 1149 bytes of stubs against 1115 -- so the gate is installed and does
nothing.

A gate that never fires means **no bullet was ever promoted on a minor tick**. The promotion
happens on the frame boundary in both modes, at the same point in the update list, and still comes
out a frame apart. So §22 was wrong: it is not a read-after-write across the list, or at least not
one that the promotion site can see.

Which leaves something narrower and stranger. At the top of the frame, before any tick runs,
`+0x4d4` reads zero in both runs. A little later in the same boundary tick, when the bullet update
reaches `0x41686a`, it reads non-zero under sub-stepping and zero without. Same frame, same tick,
same point in the list, and something between the two moments differs.

I have a list of things it could be and no reason to prefer one, which is the point at which
guessing stops paying. So: read the gate.

### 23a. `th14_gate_log`

A debug-only hook on `0x41686a` itself, the `cmp dword [esi+0x4d4], 0`. Every state-2 bullet that
reaches it inside the trace window writes a line: the replay frame, the slot, `g_major`, the
sub-tick number, the gate's value as read at that instant, and the state, `k`, `c1` and age
alongside. XMM is saved around the call for the same reason the speed stubs save it.

That is one line per state-2 bullet per tick -- about 33 x 6 x 6 in the window -- and it answers
all of it at once: whether the two runs reach the gate on the same tick, what the gate says when
they do, and on which tick the value turns over. Whatever the mechanism is, it cannot hide from a
log of the actual comparison.

The boundary gate from §22 stays in for this build. It demonstrably changes nothing, so it cannot
contaminate the measurement; whether it earns its place gets decided once the mechanism is known.
