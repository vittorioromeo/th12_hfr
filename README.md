# Touhou HFR

High refresh rate gameplay and presentation for Touhou. One DLL detects the game
and selects its adapter; the scheduler, input, replay and Direct3D code are shared.

**Current build: v0.2.0-test.** Supported executable layouts:

| Game | Version | Executables |
| --- | --- | --- |
| Touhou 11 — Subterranean Animism | v1.00a | `th11.exe`, static English `th11e.exe` |
| Touhou 12 — Undefined Fantastic Object | v1.00b | `th12.exe`, static English `th12e.exe` |

Movement, bullets, shots and other suitable systems update at the display rate,
with sub-steps whose durations add up exactly to 60 game frames per second.
Enemy scripts and frame-sensitive decisions retain their 60 Hz timing; enemy
sprites are interpolated for smoother presentation. D3D9Ex can limit the driver's
presentation queue to reduce display latency.

The pre-refactor TH11 and TH12 builds have been tested in game. The unified build
passes automated tests against all four executable layouts; it still needs manual
gameplay and full-run replay testing before a stable release. Other Touhou games
are not supported yet. See [architecture and porting guide](ARCHITECTURE.md).

## Install

Download/extract `touhou_hfr_v0.2.0-test.zip` and close the game.

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

The same four files work for either game. No game executable or data file is edited.
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
.\test.ps1 -GameExe 'G:\Touhou\TH11 ~ Subterranean Animism\th11.exe','G:\Touhou\TH12 ~ Undefined Fantastic Object\th12.exe' -Python 'C:\Python313\python.exe'
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
