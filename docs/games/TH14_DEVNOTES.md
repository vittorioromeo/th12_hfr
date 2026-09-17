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

### Why nothing is classified yet, even the certain ones

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
`draw.vm_reg` has no way to say that. That is the fifth thing this rebuild moved, and it needs
a `vm_stack_arg` in the same family as `runner_arg`, `frame_ctx_ecx`, `cleanup_this_ecx` and
`screenshot_stack_arg`. The VM struct also grew -- `+0x594` became `+0x5bc` -- so the three
field offsets are their own derivation and not a copy of TH13's.

Nothing is guessed here: `vm_draw` stays out of the profile until the stack form exists, for
the same reason `speed` does. A wrong `vm_anm_off` would make the trace print plausible ANM
names that are not the ones being drawn, and the dim rules and the class table would both then
be written against fiction.

## 14. What a trace still has to supply

The UpdateFunc class table and the dimming rules cannot be read out of the executable. Both
come from the patch's own log while a stage is running: the registered update list names every
callback, and the per-callback sprite census is what the dim rules are written against
(DEVNOTES_RUNTIME §3b). Guessing either is how a bullet manager ends up stepping at 360 Hz
with its own timers still counting in frames — §7 of the runtime notes is what that costs.

Also outstanding: the English and Steam builds (only `th14.exe` is recognised so far),
`vpatch_th14.dll`'s conflict sites, `sprite_round_sites` for internal resolution, the replay
magic (`t14r` is the obvious guess and nothing reads it yet), and `data_dir` — TH13 needed one
because it saves replays under `%APPDATA%`, and whether TH14 does has not been checked.
