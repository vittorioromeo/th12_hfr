# Developer documentation

The two documents a newcomer needs are at the repository root:
[ARCHITECTURE.md](../ARCHITECTURE.md), the source map and the replay format, and
[ADDING_A_GAME.md](../ADDING_A_GAME.md), the procedure for a new game. Everything else is here.

| Document | What it is |
| --- | --- |
| [DEVNOTES_RUNTIME.md](DEVNOTES_RUNTIME.md) | Cross-game findings for the shared x86 runtime: design decisions with their reasons, and the traps. |
| [RESOLUTION.md](RESOLUTION.md) | The picture: output scaling, the filter chain, internal resolution, texture upscaling, the window, the menu, and the presentation path. |
| [MOD_COMPATIBILITY.md](MOD_COMPATIBILITY.md) | Coexisting with THRotator, thprac and thcrap: the source audits, the confirmed conflicts, what shipped and what is still open. |
| [GAME_DIFFERENCES.md](GAME_DIFFERENCES.md) | Every game-specific behaviour, side by side, and what could be abstracted. Kept current by `tools/check_game_differences.py`. |
| [BUILDING.md](BUILDING.md) | Toolchains and build commands. |
| [TESTING.md](TESTING.md) | What the two test suites cover, how they differ, and what still has to be checked by hand. |
| [OTHER_MODS.md](OTHER_MODS.md), [REPLAYS.md](REPLAYS.md), [UPGRADING.md](UPGRADING.md), [CREDITS.md](CREDITS.md) | User-facing detail moved out of the README. |
| [FIXED_STEP_RESEARCH.md](FIXED_STEP_RESEARCH.md) | Separating simulation from presentation: the current timing paths, measured step sizes, implementation scope, replay/input risks and a staged migration plan. |

## Per-game records

One per game, written as the port was made: the engine anatomy, the addresses, what each
patch site is and why, and the bugs met on the way.

| Game | Record |
| --- | --- |
| Touhou 8 — Imperishable Night (experimental) | [games/TH08_DEVNOTES.md](games/TH08_DEVNOTES.md) |
| Touhou 10 — Mountain of Faith | [games/TH10_DEVNOTES.md](games/TH10_DEVNOTES.md) |
| Touhou 11 — Subterranean Animism | [games/TH11_DEVNOTES.md](games/TH11_DEVNOTES.md) |
| Touhou 12 — Undefined Fantastic Object | [games/TH12_DEVNOTES.md](games/TH12_DEVNOTES.md) |
| Touhou 13 — Ten Desires | [games/TH13_DEVNOTES.md](games/TH13_DEVNOTES.md) |
| Touhou 14 — Double Dealing Character | [games/TH14_DEVNOTES.md](games/TH14_DEVNOTES.md) |
| Touhou Koumakyou: New Classic | [games/TH06NC_DEVNOTES.md](games/TH06NC_DEVNOTES.md) |

TH12's record also carries the original design rationale: the patch began as `th12_hfr`. TH13 is
written as a delta against TH12, and TH14 against TH13.

New Classic is a different engine entirely — 64-bit, Direct3D 11, its own backend and a smaller
feature set. [games/TH06NC_VS_TH10_13.md](games/TH06NC_VS_TH10_13.md) is the side-by-side.
