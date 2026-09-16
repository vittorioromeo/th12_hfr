# Testing

Two suites, neither a superset of the other. A release should be validated from PowerShell,
because that is the side that runs the Unicorn emulation of the emitted machine code; the shell
side is what runs in development, because that is the side with Wine and a display.

Everything writes into `build/tests/`, which is ignored. The harness maps your real game
executables as **inert fixtures** — it never starts a game, never calls its imports, and never
ships one.

## The native harness — `tools/test_hfr.c`

Both suites build and run this first, once per game executable given on the command line. It is
the bulk of the testing.

It is linked with `--section-start,.fixture=0x400000` so that a 2 MB array sits exactly where
the game's image would be, and `tools/fill_pe_gaps.py` reserves the virtual gaps afterwards so
Windows cannot place the process's own heap there. The executable under test is then mapped into
that array and everything below runs against the real bytes.

What it establishes, in order:

- **Identification.** The executable is recognised from its frozen signatures. Then every
  signature is flipped one byte at a time and must be rejected, and truncated headers must be
  rejected — a detector that accepts everything proves nothing.
- **DRM-wrapped images.** A Steam-style wrapper is synthesised around the image: the patch must
  identify the game once the code is readable, and must name it as wrapped while it is not.
- **thprac's launcher marker.** Recognised when present, and absent from a clean executable.
- **The frame-loop guard.** Every recorded conflict site is corrupted in turn and must be
  noticed; a clean executable must trip none of them; and identification must survive a patched
  site, because a game vpatch has touched is still that game.
- **The shared subsystems**, with no game needed: the scheduler at every integer rate from 60 to
  1000, cross-rate presentation, the replay codec against malformed input, output scaling
  geometry, window aspect snapping, and menu key handling.
- **The simulation**, for a profile that describes one: the replay round trip, the shared update
  runner (boundary and minor ticks, input edges, list stop, immediate pause), and the runner's
  tail jump — that `addr.runner_ret` really is the game's return instruction, that the fallback
  is taken when it is not, and that the thunk lands there with the stack and return value intact.
- **The patch transaction.** A failed transaction must leave every byte of game code unchanged,
  and a write to an address with no frozen signature must be refused.
- **The whole install**, for real: every hook applied, no overlaps, and the import redirection
  checked against a reproduction of thcrap's own detour — the taken-back import must chain to
  the other patch rather than discard it.

It then dumps `<name>.game`, `<name>.stubs`, `<name>.json` and `<name>.patches.json`: the patched
image, the emitted stubs, the addresses of the runtime globals, and every byte the patch writes.
Those are what the later stages and the audit tools consume.

## `./test.sh <game.exe> [...]` — shell

The native harness for each executable, then:

- `tools/check_docs.py` — every markdown link and heading anchor in the tree.
- **Menu contents** (`tools/test_menu_logic.cpp`): what each section of the F11 menu offers, with
  no device and no window. A control that is silently not drawn is invisible to the device test.
- **The in-game menu** (`tools/test_menu.c`): the overlay on a real Direct3D 9Ex device, with the
  same swap chain, render target and state block the patch uses. Needs a display; skipped when
  `DISPLAY` is unset.

## `.\test.ps1 -GameExe ... [-Python ...]` — PowerShell

The native harness for each executable, then, per executable:

- `tools/verify_game.py` — identifies the executable from `tools/th1*_signatures.json`, which is
  generated from the source headers (see below).
- `tools/test_th<NN>_stubs.py` — **the emitted x86 stubs executed in Unicorn**, against the
  `.stubs` and `.json` dumps. This is the only place the machine code the patch writes is run
  rather than merely diffed, and it is why a release is validated from Windows.
- `touhou_hfr.exe --check` — the launcher's own detection path.

Needs `pefile` and `unicorn` (`python -m pip install pefile unicorn`).

## The x64 suite — `test64.sh` / `test64.ps1`

For the New Classic backend: the clock, the pose history, the sub-step schedule, the real patch
transaction, every emitted AMD64 relay, the frozen signatures, the dimming rules and pools, the
menu's contents, and the dxgi proxy — both that it forwards correctly and that it starts the
runtime under its own basename. `test64.sh` finds a 64-bit Wine itself. Run `build.ps1` before
`test64.ps1`, which checks both launcher architectures.

## Generated files

`tools/th1*_signatures.json` is generated from `src/games/th1*_signatures.h` by
`tools/gen_signature_json.py`. Both test suites regenerate it and fail if the committed copy has
drifted, so the header stays the single place a signature is written down.

`src/core/shader_sources.h` is generated from `shaders/*.hlsl` by `tools/embed_shaders.py`. No
build step runs it; regenerate it by hand after editing a shader.

## What the suites do not cover, and has to be played

The automated tests prove the patch installs the code it meant to and that the code does what it
says in isolation. They say nothing about whether the game still plays correctly. Before a
release, on each game:

- **Game speed.** A full stage should take exactly as long as it does unpatched — stage timers,
  boss spell timers.
- **Flow.** Pause and unpause, game over → continue, game over → title, stage clear, stage
  transitions.
- **Gameplay.** Every shot type, bombs, grazing bullets and lasers, item collection, boss death
  slow-motion, and whatever the game has of its own (TH12's UFO summons, TH13's spirits).
- **Replays.** Record a run and play it back on the same display; it should reproduce exactly,
  and the log should report per-tick input at each stage start. Play a stock replay too.
- **Input feel.** Tapping a direction should move the character for as little as one tick. The
  log's `stats:` lines should show `subtick polls` at roughly refresh − 60 per second while
  playing, and `SetMaximumFrameLatency(1)` should have succeeded.
- **The window.** Windowed and fullscreen, Alt+Enter, Alt-Tab, resizing, borderless fullscreen,
  screenshots, and a 60 Hz display (where the patch should be a no-op).

Include both the Japanese and the English executable of each game.
