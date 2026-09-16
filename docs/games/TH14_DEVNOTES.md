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

## 10. What a trace still has to supply

The UpdateFunc class table and the dimming rules cannot be read out of the executable. Both
come from the patch's own log while a stage is running: the registered update list names every
callback, and the per-callback sprite census is what the dim rules are written against
(DEVNOTES_RUNTIME §3b). Guessing either is how a bullet manager ends up stepping at 360 Hz
with its own timers still counting in frames — §7 of the runtime notes is what that costs.

Also outstanding: the English and Steam builds (only `th14.exe` is recognised so far),
`vpatch_th14.dll`'s conflict sites, `sprite_round_sites` for internal resolution, the replay
magic (`t14r` is the obvious guess and nothing reads it yet), and `data_dir` — TH13 needed one
because it saves replays under `%APPDATA%`, and whether TH14 does has not been checked.
