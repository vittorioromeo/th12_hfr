# Shared Touhou HFR runtime

The v0.2.0-test build contains one runtime and two game adapters. The same DLL
supports TH11 v1.00a and TH12 v1.00b, including the static English executables
tested with these layouts. Other games are not supported yet.

## Source map

| Component | Location | Responsibility |
| --- | --- | --- |
| Build entry | `src/hfr.c` | Includes the modules once, in dependency order |
| Executable identity | `src/identity.h`, `src/games/*_signatures.h` | PE layout and frozen code signatures, shared with the launcher |
| Game contract | `src/game_profile.h` | Addresses, object offsets, node classification, hook and sprite callbacks |
| Shared core | `src/core/` | Patch transactions, x86 emission, dyadic scheduler, input, interpolation history, frame pacing, replay extensions, configuration and proxy |
| Update backend | `src/backends/update_runner.c` | TH11/TH12 update-list ABI, node return values, pause boundaries and player input edges |
| Renderer backend | `src/backends/d3d9.c` | D3D9/9Ex, resource conversion, display refresh, reset and latency |
| Game adapters | `src/games/th11.c`, `src/games/th12.c` | Actual game addresses, exact x86 hooks and sprite placement |
| Launcher | `src/launcher.c` | Detect and launch a supported executable using the common identity code |
| Tests | `tools/test_hfr.c`, `tools/test_*.h`, `tools/test_*_stubs.py` | Native shared-runtime tests and emulation of emitted x86 hooks |

This is deliberately a unity build: the small C modules form one translation
unit, which keeps the existing assembly entry points and their C symbols together.
The core is compiled once, regardless of the number of registered games.
`hfr11.c` and `launcher11.c` are compatibility includes, not separate implementations.

The shared core uses profile fields and callbacks rather than checking the game
number. Game IDs occur in executable registration and replay metadata. The twoE
backends currently describe the engine family shared by TH11 and TH12; they are
not assumptions that every Touhou title uses this ABI or renderer.

## Detection and installation

Detection requires x86 PE32 at image base `0x400000`, the exact expected image
size, and every frozen signature (63 for TH11, 59 for TH12). These cover all
overwritten code ranges and selected native function entries. This is code-layout
verification, not a whole-file checksum: resources and English text may differ.
It does not establish compatibility with arbitrary third-party runtime patches.

The DLL selects its adapter from the loaded image. The launcher maps a file as
inert bytes and applies the same checks before creating a process. If both games
are present it requires an explicit target; within one game it prefers English.
`touhou_hfr.exe --check <path>` performs read-only detection, returning 0 for a
supported file and 2 otherwise.

Code writes and resolved import-thunk writes are queued in one transaction. All
code writes require a frozen signature; overlaps are rejected. Every target must
be writable and still match its preflight bytes before the transaction commits.
Protection is restored in reverse order because multiple patches can share a page.
The executable stub buffer and patched instructions are flushed from the CPU
instruction cache before use. A process-scoped mutex prevents two copies loaded
under different DLL names from applying the patch twice.

`install.ps1` verifies the executable, requires the game to be closed, backs up
the files it will replace, installs the common files and updates existing legacy
DLL/launcher aliases. It preserves an existing common INI, or copies the game's
legacy INI for a first upgrade. It never modifies the game executable or data.

## Simulation behavior in this refactor

TH11's validated game-specific stubs retain the same 952 emitted bytes, apart
from relocation to the new runtime's variables. Its edge handling and partner
hooks remain in its adapter. TH12's original site hooks are retained, with its
movement residual now using the shared resettable storage and bypassing the
residual at stock speed, as TH11 already does.

Both games now use the TH11 port's scheduler fixes: `substep=0` selects 60 Hz
logic; presentation pacing is independent of a replay's logic rate; device resets
retain an active replay's rate and tick sequence; stage/rate changes reset motion
residuals; duplicate presentation frames do not capture a new enemy position.
Input polling reads the actual raw input word rather than relying on an undefined
native return value. TH11 alone masks player pressed/released/bomb edges on minor
ticks, preserving the previously tested behavior of both adapters.

Enemy logic remains at 60 Hz. Interpolation history is shared; TH11's child sprite
propagation and TH12's slot offsets/parent relationships remain separate callbacks.
The presentation configuration is common to both games.

## Replay extensions

The original game's compressed replay payload is unchanged. Extensions are USER
chunks appended after it, using the native header's user-data offset when reading:

| Type | Format | Meaning |
| --- | --- | --- |
| `0x48` | Text containing `rate=N` | Legacy recording logic rate |
| `0x49` | `HFRI`, schema 1 | RLE movement/focus input per sub-tick, by stage |
| `0x4a` | `HFRM`, schema 1 | Game ID, simulation revision, gameplay settings and logic rate |

The HFRM payload is little endian: `char magic[4]`, `u16 schema`, `u16 game`,
`u32 simulation_revision`, `u32 flags`, `u32 node_mask`, `u32 logic_rate`.
Including the 12-byte USER header, the chunk is 36 bytes. Flag bit 0 is substep;
bit 1 is sub-tick input. Node-mask bits use the adapter's classification order.
Changing that order or simulation behavior requires a new simulation revision.
The current revision is **1**, independent of the release version.

Compatible metadata restores the recorded substep, input and subsystem settings
for playback, then restores the user's settings on exit. Refresh/vsync/9Ex remain
local presentation choices. Legacy replays retain their rate and input streams,
with a log notice that their simulation version is unknown. Unsupported or invalid
HFRM metadata produces an on-screen compatibility warning. Playback is best effort
in that case: the native replay load function has no error return to propagate.
Keep the original recording build when exact playback of older replays matters.

Extensions are assembled before writing. Malformed RLE lengths, duplicate stages,
invalid input bits and oversized streams are rejected. Recording allocation/size
failures do not silently serialize a partial input stream. A failed append attempts
to truncate back to the original replay length. These safeguards do not replace
validation of the game's own compressed replay format.

## Adding another game

1. Confirm the supported executable version, object layouts, update-list calling
   convention and renderer. Use another backend if those differ; do not clone the core.
2. Add an identity with reviewed byte signatures and a game profile. Register its
   identity and profile in `identity.h` and `core/install.c`.
3. Audit every subsystem to decide what can run at sub-tick rate. Review per-call
   counters, RNG, callbacks, integer truncation, slow motion, pause and character
   mechanics. Address translation alone is insufficient.
4. Implement the required game hooks and sprite placement. Every overwritten
   range must have a frozen signature. Add each supported executable as a local
   fixture and emulate the generated stubs, including alternate branch paths and
   register/stack/FPU effects.
5. Test in game: all character/partner combinations, stages, bombs, items, pause,
   slow motion, fullscreen/reset, stock/HFR replay recording and playback at
   different display rates. A matching executable signature is not a gameplay test.

## Validation for v0.2.0-test

Automated tests pass on all four supplied executable fixtures (JP and English,
TH11 and TH12). They cover the actual full installer, required imports, each
signature's rejection path, patch transaction failure, all integer scheduler
rates 60–1000 Hz, cross-rate presentation, synthetic eight-stage replay file
round trips, metadata/settings, update-list pauses and input edges, and the
emitted gameplay stubs including native SSE/x87 float-to-integer paths.

The tests map game files as inert local data and never ship them in a release.
The pre-refactor TH11 port passed the user's gameplay test. This unified build
still needs manual gameplay and full-run replay testing in both games; the
automated checks do not prove every character/stage combination correct.

Detailed reverse-engineering notes remain in `DEVNOTES.md` (TH12) and
`TH11_DEVNOTES.md` (TH11). They describe the original implementations; use the
source map above for their current locations.
