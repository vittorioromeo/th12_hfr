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

## 5. What a trace still has to supply

The UpdateFunc class table and the dimming rules cannot be read out of the executable. Both
come from the patch's own log while a stage is running: the registered update list names every
callback, and the per-callback sprite census is what the dim rules are written against
(DEVNOTES_RUNTIME §3b). Guessing either is how a bullet manager ends up stepping at 360 Hz
with its own timers still counting in frames — §7 of the runtime notes is what that costs.

Also outstanding: the English and Steam builds (only `th14.exe` is recognised so far),
`vpatch_th14.dll`'s conflict sites, `sprite_round_sites` for internal resolution, the replay
magic (`t14r` is the obvious guess and nothing reads it yet), and `data_dir` — TH13 needed one
because it saves replays under `%APPDATA%`, and whether TH14 does has not been checked.
