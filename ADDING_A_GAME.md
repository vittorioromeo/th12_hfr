# Adding a game

The patch is a shared runtime with a thin per-game adapter. Everything that knows an address
lives in `src/games/`; nothing in `src/core/`, `src/backends/` or `src/ui/` names a game or
hardcodes one of its addresses, and that is worth keeping true — it is what makes the video,
menu and filter work apply to a new game for free.

A game is three files and three lines.

## The three files

`src/games/thNN_signatures.h` — frozen bytes at every address the patch writes to, plus a few
it only reads. These identify the executable: identification fails unless *all* of them match,
so a build the addresses were not taken from is rejected rather than corrupted. The harness
proves each one individually rejects a modified image.

`src/games/thNN.c` — the `GameProfile`: addresses, structure offsets, and the table of
UpdateFunc classes saying which of the game's systems may be sub-stepped (`MODE_SUB`) and which
must keep stock 60 Hz behaviour (`MODE_FRAME`).

`src/games/thNN_conflicts.h` — the handful of frame-loop sites another patch is known to take
over, with the bytes a clean executable has there. Optional: a game may register `NULL, 0` and
the module-name check still covers it. See `src/core/conflict.c` for what this is for.

## The three lines

- two `#include`s in `src/identity.h`
- one row in `game_identities[]` there
- one entry in `game_profiles[]` at the top of `src/core/install.c`

## Three states a game can be in

`provisional = 1` in the profile: identified, and then left completely alone. Nothing is
hooked. Use this while a game is still being worked out -- a game that installs and then
faults is worse than one the patch does not claim to support, because the second is obvious
and the first is a bug report.

No simulation addresses (`runner_fn` and `frame_calls` left zero): the whole video path
installs and the simulation is untouched, so the game runs at its stock 60 Hz with every
scaling, filter and menu feature working. `install()` says so in the log, the menu disables
the timing controls with the reason, and the harness skips the tests that drive game
structures.

Fully described: everything.

## What comes for free, and what does not

Free, because none of it is game-specific: the whole video path (arbitrary window resizing,
aspect fit, pixel-perfect scaling, borderless fullscreen, every filter, multi-pass shader
chains), the in-game menu and all its settings, the presentation chain and its wrapper
detection, the crash reporter, and the conflict guard's module check.

Not free, because each needs an address: the scheduler and sub-stepping (the UpdateFunc class
table), replay extension, sub-tick input, the screenshot stub (`screenshot_fn` /
`screenshot_call`), and the conflict guard's byte check.

Anything a profile leaves out degrades rather than breaks. A game with no `screenshot_fn` logs
that its screenshots are unsupported and runs; a game with no conflict sites keeps the module
check. Prefer that to a half-filled field: a wrong address is worse than a missing one, because
the frozen signatures cannot tell you a *right-looking* address is pointing at the wrong thing.

## Finding the two newest fields

Both were added for TH12 and then found for TH11 by pattern, which is the method to reuse.

**The screenshot routine.** Find the string `snapshot/th%.3d.bmp` in the executable, find the
one place that references it, and disassemble forward: the filename is built there and the
routine is called about 0x46 bytes later behind a `lea eax,[esp+8]`. TH12 calls `0x42fca0` from
`0x450891`; TH11 calls `0x429ca0` from `0x446901`. Confirm by comparing the two routines'
instruction sequences — they were identical for TH11 and TH12, 61 instructions to the first
`ret`. The stub must not disturb EAX, which holds the filename.

**The conflict sites.** Collect every 32-bit immediate in `vpatch_thNN.dll` that lands in the
game's code section, then keep the ones whose code has the right shape: `a1 xx xx xx 00 8b 08`
(`mov eax,[device]; mov ecx,[eax]`) is the Present call, and the frame limiter and replay
timing are `e8` calls to a common timing routine. Check the bytes are the same in the Japanese
and English executables before recording them — a per-language difference would refuse to
install for someone whose game is fine. Then verify by actually running the game through
`vpatch.exe` and watching the guard name the site.
