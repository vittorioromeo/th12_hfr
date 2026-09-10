# Touhou HFR

Developer notes for the current shared runtime — what was learned building the video path
and taking the patch to three games — are in [DEVNOTES_RUNTIME.md](DEVNOTES_RUNTIME.md).

High refresh rate gameplay and presentation for Touhou. One DLL detects the game
and selects its adapter; the scheduler, input, replay and Direct3D code are shared.

**Current build: v0.4.1-test.** Supported executable layouts:

| Game | Version | Executables | State |
| --- | --- | --- | --- |
| Touhou 10 — Mountain of Faith | v1.00a | English `th10.exe`, Japanese `th10j.exe` | supported |
| Touhou 11 — Subterranean Animism | v1.00a | `th11.exe`, static English `th11e.exe` | supported |
| Touhou 12 — Undefined Fantastic Object | v1.00b | `th12.exe`, static English `th12e.exe` | supported |
| Touhou 13 — Ten Desires | v1.00c | `th13.exe`, static English `th13e.exe` | supported |

TH10 predates the single game-speed float that TH11 and TH12 hang their sub-stepping off, so its
speed model is described on its own terms; how that was done is in
[DEVNOTES_RUNTIME.md](DEVNOTES_RUNTIME.md) §7. Everything the other games have works on it.
TH13 is TH12's engine with a few structural changes, each described by a profile field (§7a).
TH13 keeps its replays and scores in `%APPDATA%\ShanghaiAlice\th13\`, and so does this patch's
replay metadata.

## What it does

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

Download/extract `touhou_hfr_v0.4.1-test.zip` and close the game.

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
| `debug` | `0` | Include periodic state diagnostics |

`[systems]` contains per-system switches for troubleshooting. Their defaults
follow the adapter's audited classification. Turning a switch on does not make
a system classified as frame-only run at sub-tick rate.

## Replays

New recordings include the logic rate, per-tick movement/focus stream, game ID,
simulation revision and gameplay settings. Compatible playback uses the recorded
logic rate/settings while presenting at the current display rate. Your settings
are restored when playback ends. Replays without HFR rate metadata use 60 Hz logic.

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
scope and adding another game. Reverse-engineering history is preserved in
[TH12 DEVNOTES](DEVNOTES.md) and [TH11 DEVNOTES](TH11_DEVNOTES.md); the original
release instructions are in [TH12_README.md](TH12_README.md) and
[TH11_README.md](TH11_README.md).
