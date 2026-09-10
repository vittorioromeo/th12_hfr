> Historical notes for the original per-game build. The current unified runtime and source locations are documented in [ARCHITECTURE.md](ARCHITECTURE.md); installation instructions are in [README.md](README.md).

# th11_hfr — high refresh rate patch for Touhou 11

**v0.1.0-test**, for Subterranean Animism **v1.00a**. This is a separate TH11
port of [th12_hfr](https://github.com/vittorioromeo/th12_hfr). The TH12 implementation
is unchanged.

Initial validation: Japanese and English executable signatures match, the engine
has run at approximately 360 presents/ticks per second, and the owner briefly
tested stage 1 with Reimu and reported that it looked correct. Full runs, all six
partners, and replay round trips still need gameplay testing.

## Install

Close the game, then copy these files beside `th11.exe`:

- `dinput8.dll` — loads the patch automatically when starting `th11.exe` or
  `th11e.exe`.
- `th11_hfr.ini` — configuration. Defaults detect the display rate, enable vsync,
  sub-tick movement/focus input, and Direct3D9Ex with a one-frame present queue.

If another mod already supplies `dinput8.dll`, keep it and use the optional
`th11_hfr.exe` plus `th11_hfr.dll` launcher instead. The launcher prefers
`th11e.exe`, then `th11.exe`; `[launcher] exe=` selects another executable name
in the same directory. The proxy and launcher can coexist: the process is patched
once.

Start the game normally. `th11_hfr.log` reports compatibility checks, graphics
initialization, and measured rates. No game executable, archive, score, or replay
is changed by installation. The mod writes its additional replay metadata only
when you save a new replay from a patched session.

**Remove:** delete the installed `dinput8.dll` to disable automatic loading, and
stop using the optional HFR launcher. The other HFR files can also be deleted.
Restore a previous proxy if you replaced one yourself.

## Behavior

The player, shots, bullets, lasers, items, stage graphics, and animation system
update in smaller time steps. Enemy scripts, menus, HUD, bombs, and game flow
retain their frame-based cadence. Enemy sprites use interpolation. Movement and
focus are sampled between game frames; bomb and edge-triggered actions remain
on frame boundaries.

TH11 requires its own hooks for Reimu's speed bonus, item collection warmup,
Yukari's warp state, Marisa B's formation switching, shot homing/gravity,
line/beam laser graze, and player death drops. Option movement and its orbit/easing
callbacks currently remain at 60 Hz. See [TH11_DEVNOTES.md](TH11_DEVNOTES.md).

This changes collision sampling and floating-point integration. HFR gameplay is
not bit-identical to stock 60 Hz gameplay, and this build does not claim exhaustive
pattern/RNG parity across every stage and character.

## Configuration

The supplied INI documents every setting. The main ones are:

| Setting under `[hfr]` | Default | Meaning |
| --- | --- | --- |
| `fps` | `0` | Automatic display rate; explicit values are clamped to 60–1000 |
| `vsync` | `1` | Vsync; `0` uses software presentation pacing |
| `substep` | `1` | `0` uses 60 Hz simulation with high-rate presentation |
| `subtick_input` | `1` | Sample movement and focus on every simulation tick |
| `enemy_interp` | `1` | Interpolate enemy sprite positions |
| `d3d9ex` | `1` | Use Direct3D9Ex; try `0` for graphics-wrapper problems |
| `max_frame_latency` | `1` | Maximum queued frames under D3D9Ex |
| `fullscreen_refresh` | `0` | Automatic exclusive-fullscreen refresh request |
| `flipex` | `0` | Experimental windowed flip presentation |
| `log` / `debug` | `1` / `0` | Log rates; optional stage/player state diagnostics |

Use the game's full drawing mode rather than its 1/2 or 1/3 frame-skip modes.
The per-system switches are troubleshooting controls; use their defaults for
recordings and playback.

## Replays

New HFR recordings append `USER` chunks containing the simulation rate and a
run-length encoded stream of per-tick movement/focus inputs. The port reads TH11's
`t11r` replay format and its TH11-specific ReplayManager fields.

Playback selects the recorded simulation rate, independently of presentation.
Stock replays without an HFR rate chunk use 60 Hz simulation. The sub-step sequence
and movement remainders reset at the start of each stage. Device resets preserve
the active replay's rate and sequence.

**Replay support is implemented but has not yet passed an end-to-end recording/
playback test.** Keep the same build and default system settings for testing.
Patched replays can desync in the stock game or in a different HFR build.

## Compatibility and reporting

The loader checks 63 frozen instruction signatures before applying hooks. An
unsupported/modified layout is left unpatched and reported in the log. The
supplied Japanese v1.00a and English static patch v1.0 both pass these checks.
The initial runtime test included the existing local `d3d9.dll` wrapper.
TH11 combinations with thcrap, vpatch, other wrappers, fullscreen, and gamepads
have not been tested in this port.

For an issue, include `th11_hfr.log`, the INI, character/partner, stage, and what
happened. Useful next checks: all six partners, movement/focus transitions,
bombs/deathbombs, dying and collecting dropped power, pause/resume, stage changes,
lasers, and saving/replaying a run. Also compare 144 Hz with 360 Hz.

## Build

Use 32-bit MinGW-w64 GCC on Windows:

```powershell
.\build.ps1 -Game th11
# Optional compiler override:
.\build.ps1 -Game th11 -Compiler C:\path\to\i686-w64-mingw32-gcc.exe
```

Files ready to copy are in `build/th11/`. `-Game all` builds both games.
There is no extra runtime dependency on Python, Ghidra, or MinGW.

Development checks need Python and `unicorn` (`python -m pip install unicorn`):

```powershell
python tools/verify_th11.py "G:\Touhou\TH11 ~ Subterranean Animism\th11.exe"
.\test_th11.ps1 -GameExe "G:\Touhou\TH11 ~ Subterranean Animism\th11.exe"
.\package_th11.ps1
```

The native harness maps a user-supplied executable as inert data. It never starts
the game. Game binaries and generated memory snapshots are not packaged.
