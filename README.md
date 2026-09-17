# Touhou HFR

**Touhou, running at your monitor's refresh rate.** These games were built around 60 frames a
second. On a 144, 240 or 360 Hz display this patch draws — and moves the player, the bullets and
your shots — that many times a second instead, while keeping the game's own logic on its exact
60 Hz schedule.

It also gives you a window you can resize, decent upscaling filters instead of whatever your
driver picks, optional dimming so bullets stand out against the background, and a settings menu
on **F11** that applies changes as you make them.

Nothing is installed and no game file is modified: you add four files to the game's folder, and
you remove the patch by deleting them again.

**[Download the latest release](https://github.com/vittorioromeo/th12_hfr/releases)** — [what changed](CHANGELOG.md)

| [![Touhou HFR — v0.4 showcase](https://img.youtube.com/vi/EmfJLZxlHZI/hqdefault.jpg)](https://www.youtube.com/watch?v=EmfJLZxlHZI) | [![Touhou HFR — High Framerate and Refresh Rate Patch](https://img.youtube.com/vi/H7FcMXPgPFQ/hqdefault.jpg)](https://www.youtube.com/watch?v=H7FcMXPgPFQ) |
| :---: | :---: |
| **[v0.4 showcase](https://www.youtube.com/watch?v=EmfJLZxlHZI)** | **[High Framerate and Refresh Rate Patch](https://www.youtube.com/watch?v=H7FcMXPgPFQ)** |

**Contents** — [Does it work with my game?](#does-it-work-with-my-game) ·
[Install](#install) · [Using it](#using-it) · [Settings](#settings) ·
[Other mods](#other-mods) · [Limitations](#limitations) ·
[New Classic](#new-classic) · [Troubleshooting](#troubleshooting) ·
[For developers](#for-developers) · [Credits](#credits-tools-and-resources)

## Does it work with my game?

| Game | Version | Executables | State |
| --- | --- | --- | --- |
| Touhou 10 — Mountain of Faith | v1.00a | `th10.exe`, `th10e.exe` | supported |
| Touhou 11 — Subterranean Animism | v1.00a | `th11.exe`, `th11e.exe` | supported |
| Touhou 12 — Undefined Fantastic Object | v1.00b | `th12.exe`, `th12e.exe` | supported |
| Touhou 13 — Ten Desires | v1.00c | `th13.exe`, `th13e.exe` | supported |
| Touhou 14 — Double Dealing Character | v1.00b | `th14.exe` | **partial** — picture only, see below |
| Touhou Koumakyou: New Classic | see [notes](docs/games/TH06NC_DEVNOTES.md#2-product-and-inspected-build) | `th06nc.exe` | **experimental**, smaller feature set — [details](#new-classic) |

TH14 is new and only half described. What works is the picture: resizing, scaling, filters,
borderless, dimming and the F11 menu. What does not work yet is the thing the patch is for —
**motion still looks exactly like the unmodified game.** The game presents at your display's
rate, but nothing moves between its 60 Hz ticks, so at 360 Hz you are shown the same frame six
times. Smoothness needs sub-stepping, and that needs work on TH14 that is not finished. There
is also no replay extension and no sub-tick input. Only the Japanese `th14.exe` is recognised
so far; the English and Steam builds still have to be checked.

Japanese and English executables are both supported for TH10–13, and so are the Steam releases. Other
versions, other games, and the `th06c.exe` (Classic) executable bundled with New Classic are
not: the patch verifies the executable's code before touching anything and declines to install
if it does not recognise it.

You need Windows and a display that runs above 60 Hz. On a 60 Hz display the patch installs and
behaves, but there is nothing for it to do — the extra frames are the point.

**On Steam, start the game from Steam.** A Steam copy of TH10–13 is the same game inside a DRM
wrapper that keeps its code encrypted until the game itself starts, so `touhou_hfr.exe` cannot
check it or launch it and will tell you so. `dinput8.dll` does not care: put the four files in
the folder, press Play, and the patch installs itself once the game is running.

**Other mods** — THRotator, thprac and thcrap all work alongside this patch;
[see below](#other-mods) for the one setting each combination needs.

## Install

Close the game. Download and extract the release archive, then copy these four files next to
the game's executable:

```
dinput8.dll
touhou_hfr.dll
touhou_hfr.exe
touhou_hfr.ini
```

That is the whole installation, and `dinput8.dll` loads the patch whenever the game starts. You
can also run `touhou_hfr.exe`, which finds the game beside it and starts it — except on Steam,
where you press **Play** instead, for the reason above.

The same four files work for every supported game, so you can copy the same folder contents
into each. If you keep several games in one folder, set `[launcher] exe=th12.exe` in the INI (or
pass the executable name to `touhou_hfr.exe`) to say which one you mean.

The bundled filters are compiled into the DLL. The `shaders/` folder in the archive is only
needed if you want to drop in extra `.hlsl` filters of your own — copy it next to the game and
the patch will offer whatever is in it.

**To uninstall**, delete those four files.

### New Classic

New Classic is 64-bit, so it needs different files. Put these in the game's `th06nc` folder —
the one containing `th06nc.exe` — and start the game from Steam as usual:

```
dxgi.dll
touhou_hfr64.dll
touhou_hfr.ini
```

**Do not put the 32-bit `dinput8.dll` in this game**; it cannot load. To uninstall, delete those
three files. One conflict to know about: ReShade also installs as `dxgi.dll`, and only one of
them can have that name in the folder.

## Using it

**F11** opens the settings menu: four tabs, changes applied as you move the slider, and **Save**
writes them to `touhou_hfr.ini` so they become the default. Anything that cannot take effect in
your current setup is shown disabled with the reason rather than silently ignored. A few
settings — internal resolution, texture upscaling and the key bindings — are INI-only and need a
restart; the [settings tables](#settings) say which.

**F10** steps the window through 640x480, 960x720, 1280x960 and borderless fullscreen. TH11, TH12
and TH13 have their own F10 for this and keep it; the patch only provides one for TH10, whose own
dialog is stuck at 640x480. Both keys can be rebound or switched off in the INI.

**Is it working?** Open F11 → Timing. It shows the rate it is presenting at and the rate it is
simulating at, both measured. The first should be your refresh rate (or whatever you set `fps`
to) and the second should sit at 60. The game's own on-screen FPS readout counts presented
frames, so it should agree with the first number.

The patch writes `touhou_hfr.log` next to the game: which game it recognised, every setting it
read, what it patched, every non-Windows module in the process, and the measured rates once a
second. If you report a problem, that file is what to send.

## Settings

To edit by hand, open `touhou_hfr.ini` beside the game and restart. The file itself carries a
comment for every setting; the tables below are the summary. The defaults detect your display's
refresh rate and turn on sub-stepping, sub-tick input and D3D9Ex.

Settings marked **INI only** do not appear in the F11 menu; those marked **restart** take effect
the next time the game starts.

### `[launcher]`

| Setting | Default | Meaning |
| --- | --- | --- |
| `exe` | empty | Which executable `touhou_hfr.exe` starts; empty detects a supported game, preferring the English build. **INI only** |

### `[hfr]` — timing

| Setting | Default | Meaning |
| --- | --- | --- |
| `fps` | `0` | Automatic display rate; explicit logic rates of 60–1000 are accepted |
| `vsync` | `1` | Vsync pacing; `0` uses the software presentation limiter |
| `substep` | `1` | Smooth gameplay updates; `0` keeps logic at 60 Hz with HFR presentation |
| `subtick_input` | `1` | Sample movement and focus between stock frame inputs |
| `enemy_interp` | `1` | Interpolate enemy sprites between 60 Hz positions |
| `d3d9ex` | `1` | Use Direct3D 9Ex when available |
| `max_frame_latency` | `1` | D3D9Ex queued-frame limit; `0` leaves the driver default. Shortening the queue takes a little delay out of the gap between your input and the screen |
| `fullscreen_refresh` | `0` | Exclusive fullscreen rate; `0` follows automatic/explicit `fps`. **INI only** |
| `flipex` | `0` | Experimental windowed flip presentation. **INI only, restart** |
| `log` | `1` | Write `touhou_hfr.log` beside the game. **INI only**; New Classic always logs |
| `debug` | `0` | Periodic state diagnostics; `2` and `3` add denser draw tables |

### `[video]` — picture, window and readability

| Setting | Default | Meaning |
| --- | --- | --- |
| `scaling` | `1` | `0` stretch to fill, `1` fit with letterboxing, `2` whole-number scale only |
| `filter` | `sharp-bilinear` | `nearest`, `bilinear`, `sharp-bilinear`, `mmpx`, `xbr-lv2`, `super-xbr`, `scalefx`, or any `.hlsl` in `shaders/` |
| `sharpen` | `none` | Post-filter sharpening over the finished picture: `none`, `cas`, `unsharp-mask`. Reach for this when an awkward window size has left the image soft |
| `sharpen_strength` | `50` | 0–100 |
| `resizable` | `1` | Resize border on the window |
| `window_scale` | `0` | Startup size as a percentage of 640x480 (`200` = 1280x960); `-1` = largest whole multiple that fits the screen; `0` = as the game made it |
| `snap_aspect` | `0` | Hold the window at 4:3 while dragging |
| `fullscreen_mode` | `1` | `1` the game's fullscreen becomes a borderless window covering the monitor; `0` leave it alone |
| `cursor` | `2` | Mouse pointer in borderless fullscreen: `0` never, `1` always, `2` while it moves. Always shown while the menu is open |
| `menu_key` | `122` | Virtual-key code of the in-game menu (F11); `0` disables it. **INI only** |
| `size_cycle_key` | `121` | Window-size key for a game with none of its own, which means TH10 (F10); `0` = off. **INI only** |
| `warn_wrapper` | `1` | Say at startup when a d3d9 wrapper is presenting the game. **INI only** |
| `external_renderer` | `-1` | Hand the picture to a rotation wrapper such as THRotator: `-1` when one is recognised, `1` always, `0` never. **INI only** |
| `own_present` | `-1` | Which swap chain reaches the screen; `-1` decides automatically |
| `internal_scale` | `1` | Draw the game at N times 640x480, 1–4, so sprites land on real sub-pixel positions and the 3D backgrounds gain detail. **INI only, restart** |
| `texture_scale` | `0` | Magnify every loaded texture N times at load time, 2–4 (`0` and `1` are off); pair with `internal_scale`. **INI only, restart** |
| `texture_filter` | `xbr-lv2` | The filter for that: `xbr-lv2`, `mmpx`, `super-xbr`, `scalefx`, or a `.hlsl` from `shaders/`. **INI only, restart** |
| `dim_background` | `0` | Fade the stage background towards black by this percentage |
| `dim_items` | `0` | Fade the P and point pickups towards transparent |
| `dim_effects` | `0` | Fade explosions, hit sparks, bullet cancels and particles |
| `dim_special` | `0` | Fade the game's own extra class — TH13's divine spirits; nothing in TH10–TH12 or New Classic |
| `dim_player_shots` | `0` | Fade your own shots and options |

Dimming never touches enemies, bullets, lasers, the player or the interface.

### `[fixed60]` — New Classic only

`[hfr] fps` sets the presentation rate for New Classic too; these control its backend.

| Setting | Default | Meaning |
| --- | --- | --- |
| `interpolate` | `1` | Smooth sprite position, rotation and scale between 60 Hz frames |
| `subtick` | `0` | Sub-tick player movement ([see below](#new-classic)) |
| `substep` | `0` | Sub-stepped enemy bullets and lasers ([see below](#new-classic)) |
| `vsync` | `0` | D3D11 vsync; separate from `[hfr] vsync` so the D3D9 default is untouched |
| `diag_seconds` | `0` | Run for N seconds, log the draw and update lists, and quit. Leave at 0 |

`[systems]` holds per-system switches for troubleshooting, defaulting to each game's audited
classification of which systems may run at sub-tick rate. Turning one on removes a system that
was allowed to sub-step; it cannot promote one the audit classified as frame-only.

## Other mods

| Mod | Works alongside? | What to change |
| --- | --- | --- |
| [THRotator](https://github.com/massanoori/THRotator) | Yes, TH10–13 | Nothing. This patch recognises it and hands over the picture |
| [thprac](https://github.com/touhouworldcup/thprac) | Yes, TH10–13 | Untick *Use VsyncPatch* and *Use OpenInputLagPatch* in its launcher |
| [thcrap](https://github.com/thpatch/thcrap) | Yes, TH10–13 | Nothing. Start the game through thcrap as usual |
| vpatch, OpenInputLagPatch | No | Remove them. This patch replaces what they did for these games |
| Direct3D 9 wrappers (PivotDX9 and the like) | Partly | Nothing, but scaling and borderless fullscreen go to the wrapper |

The patch detects the ones it knows and says so, in the log and on screen; it cannot detect
everything. If something looks wrong, try the game with only this patch installed.

### Rotation and layout wrappers (THRotator)

THRotator and this patch divide the job rather than share it: THRotator owns the picture — the
render target, the rotation, the HUD layout, the window and the presentation — and this patch
owns everything upstream of that, which is the frame rate and its pacing, sub-stepping,
interpolation, dimming, replays and input.

Install both as each normally wants; `d3d9.dll` and `dinput8.dll` do not collide, so the game
loads them both. This patch recognises THRotator when it starts and steps out of the picture by
itself, logging what each side is doing.

What this patch stops doing in that mode: scaling mode, upscaling filters, sharpening, internal
resolution, texture upscaling, window sizing and borderless fullscreen. THRotator is doing its
own version of all of those, in its own coordinate system, and two programs composing one image
is how you get a picture nobody can explain. Configure them in THRotator.

`external_renderer` under `[video]` decides this: `-1` (the default) recognises it, `1` forces the
same treatment for another renderer this patch does not know by name, and `0` keeps composing
the picture here.

**F11 works here too**, drawn onto the image THRotator presents, and re-acquired after every
rotation. Confirmed on TH12 with THRotator 2.1.0 at 360 Hz.

### Practice tools (thprac)

thprac owns the practice menu, the stage and section jumps and the replay tools; this patch owns
the frame loop underneath them. Their patch sites do not touch anywhere. Install both as each
normally wants and launch from thprac's launcher, or attach thprac to a running game.

**One thing to change in thprac's launcher: untick "Use VsyncPatch (if avaliable)" and "Use
OpenInputLagPatch (if avaliable)".** Both are on by default, and thprac will load either one it
finds sitting in the game's folder — an old `vpatch_th12.dll` you have not thought about in
years is enough. Those patches replace the game's frame limiter, which is the one job this patch
cannot share, so it refuses to install beside them and tells you which box to untick.

Two smaller things. The practice menu updates at the display rate rather than at 60 Hz, which is
what keeps it in step with the drawing — hotkeys are unaffected, but anything you hold down to
repeat repeats proportionally faster. And leave `internal_scale` at 1 while using thprac if you
can: thprac sizes its interface from the back buffer at startup, where it agrees with this
patch, but from the presentation parameters again after a device reset, where it does not once
the internal resolution is higher. The first launch looks right; the first window resize or
Alt-Tab after that can leave its menu the wrong size.

Needs v0.5.3-test or newer. Confirmed on TH12 with thprac 2.3.1.1 at 360 Hz, and on TH10, TH11
and TH13 against a stand-in reproducing thprac's hook mechanism. Running thprac and THRotator
together with this patch has not been tried.

### Translation patches (thcrap)

thcrap replaces text, fonts and images; this patch replaces the frame loop and the renderer. They
do not write over each other anywhere. Install both and start the game through thcrap's shortcut
as usual; `dinput8.dll` loads this patch on that launch like any other. Install order does not
matter, and neither modifies the other's files — this patch never changes the executable on disk,
so thcrap still identifies the game by hash exactly as it expects.

One thing to know: **thcrap's own Direct3D features do not engage while `d3d9ex=1`** (the
default). This patch creates the Direct3D object through 9Ex, which steps over whoever else is in
that chain, so thcrap's translation notes and its device-lost handling are skipped. Everything
thcrap does to text, fonts, images and files is unaffected — that is where the translation
actually lives. If you want those extras, set `d3d9ex=0`, at the cost of `max_frame_latency`.

Needs v0.5.2-test or newer; earlier builds were silently switched off by thcrap.

The one combination that does not work is an **executable that has already been translated** —
the old pre-thcrap English patches that ship a modified `th10e.exe` and friends. Those rewrite
the game's code, so this patch no longer recognises it and declines. thcrap is the supported way
to play in English; it translates at run time and leaves the executable alone.

## Limitations

**Replays are the big one.** A replay recorded with this patch carries extra metadata — the logic
rate, the per-tick input stream, the game and simulation revision, and the gameplay settings —
appended to the game's own untouched payload. That means:

- **A replay recorded with the patch is not guaranteed to play back correctly in the unmodified
  game, or in a different build of this patch.** The native payload is still there, but the
  patch's own simulation produced the run. Keep the build you recorded with if a replay matters.
- **Replays recorded *before* the patch play back fine**, at 60 Hz logic, presented at your
  display rate.
- Replays from older HFR versions keep their rate and input data, but their exact simulation
  revision is unknown and playback may desynchronise. You are warned on screen when a replay
  carries metadata this build does not understand.
- TH13 keeps its replays in `%APPDATA%\ShanghaiAlice\th13\replay\`; the others keep them beside
  the executable.

**Scores are not comparable.** Do not submit scores set with this patch to leaderboards that
expect an unmodified game, and do not assume a replay of one proves anything to anyone else.

**Sub-stepping can change outcomes, and usually makes the game harder.** Testing collision
several times per frame means a bullet that would have jumped clean past you between two 60 Hz
frames can now hit you. That is more *correct*, not more forgiving, but a pattern you have
practised at 60 Hz may not behave identically.

**Full-run replay parity has not been tested.** Every supported game has been played on this
build and the automated tests cover the scheduler, the patch transaction and the emitted code,
but nobody has yet played identical replays through the stock game and the patched game and
compared them frame by frame.

**Per-game gaps.**

- Internal resolution is fully supported on TH13; the other games get the sharper rasterisation
  but their sprite snapping is not yet mapped.
- `dim_special` only does something in TH13 (divine spirits); TH10–TH12 have no fifth class.
- TH10 has no native window-size dialog beyond 640x480, so use `window_scale` or F10 there.

## New Classic

New Classic is a modern 64-bit remake, so nothing from the x86 runtime transfers. It has its own
backend and a smaller feature set:

- The simulation stays at **60 Hz** and is presented at the display rate, with sprite position,
  rotation and scale smoothed between native frames — including on the menus and title screen.
- **Dimming works** (background, items, effects, your own shots). Scaling, filters, sharpening,
  internal resolution and window management do not exist here; use the game's own display
  settings.
- Two settings, **both off by default**, take parts of the simulation to the display rate.
  *Sub-tick player movement* polls input and moves the player once per drawn frame, so a
  direction change takes effect within the frame you make it; holding a direction still covers
  the stock distance per frame. *Sub-stepped projectiles* advance enemy bullets and lasers a
  fraction of a frame at a time, running culling, grazing and collision at every step.
- Enemies, the player's own shots, items and every script stay at 60 Hz. That is deliberate:
  their discrete effects (enemy damage above all) are applied once per 60 Hz frame, so
  sub-stepping their motion could not change an outcome. See
  [§18](docs/games/TH06NC_DEVNOTES.md#18-lasers-and-where-the-parity-with-th10-13-actually-is-2026-09-13)
  and [§22](docs/games/TH06NC_DEVNOTES.md#22-items-fell-at-the-tick-rate-and-the-finished-dimming-map-2026-09-13).
- **Neither setting is safe for replays, and nothing turns them off for you.** New Classic's
  replay format stores one input word per 60 Hz frame, which cannot describe a player who moved
  from six input samples. Turn both off by hand before recording or watching a replay.

[What New Classic has that TH10–13 do not, and the other way round](docs/games/TH06NC_VS_TH10_13.md)
is the full comparison.

## Troubleshooting

If the game does not start, or starts unpatched, the usual causes are an executable version the
patch does not recognise, another `dinput8.dll` already in the folder, or a second patch. Try the
game with only this patch installed before anything else, and read `touhou_hfr.log` — it says
which game it recognised, or why it declined.

Two things the patch will tell you on screen. If another patch has taken over the game's frame
loop you get a conflict message; this patch replaces what vpatch did for these games, so you
should not need both. If a Direct3D 9 wrapper is presenting the game you get a warning that
scaling and borderless fullscreen cannot work — the filters still do.

### Upgrading from th11_hfr or th12_hfr

If you have one of the two original per-game patches installed, from PowerShell:

```powershell
.\install.ps1 -GameDirectory 'G:\Touhou\TH11 ~ Subterranean Animism'
```

The script knows TH11 and TH12 only, which is all the old patches covered; for TH10, TH13 and
New Classic there is nothing to upgrade *from*, so use the plain install above. It verifies the
target, backs the folder up to `hfr-backups/<timestamp>/`, installs the new files and repoints the
old `th11_hfr.dll` / `th12_hfr.dll` aliases so existing shortcuts keep working and no old patch
loads alongside the new one. Your existing `touhou_hfr.ini` is kept; otherwise the legacy INI is
carried over. To undo it, restore the timestamped backup and delete the files its `manifest.json`
lists with `existed: false`.

Doing it by hand: replace any `th11_hfr.dll` / `th12_hfr.dll` with a copy of the new
`touhou_hfr.dll`, replace the old launcher, and keep your settings in the common INI. If some
*unrelated* `dinput8.dll` proxy is already in the folder, do not simply overwrite it — two
proxies need deliberate handling.

## For developers

[ARCHITECTURE.md](ARCHITECTURE.md) is the source map, the replay format and the validation
scope, and [ADDING_A_GAME.md](ADDING_A_GAME.md) is what a new game needs. Everything else is
under [docs/](docs/README.md): what was learned taking the patch to four games, how the scaler
and menu work, the compatibility audits, the per-game reverse-engineering records, and how to
build and test.

### Build and test

Windows needs a **32-bit MinGW-w64 GCC** (default `C:\msys64\mingw32\bin\gcc.exe`; pass
`-Compiler` to override):

```powershell
.\build.ps1
.\test.ps1 -GameExe 'G:\Touhou\TH11 ~ Subterranean Animism\th11.exe','G:\Touhou\TH12 ~ Undefined Fantastic Object\th12.exe','G:\Touhou\TH13 ~ Ten Desires\th13.exe' -Python 'C:\Python313\python.exe'
.\package.ps1
```

For New Classic, also install **64-bit MinGW-w64 GCC/G++** (default
`C:\msys64\mingw64\bin\gcc.exe`), run `build.ps1` first, then:

```powershell
.\build64.ps1
.\test64.ps1 -GameExe 'C:\Program Files (x86)\Steam\steamapps\common\th06nc\'
.\package.ps1 -IncludeExperimental64
```

`build.sh`, `build64.sh`, `test.sh`, `test64.sh` and `package.sh` do the same jobs in a shell with
`i686-w64-mingw32-gcc`, `x86_64-w64-mingw32-gcc` and `zip`, running the Windows binaries under
Wine. [docs/TESTING.md](docs/TESTING.md) says what each suite covers, where the two differ, and
what to check by hand before a release.

## Credits, tools and resources

Developed by Vittorio Romeo with AI assistance — the original TH12 patch and the shared
multi-game runtime with OpenAI's ChatGPT, everything from the scaler onward with Anthropic's
Claude. Each commit names its co-author, and the record of what found what is in the git history
and the per-game devnotes.

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
| [MinHook](https://github.com/TsudaKageyu/minhook) | Tsuda Kageyu | BSD-2-Clause | function hooks in the x64 runtime (`third_party/minhook`) |
| MMPX | Morgan McGuire and Mara Gagiu; slang port by hunterk | MIT | `shaders/mmpx.hlsl` |
| xBR-lv2, Super-xBR | Hyllian | MIT | `shaders/xbr-lv2.hlsl`, `shaders/super-xbr.hlsl` |
| ScaleFX | Sp00kyFox | MIT | `shaders/scalefx.hlsl` |
| FidelityFX CAS | Advanced Micro Devices | MIT | `shaders/cas.hlsl` |

The filters were ported to Direct3D 9 HLSL from the
[libretro/slang-shaders](https://github.com/libretro/slang-shaders) and
[libretro/common-shaders](https://github.com/libretro/common-shaders) versions; the licence
texts travel at the top of each file, and `shaders/README.md` records what the ports changed
and which well-known filters (xBRZ, hqx, NNEDI3, FSRCNNX) were left out and why.

**Build and test environment.** MinGW-w64 GCC on Windows and Linux; PowerShell and POSIX shell
scripts. Development and every automated run happened under [Wine](https://www.winehq.org/) 9 on
Linux with Xvfb, `xdotool` driving the games and ImageMagick reading the screen; Direct3D
behaviour was confirmed on Windows in play.

Touhou Project is © Team Shanghai Alice / ZUN. This mod contains no game files and patches the
games only in memory.
