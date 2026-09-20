# Shared Touhou HFR runtime

For why things are the way they are, rather than what they are, see
[DEVNOTES_RUNTIME.md](docs/DEVNOTES_RUNTIME.md).

The current build contains **two runtimes** and five game adapters.

`touhou_hfr.dll` (x86, Direct3D 9) supports TH10 v1.00a, TH11 v1.00a, TH12 v1.00b
and TH13 v1.00c, including the static English executables tested with these layouts.
Everything below describes that runtime unless it says otherwise.

`touhou_hfr64.dll` (AMD64, Direct3D 11) supports **Touhou Koumakyou: New Classic**,
experimentally. It is a separate binary with its own clock, patch set and renderer;
it shares source with the x86 runtime only where that is possible (the F11 menu, the
settings API, the patch transaction, the launcher's identity registry). The two
cannot share object code. What each does and does not do is set out side by side in
[TH06NC_VS_TH10_13.md](docs/games/TH06NC_VS_TH10_13.md); the research record is
[TH06NC_DEVNOTES.md](docs/games/TH06NC_DEVNOTES.md). Other games are not supported yet.

## Source map

| Component | Location | Responsibility |
| --- | --- | --- |
| Build entry | `src/hfr.c` | Includes the modules once, in dependency order |
| Executable identity | `src/identity.h`, `src/games/*_signatures.h` | PE layout and frozen code signatures, shared with the launcher |
| Game contract | `src/game_profile.h`, `src/dim_classes.h` | Addresses, object offsets, node classification, hook and sprite callbacks, and the dimming classes both runtimes share |
| Shared core | `src/core/` | Patch transactions, x86 emission, dyadic scheduler, input, interpolation history, frame pacing, dimming, texture scaling, replay extensions, configuration and proxy |
| Import hooks | `src/core/imports.c` | Import-table hooking that chains to whoever held the slot, and takes a slot back if another patch overwrites it |
| Other patches | `src/core/conflict.c` | Recognising a patch that owns the frame loop, the launcher marker thprac leaves, the module census and the one notice box a run may show |
| Update backend | `src/backends/update_runner.c` | The TH10–TH13 update-list ABI, node return values, pause boundaries, player input edges, and ending the pass on the game's own return instruction |
| Renderer backend | `src/backends/d3d9.c` | D3D9/9Ex, resource conversion, display refresh, reset and latency |
| Output scaling | `src/core/scaler.c` | Render-target redirection, scaling geometry, filter passes, present blit |
| Filter shaders | `src/core/shaders.c`, `shaders/` | Runtime shader compilation, filter registry, drop-in shader folder |
| Window | `src/core/window.c` | Resize border, aspect snapping, borderless fullscreen, deferred reset |
| In-game menu | `src/ui/` , `third_party/imgui` | Dear ImGui overlay; `ui_api.h` is the whole C/C++ interface |
| Game adapters | `src/games/th10.c`, `th11.c`, `th12.c`, `th13.c` | Actual game addresses, exact x86 hooks and sprite placement |
| Older-engine adapter | `src/games/th08.c`, `src/backends/d3d8.c`, `d3d8_bridge.cpp`, `third_party/d3d8to9` | TH08: the Direct3D 8 bridge, a walker for the TH06-era Chain on the shared frame scheduler, and presentation from a 60 Hz simulation — predicted playfield, input-led player, rigid-quad smoothing, camera smoothing, dimming in the quad hook ([TH08_DEVNOTES.md](docs/games/TH08_DEVNOTES.md)) |
| **x64 runtime** | `src/hfr64.c` | The New Classic runtime end to end: fixed clock, patch transaction, emitted relays, sub-stepping, dimming, D3D11 and sprite hooks |
| x64 support headers | `src/backends/fixed_clock.h`, `fixed_history.h`, `fixed_game.h`, `subtick.h`, `substep.h` | Fixed 60 Hz clock, pose history and smoothing, the game profile, sub-tick slice accounting, the dyadic sub-step schedule |
| x64 game adapter | `src/games/th06nc.c` | New Classic's addresses, frozen signatures, guard ranges and dimming rules |
| x64 UI and renderer | `src/ui/overlay_fixed.c`, `src/ui/menu_dx11.cpp`, `src/ui/overlay_dx11.cpp` | The x64 side of `ui_api.h` and the D3D11 ImGui backend |
| x64 install | `src/proxy_dxgi.c`, `src/dxgi_exports.h` | The `dxgi.dll` the game loads itself, forwarding all 57 exports; how the patch installs when Steam is the launcher (TH06NC_DEVNOTES §20) |
| x64 tests | `tools/test_fixed.c`, `tools/test_fixed_stubs.py`, `tools/test_fixed_profile.py`, `tools/test_dxgi_proxy.c` | Clock, history, schedule and patch transaction; Unicorn execution of every emitted AMD64 relay; signature and launcher checks; the dxgi proxy against the system's real one |
| Launcher | `src/launcher.c`, `src/launcher64.c`, `src/fixed_identity.h` | Detect and launch a supported executable using the common identity code; x64 games are handed to the x64 launcher |
| Tests | `tools/test_hfr.c`, `tools/test_*.h`, `tools/test_*_stubs.py` | Native shared-runtime tests and emulation of emitted x86 hooks; [docs/TESTING.md](docs/TESTING.md) says what each suite covers |
| Generated files | `tools/gen_signature_json.py`, `tools/embed_shaders.py` | `tools/th*_signatures.json` from the frozen headers (checked by both test suites); `src/core/shader_sources.h` from `shaders/*.hlsl` (by hand) |
| Build | `build.sh`/`build.ps1` (x86), `build64.sh`/`build64.ps1` (x64) | Two independent builds; `package.sh`/`package.ps1` ship both when asked |

This is deliberately a unity build: the small C modules form one translation
unit, which keeps the existing assembly entry points and their C symbols together.
The core is compiled once, regardless of the number of registered games.

The shared core uses profile fields and callbacks rather than checking the game
number. Game IDs occur in executable registration and replay metadata. The two
backends describe the engine family shared by TH10 through TH13, parameterised
by profile fields where the games differ (DEVNOTES_RUNTIME.md §7 and §7a); they
are not assumptions that every Touhou title uses this ABI or renderer.

## Detection and installation

Detection requires x86 PE32 at image base `0x400000`, an image at least as large as
the build's own (Steam and the static English builds append sections of their own, so
this is a floor rather than an equality), and every frozen
signature (83 for TH10, 66 for TH11, 69 for TH12, 101 for TH13). These cover all
overwritten code ranges and selected native function entries. This is code-layout
verification, not a whole-file checksum: resources and English text may differ.
It does not establish compatibility with arbitrary third-party runtime patches.

The DLL selects its adapter from the loaded image. On a DRM-wrapped copy there is
nothing to recognise at load time, so identification is retried from a late entry point
armed on `kernel32!QueryPerformanceCounter` — a function the game calls before it asks for
Direct3D, and one no translation patch has any reason to take. That same entry point
re-asserts the patch's import hooks if something else has overwritten them since
(`DEVNOTES_RUNTIME.md` §5d). The launcher maps a file as
inert bytes and applies the same checks before creating a process. It knows every
supported executable, x86 and x64; if more than one supported game is present it requires an
explicit target, and within one game it prefers English.
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

## Simulation behavior

All four x86 adapters share one scheduler. `substep=0` selects 60 Hz logic;
presentation pacing is independent of a replay's logic rate; device resets retain an
active replay's rate and tick sequence; stage and rate changes reset motion residuals;
duplicate presentation frames do not capture a new enemy position. Input polling reads
the actual raw input word rather than an undefined native return value.

What stays per-adapter is what the games genuinely do differently. TH11's validated
stubs retain the same 952 emitted bytes apart from relocation to the shared runtime's
variables, and it alone masks player pressed/released/bomb edges on minor ticks. TH12's
original site hooks are retained, its movement residual now using the shared resettable
storage. TH13 is TH12's engine with the structural differences named by profile fields.
TH10 has no single game-speed float at all, so its speed handling is a hand-written
`SpeedSite` table plus movement, item-homing and Cartesian-integration hooks the others
do not need (`DEVNOTES_RUNTIME.md` §7).

Enemy logic remains at 60 Hz in every game. Interpolation history is shared; child
sprite propagation and slot/parent relationships remain per-adapter callbacks. The
presentation configuration is common to all of them.

New Classic's runtime shares none of this. It keeps the simulation at 60 Hz and presents
at the display rate, sub-stepping only the player, enemy bullets and lasers, each behind
its own setting; `TH06NC_VS_TH10_13.md` is the comparison in full.

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

[ADDING_A_GAME.md](ADDING_A_GAME.md) is the procedure, field by field. The two rules
that belong here rather than there: a game whose object layouts, update-list calling
convention or renderer genuinely differ wants another backend, not a clone of the core;
and address translation alone is never enough — every subsystem has to be audited for
per-call counters, RNG, integer truncation, slow motion, pause and character mechanics
before it may run at sub-tick rate.

## Validation (as of v0.2.0-test, historical)

Automated tests pass on all four supplied executable fixtures (JP and English,
TH11 and TH12). They cover the actual full installer, required imports, each
signature's rejection path, patch transaction failure, all integer scheduler
rates 60–1000 Hz, cross-rate presentation, synthetic eight-stage replay file
round trips, metadata/settings, update-list pauses and input edges, and the
emitted gameplay stubs including native SSE/x87 float-to-integer paths.

The tests map game files as inert local data and never ship them in a release. Every
supported title has been played on this build, and New Classic extensively at 360 and
480 Hz. What is still missing is full-run replay testing: the automated checks do not
prove every character and stage combination correct, and no native-versus-patched
comparison over identical replays has been done for any game.

Output scaling, the filter pipeline, window management and the menu are described in
[RESOLUTION.md](docs/RESOLUTION.md); they live in the shared core because they are Direct3D
and Win32 work that needed no game-specific addresses. [docs/README.md](docs/README.md) is
the index of everything else, including one reverse-engineering record per game.
