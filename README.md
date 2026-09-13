# Touhou HFR

Developer notes for the current shared runtime — what was learned building the video path
and taking the patch to four games — are in [DEVNOTES_RUNTIME.md](DEVNOTES_RUNTIME.md); the
per-game records are [TH10_DEVNOTES.md](TH10_DEVNOTES.md), [TH11_DEVNOTES.md](TH11_DEVNOTES.md),
[DEVNOTES.md](DEVNOTES.md) (TH12) and [TH13_DEVNOTES.md](TH13_DEVNOTES.md).

**Touhou Koumakyou: New Classic** now has an experimental x64/D3D11 backend:
60 Hz gameplay with high-rate sprite-position interpolation and the shared F11 menu.
The owner has tested gameplay and the menu at high refresh rates. An optional setting,
off by default and **untested in the running game**, additionally moves the player at the
display rate, so input is sampled every drawn frame rather than every 60 Hz frame; the rest
of the world — bullets, enemies, scripts, collisions — still runs at 60 Hz, and that is a
deliberate limit rather than a temporary one. A second optional setting, also off by default and also untested in
the running game, sub-steps enemy bullets and lasers: they advance a fraction of a frame at a
time and their culling, grazing and collision run at every step, so a projectile that would
have jumped past you between two 60 Hz frames can now hit you — it makes the game harder,
not just smoother. Both ways the player can die are now evaluated at the display rate.
Enemies, the player's shots, items and every script still run at 60 Hz, which is deliberate:
their discrete effects (enemy damage above all) are applied once per 60 Hz frame, so
sub-stepping their motion could not change an outcome — see
[§18](TH06NC_DEVNOTES.md#18-lasers-and-where-the-parity-with-th10-13-actually-is-2026-09-13). New Classic does not have
TH10–13's video enhancements. See
[what New Classic has and TH10–13 do not, and the other way round](TH06NC_VS_TH10_13.md),
[the prototype instructions and complete research record](TH06NC_DEVNOTES.md#13-experimental-prototype-2026-09-13)
and [the sub-tick player movement notes](TH06NC_DEVNOTES.md#14-sub-tick-player-movement-2026-09-13).

High refresh rate gameplay and presentation for Touhou. One DLL detects the game
and selects its adapter; TH10–13 share their scheduler, input, replay and Direct3D code.
The same launcher dispatches New Classic to its separate x64 runtime. Menu contents,
menu-key handling, patch transactions and the x64 executable registry are shared.

**Current build: v0.4.17-test.** Supported executable layouts:

| Game | Version | Executables | State |
| --- | --- | --- | --- |
| Touhou 10 — Mountain of Faith | v1.00a | `th10.exe`, `th10e.exe` | supported |
| Touhou 11 — Subterranean Animism | v1.00a | `th11.exe`, `th11e.exe` | supported |
| Touhou 12 — Undefined Fantastic Object | v1.00b | `th12.exe`, `th12e.exe` | supported |
| Touhou 13 — Ten Desires | v1.00c | `th13.exe`, `th13e.exe` | supported |
| Touhou Koumakyou: New Classic | verified SHA-256 in [notes](TH06NC_DEVNOTES.md#2-product-and-inspected-build) | `th06nc.exe` | experimental: fixed 60 Hz + sprite interpolation, optional sub-tick player movement and sub-stepped projectiles |

The current source fixes TH10's recurring HFR hitches accompanied by a brief
`0.0fps` reading: its native FPS watchdog mistook rates above 65 FPS for a broken
clock and reset the timer. See [TH10 development notes, §6b](TH10_DEVNOTES.md#6b-hfr-hitches-and-the-fps-counter-briefly-reading-zero-2026-09-12).

TH10 predates the single game-speed float that TH11 and TH12 hang their sub-stepping off, so its
speed model is described on its own terms; how that was done is in
[DEVNOTES_RUNTIME.md](DEVNOTES_RUNTIME.md) §7. Everything the other games have works on it.
TH13 is TH12's engine with a few structural changes, each described by a profile field (§7a).
TH13 keeps its replays and scores in `%APPDATA%\ShanghaiAlice\th13\`, and so does this patch's
replay metadata.

## What it does

The features below describe TH10–13. New Classic currently provides the smaller feature
set listed above; its F11 menu identifies the unavailable controls.

**High frame rate.** Movement, bullets, shots and other suitable systems update at the display
rate, with sub-steps whose durations add up exactly to 60 game frames per second. Enemy scripts
and frame-sensitive decisions retain their 60 Hz timing; enemy sprites are interpolated for
smoother presentation. Direct3D 9Ex can limit the driver's presentation queue to reduce display
latency. The log reports how many presented frames had no simulation tick behind them, so the
frame rate can be checked rather than taken on trust.

**Resolution and scaling.** The window can be any size — dragged, set at startup with
`window_scale`, or stepped with F10 through 1x, 1.5x, 2x and borderless fullscreen the way
TH11 and later do natively (TH10's own dialog only offers 640x480) — with the picture fitted and letterboxed,
stretched, or held to whole-number multiples for pixel-perfect output. The game's fullscreen can
become a borderless window covering the monitor. Four upscaling filters are bundled — MMPX,
xBR-lv2, Super-xBR and ScaleFX — and any `.hlsl` file dropped into `shaders/` next to the game
is offered alongside them, including multi-pass ones. `shaders/README.md` describes the format.
After the filter, an optional sharpening pass — AMD's CAS or a clamped luma unsharp mask, with
a strength slider — runs over the finished picture at the window's resolution, where it undoes
the softness a non-integer resample leaves without touching the game's own pixels. In borderless
fullscreen the mouse pointer, which the game hides, is shown while the mouse moves (or always,
or never — `cursor=`), and always while the menu is open.

**Internal resolution.** `internal_scale=2` (or 3) makes the game draw at 1280x960 (1920x1440)
instead of 640x480 while keeping every coordinate it has: sprites land on real sub-pixel
positions instead of snapping to 640x480 pixels — a slow bullet at 360 Hz glides instead of
stepping — and the 3D backgrounds gain real detail. Supported fully on TH13; other games get
the sharper rasterisation until their sprite snapping is mapped. `texture_scale=2` with
`texture_filter=xbr-lv2` (or `mmpx`, `super-xbr`, `scalefx`) then magnifies every texture the
game loads with that filter, once, at load time — the pixel-art upscaling applied to the art
itself rather than to the finished frame, alpha edges included.

**Readability.** Five sliders in the menu's Display tab (and `dim_*` keys) fade what competes
with the enemy's bullets: the stage background towards black; the P and point pickups, the
cosmetic effects (explosions, hit sparks, bullet cancels, particles), your own shots, and a
game's own extra class (TH13's divine spirits) towards transparent. Enemies, bullets, lasers,
the player and the interface are never touched. The background is dimmed once per frame at
the point where the game finishes drawing it, so fog, additive layers and TH11-13's offscreen
compositing fade together; everything else is classified per sprite by which ANM file it came
from and which draw callback drew it, and faded in its own draw call. Supported on all four
games; a new game needs two addresses and a short rule table (DEVNOTES_RUNTIME §3b).

**An in-game menu.** F11 by default. Every setting in the INI is there, in four tabs, and changes
apply immediately; saving makes them the default. Settings that cannot take effect in the current
configuration are disabled with the reason shown rather than silently ignored.

**Coexistence.** The patch refuses to install alongside another patch that has taken the game's
frame loop (vpatch and the like), and warns when a Direct3D 9 wrapper such as PivotDX9 is
presenting the game, because that takes the scaling modes and borderless fullscreen away.

The pre-refactor TH11 and TH12 builds have been tested in game. The unified build passes
automated tests against every supported executable layout and has been exercised in game on
every supported title; it still needs full-run replay testing before a stable release. See
[architecture and porting guide](ARCHITECTURE.md) and [ADDING_A_GAME.md](ADDING_A_GAME.md).

## Install

For **New Classic**, build both architectures (below), then copy `touhou_hfr.exe`,
`touhou_hfr64.exe`, `touhou_hfr64.dll` and `touhou_hfr.ini` beside `th06nc.exe` in the
`th06nc` subfolder. Start `touhou_hfr.exe`. Use F11 → Timing to select the presentation
rate, toggle interpolation, or enable sub-tick player movement and sub-stepped bullets
(both off by default; they make recorded replays diverge, and should be turned off before
watching a replay — the flag that used to disable them automatically turned out not to mean
what it claimed, see [§17](TH06NC_DEVNOTES.md#17-why-neither-feature-had-ever-run-and-what-the-fps-readout-counts-2026-09-13)). Use the game's own display settings. Do not install the
32-bit `dinput8.dll` proxy in this x64 game. The generic `install.ps1` workflow below is
for TH10–13. Bundled `th06c.exe` (Classic) is different and is not supported.

Download/extract `touhou_hfr_v0.4.17-test.zip` and close the game.

For a fresh installation, copy these four files next to the game executable:

- `dinput8.dll`
- `touhou_hfr.dll`
- `touhou_hfr.exe`
- `touhou_hfr.ini`

Start the game normally to use the `dinput8.dll` proxy, or run `touhou_hfr.exe`.
The launcher detects supported game code and prefers the English executable when
both languages of one game are present. Use `[launcher] exe=th12.exe` (for example)
to select another executable. If multiple supported games are present, set an
explicit target. An executable name can also be passed as the launcher's argument.

The same four files work for every supported game. No game executable or data file is edited.
The DLL forwards DirectInput calls to Windows' original library. Unknown or changed
hook sites cause installation to be declined, leaving proxy forwarding available.

### Upgrade from th11_hfr / th12_hfr

Use the included installer from PowerShell:

```powershell
.\install.ps1 -GameDirectory 'G:\Touhou\TH11 ~ Subterranean Animism'
```

It verifies the target, creates `hfr-backups/<timestamp>/`, installs the common
files and updates existing legacy DLL/launcher aliases. This lets existing
shortcuts keep working and avoids loading an old patch alongside the new proxy.
An existing `touhou_hfr.ini` is retained; otherwise the game's legacy INI is copied.
Without a common INI, the runtime also understands `th11_hfr.ini` / `th12_hfr.ini`.

If upgrading manually, back up the old patch files and replace any existing
`th11_hfr.dll` / `th12_hfr.dll` with a copy of the new `touhou_hfr.dll` too. Replace
old launcher EXEs with the new launcher or use `touhou_hfr.exe` directly. Preserve
your settings in the common INI. An existing unrelated `dinput8.dll` proxy needs
separate compatibility handling; do not assume two proxies can be combined.

To undo a scripted upgrade, close the game, restore the files in the timestamped
backup, and remove newly created files listed with `existed: false` in its
`manifest.json`. To remove a fresh installation, delete the four patch files above.
Older releases remain available separately for comparison and old replays.

## Configuration

Edit `touhou_hfr.ini` beside the game and restart. Defaults detect the display
refresh and enable sub-stepping, sub-tick movement/focus input and D3D9Ex.

| `[hfr]` setting | Default | Meaning |
| --- | --- | --- |
| `fps` | `0` | Automatic display rate; explicit supported logic rates are 60–1000 |
| `vsync` | `1` | Vsync pacing; `0` uses the software presentation limiter |
| `substep` | `1` | Smooth gameplay updates; `0` keeps logic at 60 Hz with HFR presentation |
| `subtick_input` | `1` | Sample movement/focus between stock frame inputs |
| `enemy_interp` | `1` | Interpolate enemy sprites between 60 Hz positions |
| `d3d9ex` | `1` | Use D3D9Ex when available |
| `max_frame_latency` | `1` | D3D9Ex queued-frame limit; `0` leaves the driver default |
| `fullscreen_refresh` | `0` | Exclusive fullscreen rate; `0` follows automatic/explicit fps |
| `flipex` | `0` | Experimental windowed flip presentation |
| `log` | `1` | Write `touhou_hfr.log` beside the game |
| `debug` | `0` | Include periodic state diagnostics; `2` and `3` add denser draw tables (DEVNOTES_RUNTIME §3b) |

| `[video]` setting | Default | Meaning |
| --- | --- | --- |
| `scaling` | `1` | `0` stretch to fill, `1` fit with letterboxing, `2` whole-number scale only |
| `filter` | `sharp-bilinear` | Upscaling filter by name: `nearest`, `bilinear`, `sharp-bilinear`, `mmpx`, `xbr-lv2`, `super-xbr`, `scalefx`, or any `.hlsl` in `shaders/` |
| `resizable` | `1` | Resize border on the window |
| `window_scale` | `0` | Startup window size as a percentage of 640x480 (`200` = 1280x960); `-1` = largest whole multiple that fits the screen; `0` = as the game made it. TH10's own dialog only offers 640x480, so set this there |
| `snap_aspect` | `0` | Hold the window at 4:3 while dragging |
| `fullscreen_mode` | `1` | `1` the game's fullscreen becomes a borderless window covering the monitor; `0` leave it |
| `menu_key` | `122` | Virtual-key code of the in-game menu (F11); `0` disables the menu |
| `size_cycle_key` | `121` | Key that steps 640x480 → 960x720 → 1280x960 → borderless fullscreen on games without their own (TH10); `0` = off |
| `warn_wrapper` | `1` | Say at startup when a d3d9 wrapper is presenting the game |
| `own_present` | `-1` | Which swap chain reaches the screen; `-1` decides automatically |
| `internal_scale` | `1` | Draw the game at N times 640x480 (sub-pixel sprite positions; restart to apply) |
| `texture_scale` | `0` | Magnify every loaded texture N times with `texture_filter` at load time (pair with `internal_scale`) |
| `texture_filter` | `xbr-lv2` | The filter for that: `xbr-lv2`, `mmpx`, `super-xbr`, `scalefx`, or a `.hlsl` from `shaders/` |
| `dim_background` | `0` | Fade the stage background towards black by this percentage, in-stage (menu slider too) |
| `dim_items` | `0` | Fade the P/point pickups towards transparent by this percentage |
| `dim_effects` | `0` | Fade explosions, hit sparks, bullet cancels and particles |
| `dim_special` | `0` | Fade the game's own extra class: TH13's divine spirits (nothing in TH10-TH12) |
| `dim_player_shots` | `0` | Fade your own shots and options |

`[systems]` contains per-system switches for troubleshooting. Their defaults
follow the adapter's audited classification. Turning a switch on does not make
a system classified as frame-only run at sub-tick rate.

## Replays

New recordings include the logic rate, per-tick movement/focus stream, game ID,
simulation revision and gameplay settings. Compatible playback uses the recorded
logic rate/settings while presenting at the current display rate. Your settings
are restored when playback ends. Replays without HFR rate metadata use 60 Hz logic.

TH13 keeps its replays in `%APPDATA%\ShanghaiAlice\th13\replay\` (the game's own
location); the HFR metadata is appended there. The other games keep them beside the executable.

Legacy HFR replays retain their rate/input chunks, but their exact simulation
version is unknown. Unsupported new metadata produces a compatibility warning;
playback may desynchronize. Keep the build that recorded a replay when exact
playback matters. The native compressed payload is unchanged, but an HFR recording
is not guaranteed to play accurately with the unmodified game or another build.

## Build and test

Windows requires a **32-bit MinGW-w64 GCC** compiler. The default script path is
`C:\msys64\mingw32\bin\gcc.exe`; pass `-Compiler` to override it.

```powershell
.\build.ps1
.\test.ps1 -GameExe 'G:\Touhou\TH11 ~ Subterranean Animism\th11.exe','G:\Touhou\TH12 ~ Undefined Fantastic Object\th12.exe','G:\Touhou\TH13 ~ Ten Desires\th13.exe' -Python 'C:\Python313\python.exe'
.\package.ps1
```

For New Classic, also install **64-bit MinGW-w64 GCC/G++** (default
`C:\msys64\mingw64\bin\gcc.exe`), then run:

```powershell
.\build64.ps1
.\test64.ps1 -GameExe 'C:\Program Files (x86)\Steam\steamapps\common\th06nc\th06nc.exe'
.\package.ps1 -Version '0.4.17-th06nc-prototype' -IncludeExperimental64
```

Run `build.ps1` first: `test64.ps1` checks both launcher architectures. The x64 tests
also require Python `pefile`; they check clock scheduling, history invalidation, the
actual patch transaction and emitted AMD64 relays, frozen signatures, and read-only
launcher rejection. `build64.ps1` produces `touhou_hfr64.dll` and its launcher helper.
The optional package includes vendored ImGui and MinHook sources/licenses. There is
currently no x64 shell build script; the existing shell build remains x86.

The Python tests need `unicorn` (`python -m pip install unicorn`). The native
harness runs on Windows and uses your local executables as inert fixtures. It
tests the shared scheduler, installer, replay codec, input/pauses and the actual
emitted x86 stubs. Test artifacts remain under ignored `build/tests/` and are
excluded from release archives. Include both Japanese and English files when
validating a release.

`build.sh` / `package.sh` support a shell with `i686-w64-mingw32-gcc` and `zip`.
The build outputs one `touhou_hfr.dll`, one launcher and a byte-identical proxy
alias. The archive includes the complete source and no game files.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the source map, replay format, validation
scope and adding another game. Per-game reverse-engineering records: [TH10](TH10_DEVNOTES.md),
[TH11](TH11_DEVNOTES.md), [TH12](DEVNOTES.md) and [TH13](TH13_DEVNOTES.md); the original
release instructions are in [TH12_README.md](TH12_README.md) and
[TH11_README.md](TH11_README.md).

## Credits, tools and resources

The mod was developed by Vittorio Romeo with AI assistance. The record of what model found
what is in the git history and the per-game devnotes.

**AI models.** The original TH12 patch (v0.10-v0.11), the restructuring into a shared
multi-game runtime with the TH11 adapter and the native regression harness, and the first
TH10 profile were authored with OpenAI's ChatGPT ("ChatGPT 6.0 Astra" in the import commit;
the TH10 work on a Codex branch). ChatGPT Astra also found the cause of TH10's HFR hitches —
the game's FPS-counter watchdog resetting its clock — after a long hunt elsewhere had failed ([TH10_DEVNOTES §6b](TH10_DEVNOTES.md)). Everything from
the scaler onward — output scaling and the filter chain, the Dear ImGui menu, Direct3D 9Ex
presentation through an additional swap chain, the TH10 validation and the TH13 port, internal
resolution and texture upscaling, dimming, sharpening, the joystick thread, the SSE clock fix
and most of the devnotes — was developed with Anthropic's Claude (Opus 4.8, Opus 5 and Fable
5.1, through Claude Code and Cowork), which also extended the harness and built and drove the
Wine test rig. Each commit names its co-author.

**Reverse engineering.** [Ghidra](https://github.com/NationalSecurityAgency/ghidra) 11.3.2,
headless, with the two scripts in `tools/` (`FixFuncs.java` recovers the functions ZUN's MSVC
builds hide behind vtables and `int3` padding; `ExportAll.java` decompiles everything into one
greppable file). `objdump -d -M intel` for exact instruction bytes; Python with
[pefile](https://github.com/erocarrera/pefile) and [Capstone](https://www.capstone-engine.org/)
for the pattern scans in `tools/`, and [Unicorn](https://www.unicorn-engine.org/) to emulate
the emitted stubs and patched sites in the Python tests. Game data was unpacked and the ECL and
ANM scripts read with [thtk](https://github.com/thpatch/thtk) (`thdat`, `thecl`, `thanm`) and
[truth](https://github.com/ExpHP/truth), whose instruction tables name what each script does.

**Existing projects consulted.** [thprac](https://github.com/touhouworldcup/thprac) for its
large, well-tested sets of TH11 and TH12 addresses and struct offsets, used as an independent
reference; [OpenInputLagPatch](https://github.com/khang06/OpenInputLagPatch) by khang06 for the
Direct3D 9Ex approach (managed-pool conversion, `CreateDeviceEx`, `SetMaximumFrameLatency`)
and its main-loop hook site; vpatch and thcrap for how a `dinput8.dll` proxy is expected to
coexist with the rest of the ecosystem; PivotDX9 as the wrapper the presentation-path detection
was written against. The update-runner protocol, the game-speed model, the draw order and
everything else in the devnotes was reverse-engineered from the binaries.

**Third-party code in the build.**

| Component | Author | Licence | Used for |
| --- | --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.91.8 | Omar Cornut | MIT | the F11 menu (`third_party/imgui`) |
| MMPX | Morgan McGuire and Mara Gagiu; slang port by hunterk | MIT | `shaders/mmpx.hlsl` |
| xBR-lv2, Super-xBR | Hyllian | MIT | `shaders/xbr-lv2.hlsl`, `shaders/super-xbr.hlsl` |
| ScaleFX | Sp00kyFox | MIT | `shaders/scalefx.hlsl` |
| FidelityFX CAS | Advanced Micro Devices | MIT | `shaders/cas.hlsl` |

The filters were ported to Direct3D 9 HLSL from the
[libretro/slang-shaders](https://github.com/libretro/slang-shaders) and
[libretro/common-shaders](https://github.com/libretro/common-shaders) versions; the licence
texts travel at the top of each file, and `shaders/README.md` records what the ports changed
and which well-known filters (xBRZ, hqx, NNEDI3, FSRCNNX) were left out and why.

**Build and test environment.** MinGW-w64 GCC (32-bit) on Windows and Linux; PowerShell and
POSIX shell scripts; the native harness in `tools/test_hfr.c` runs against the real executables
as inert fixtures. Development and every automated run happened under
[Wine](https://www.winehq.org/) 9 on Linux with Xvfb, `xdotool` driving the games and
ImageMagick reading the screen; Direct3D behaviour was confirmed on Windows in play. Microsoft's
Direct3D 9 and Win32 documentation was the reference for everything the patch asks of the
system.

Touhou Project is © Team Shanghai Alice / ZUN. This mod contains no game files and patches the
games only in memory.
