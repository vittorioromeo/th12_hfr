# Developer documentation

The two documents a newcomer needs are at the repository root:
[ARCHITECTURE.md](../ARCHITECTURE.md), the source map and the replay format, and
[ADDING_A_GAME.md](../ADDING_A_GAME.md), the procedure for a new game. Everything else is here.

| Document | What it is |
| --- | --- |
| [DEVNOTES_RUNTIME.md](DEVNOTES_RUNTIME.md) | The findings: what was learned taking one runtime to four games, why each design decision went the way it did, and the bugs that shaped it. The file to read second, and the file to append to. |
| [RESOLUTION.md](RESOLUTION.md) | The picture: output scaling, the filter chain, internal resolution, texture upscaling, the window, the menu, and the presentation path. |
| [MOD_COMPATIBILITY.md](MOD_COMPATIBILITY.md) | Coexisting with THRotator, thprac and thcrap: the source audits, the confirmed conflicts, what shipped and what is still open. |
| [TESTING.md](TESTING.md) | What the two test suites cover, how they differ, and what still has to be checked by hand. |

## Per-game records

One per game, written as the port was made: the engine anatomy, the addresses, what each
patch site is and why, and the bugs met on the way.

| Game | Record |
| --- | --- |
| Touhou 10 — Mountain of Faith | [games/TH10_DEVNOTES.md](games/TH10_DEVNOTES.md) |
| Touhou 11 — Subterranean Animism | [games/TH11_DEVNOTES.md](games/TH11_DEVNOTES.md) |
| Touhou 12 — Undefined Fantastic Object | [games/TH12_DEVNOTES.md](games/TH12_DEVNOTES.md) |
| Touhou 13 — Ten Desires | [games/TH13_DEVNOTES.md](games/TH13_DEVNOTES.md) |
| Touhou 14 — Double Dealing Character | [games/TH14_DEVNOTES.md](games/TH14_DEVNOTES.md) |
| Touhou Koumakyou: New Classic | [games/TH06NC_DEVNOTES.md](games/TH06NC_DEVNOTES.md) |

TH12's record is also the oldest document in the tree: the patch began as `th12_hfr`, so it
carries the original design rationale alongside its address map. TH11 and TH12 were the first
two games in the shared runtime; TH10 came next and needed its own speed model, because it
predates the single game-speed float the later engines hang their sub-stepping off; TH13 is
TH12's engine with a handful of structural changes, each named by a profile field; TH14 is
TH13's engine rebuilt with a newer compiler, which changed four calling conventions and is the
first game to need backend variants rather than only profile fields.

New Classic is a different engine entirely — 64-bit, Direct3D 11, its own backend and a smaller
feature set. [games/TH06NC_VS_TH10_13.md](games/TH06NC_VS_TH10_13.md) is the side-by-side.
