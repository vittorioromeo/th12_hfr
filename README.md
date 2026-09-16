# Touhou HFR

**Touhou, running at your monitor's refresh rate.** These games were built around 60 frames a
second. On a 144, 240 or 360 Hz display this patch draws — and moves the player, the bullets and
your shots — that many times a second instead, while keeping the game's own logic on its exact
60 Hz schedule, so nothing about the game changes except how smooth it is and how quickly it
answers your input.

It also gives you a window you can resize, decent upscaling filters instead of whatever your
driver picks, optional dimming so bullets stand out against the background, and a settings menu
on **F11** that applies changes as you make them.

That describes Touhou 10 through 13. Touhou Koumakyou: New Classic is a 64-bit remake with its
own, more limited backend — [see below](#new-classic-is-experimental-and-different).

**Nothing is installed and no game file is modified.** The patch lives entirely in memory; you
add four files to the game's folder and remove it by deleting them again.

| [![Touhou HFR — v0.4 showcase](https://img.youtube.com/vi/EmfJLZxlHZI/hqdefault.jpg)](https://www.youtube.com/watch?v=EmfJLZxlHZI) | [![Touhou HFR — High Framerate and Refresh Rate Patch](https://img.youtube.com/vi/H7FcMXPgPFQ/hqdefault.jpg)](https://www.youtube.com/watch?v=H7FcMXPgPFQ) |
| :---: | :---: |
| **[v0.4 showcase](https://www.youtube.com/watch?v=EmfJLZxlHZI)** | **[High Framerate and Refresh Rate Patch](https://www.youtube.com/watch?v=H7FcMXPgPFQ)** |

## Supported games

**Current build: v0.5.3-test.**

| Game | Version | Executables | State |
| --- | --- | --- | --- |
| Touhou 10 — Mountain of Faith | v1.00a | `th10.exe`, `th10e.exe` | supported |
| Touhou 11 — Subterranean Animism | v1.00a | `th11.exe`, `th11e.exe` | supported |
| Touhou 12 — Undefined Fantastic Object | v1.00b | `th12.exe`, `th12e.exe` | supported |
| Touhou 13 — Ten Desires | v1.00c | `th13.exe`, `th13e.exe` | supported |
| Touhou Koumakyou: New Classic | see [notes](TH06NC_DEVNOTES.md#2-product-and-inspected-build) | `th06nc.exe` | **experimental**, smaller feature set — [details](#new-classic-is-experimental-and-different) |

Japanese and English executables are both supported, and so are the Steam releases. Other
versions, other games, and the `th06c.exe` (Classic) executable bundled with New Classic are
not: the patch verifies the executable's code before touching anything and declines to install
if it does not recognise it.

**On Steam, start the game from Steam.** A Steam copy of TH10–13 is the same game inside a DRM
wrapper that keeps its code encrypted until the game itself starts, so `touhou_hfr.exe` cannot
check it or launch it and will tell you so. `dinput8.dll` does not care: put the four files in
the folder, press Play, and the patch installs itself once the game is running.

## Install

Close the game. Download and extract the release archive, then copy these four files next to
the game's executable:

```
dinput8.dll
touhou_hfr.dll
touhou_hfr.exe
touhou_hfr.ini
```

That is the whole installation. Start the game as you normally would — `dinput8.dll` loads the
patch automatically — or run `touhou_hfr.exe`, which finds the game beside it and starts it.

Steam and standalone copies are both fine. On a Steam copy the executable's code is encrypted
until the game itself starts, so press **Play** in Steam rather than running `touhou_hfr.exe`;
`dinput8.dll` installs the patch on that launch just the same.

Press **F11** in game for the settings menu. Most settings are there, changes apply immediately,
and **Save** writes them to `touhou_hfr.ini` so they become the default. A few — internal
resolution, texture upscaling, and the key bindings — are INI-only and need a restart; the
[settings tables](#settings) say which.

The same four files work for every supported game, so you can copy the same folder contents
into each. If you keep several games in one folder, set `[launcher] exe=th12.exe` in the INI
(or pass the executable name to `touhou_hfr.exe`) to say which one you mean.

The bundled filters are compiled into the DLL, so those four files really are everything. The
`shaders/` folder in the archive is only needed if you want to drop in extra `.hlsl` filters of
your own — copy it next to the game and the patch will offer whatever is in it.

**Keys:** **F11** opens the settings menu. **F10** steps the window through 1x, 1.5x, 2x and
borderless fullscreen. Both can be changed or switched off in the INI.

**Is it working?** Open F11 → Timing: it shows the rate it is presenting at and the rate it is
simulating at, measured, side by side. The first should be your refresh rate (or whatever you
set `fps` to) and the second should sit at 60. The game's own on-screen FPS readout counts
presented frames too, so it should agree with the first number.

**To uninstall**, delete those four files.

### New Classic

New Classic is 64-bit, so it needs different files. Put these in the game's `th06nc` folder —
the one containing `th06nc.exe`:

```
dxgi.dll
touhou_hfr64.dll
touhou_hfr.ini
```

Then start the game from Steam however you normally do. The game loads
`dxgi.dll` itself, so the patch installs on any launch, exactly as `dinput8.dll` does for
TH10–13. **Do not put the 32-bit `dinput8.dll` in this game**; it cannot load.

One conflict to know about: ReShade also installs as `dxgi.dll`. Only one of them can have
that name in the folder.

### Upgrading from th11_hfr or th12_hfr

From PowerShell:

```powershell
.\install.ps1 -GameDirectory 'G:\Touhou\TH11 ~ Subterranean Animism'
```

The script knows TH11 and TH12 only, which is all the old patches covered; for TH10, TH13 and
New Classic there is nothing to upgrade *from*, so use the plain install above. It verifies the
target, backs the folder up to `hfr-backups/<timestamp>/`, installs the new
files and repoints the old `th11_hfr.dll` / `th12_hfr.dll` aliases so existing shortcuts keep
working and no old patch loads alongside the new one. Your existing `touhou_hfr.ini` is kept;
otherwise the legacy INI is carried over. To undo it, restore the timestamped backup and delete
the files its `manifest.json` lists with `existed: false`.

Doing it by hand: replace any `th11_hfr.dll` / `th12_hfr.dll` with a copy of the new
`touhou_hfr.dll`, replace the old launcher, and keep your settings in the common INI. If some
*unrelated* `dinput8.dll` proxy is already in the folder, do not simply overwrite it — two
proxies need deliberate handling.

## What you get

**High frame rate.** Movement, bullets, shots and other suitable systems update at the display
rate, in sub-steps whose lengths add up to exactly 60 game frames a second. Enemy scripts and
frame-sensitive decisions keep their 60 Hz timing, and enemy sprites are interpolated so they
look smooth anyway. On Direct3D 9Ex the patch can also shorten the queue of frames the driver
holds before displaying one, which takes a little more delay out of the gap between your input
and the screen.

**A window that resizes.** Drag it, set it at startup with `window_scale`, or press **F10** to
step through 1x, 1.5x, 2x and borderless fullscreen — which is what TH11 and later offer
natively and TH10 does not, its own dialog being stuck at 640x480. The picture can be fitted
with letterboxing, stretched, or held to whole-number multiples for pixel-perfect output, and
the game's fullscreen can become a borderless window covering the monitor.

**Upscaling filters.** MMPX, xBR-lv2, Super-xBR and ScaleFX are bundled, alongside nearest,
bilinear and sharp-bilinear. Any `.hlsl` file dropped into a `shaders/` folder next to the game
is offered too, including multi-pass ones; `shaders/README.md` describes the format. After the
filter, an optional sharpening pass — AMD's CAS or a clamped unsharp mask, with a strength
slider — runs over the finished picture. That is what to reach for if scaling to an awkward
window size has left the image looking soft; it works on the output, not on the game's art.

**Internal resolution.** `internal_scale=2` (or `3`) makes the game draw at 1280x960 (1920x1440)
while keeping every coordinate it has, so sprites land on real sub-pixel positions instead of
snapping to a 640x480 grid — a slow bullet at 360 Hz glides instead of stepping — and the 3D
backgrounds gain real detail. `texture_scale=2` then magnifies every texture the game loads,
once, at load time, with a filter of your choice: pixel-art upscaling applied to the art itself
rather than to the finished frame, alpha edges included.

**Readability.** Five sliders fade the things that compete with the enemy's bullets: the stage
background towards black, and the P/point pickups, cosmetic effects (explosions, hit sparks,
bullet cancels, particles), your own shots and options, and a game's own extra class (TH13's
divine spirits) towards transparent. Enemies, bullets, lasers, the player and the interface are
never touched.

**An in-game menu.** F11, four tabs, applied as you move the slider. Anything that cannot take
effect in your current setup is shown disabled with the reason, rather than silently ignored.
The Timing tab shows the presentation and simulation rates it is actually measuring.

**It checks before it touches anything.** The patch declines to install if the game's code is not
what it expects. If another patch that takes over the frame loop (vpatch and the like) is already
in the process, it refuses outright; if that patch loads afterwards, it is too late to refuse, so
you get a warning instead and both keep running. A Direct3D 9 wrapper such as PivotDX9 also gets
a warning, because a wrapper presenting the game takes the scaling modes and borderless
fullscreen away.

## If something is wrong

The patch writes `touhou_hfr.log` next to the game, and it is verbose on purpose: it records
which game it recognised, every setting it read, what it patched, and the measured rates once a
second. If you report a problem, that file is what to send.

The log's first frame also lists every module in the process that did not come from Windows
itself, so a report says what else was loaded alongside it without anyone having to ask.

Two things it will tell you on screen. If another patch has taken over the game's frame loop you
get a conflict message — Touhou HFR replaces what vpatch did for these games, so you should not
need both. If a Direct3D 9 wrapper is presenting the game you get a warning that scaling and
borderless fullscreen cannot work; the filters still do.

If the game does not start, or starts unpatched, the usual causes are an executable version the
patch does not recognise (it says so in the log and declines rather than guessing), another
`dinput8.dll` already in the folder, or a second patch. Try the game with only this patch
installed before anything else.

## Limitations

Read this part before you use the patch for anything you care about.

**Replays are the big one.** A replay recorded with this patch carries extra metadata (the logic
rate, the per-tick input stream, the game and simulation revision, and the gameplay settings)
appended to the game's own untouched payload. That means:

- **A replay recorded with the patch is not guaranteed to play back correctly in the unmodified
  game, or in a different build of this patch.** The native payload is still there, but the
  patch's own simulation produced the run. Keep the build you recorded with if a replay matters.
- **Replays recorded *before* the patch play back fine**, at 60 Hz logic, presented at your
  display rate.
- Replays from older HFR versions keep their rate and input data, but their exact simulation
  revision is unknown and playback may desynchronise. You are warned on screen only when a
  replay carries metadata this build does not understand; a replay from *before* metadata
  existed is noted in the log and nowhere else.
- TH13 keeps its replays in `%APPDATA%\ShanghaiAlice\th13\replay\`; the others keep them beside
  the executable.

**Scores are not comparable.** Running the simulation at a higher rate is a change to the game.
Do not submit scores set with this patch to leaderboards that expect an unmodified game, and do
not assume a replay of one proves anything to anyone else.

**Sub-stepping can change outcomes, and usually makes the game harder.** Testing collision
several times per frame means a bullet that would have jumped clean past you between two 60 Hz
frames can now hit you. That is more *correct*, not more forgiving. It also means a pattern you
have practised at 60 Hz may not behave identically.

**No full-run replay verification has been done.** Every supported game has been played on this
build and the automated tests cover the scheduler, the patch transaction and the emitted code —
but nobody has yet played identical replays through the stock game and the patched game and
compared the results frame by frame. Until that exists, treat "the simulation is unchanged at
60 Hz boundaries" as a well-argued design claim rather than a measured fact.

**Per-game gaps.**

- Internal resolution is fully supported on TH13; the other games get the sharper rasterisation
  but their sprite snapping is not yet mapped.
- `dim_special` only does something in TH13 (divine spirits); TH10–TH12 have no fifth class.
- TH10 has no native window-size dialog beyond 640x480, so use `window_scale` or F10 there.

**Other patches.** vpatch and Direct3D 9 wrappers want the same parts of the game as this one.
The patch detects the ones it knows and tells you; it cannot detect everything. If something
looks wrong, try the game with only this patch installed. Rotation wrappers, practice tools and
translation patches are separate cases — see below.

### Rotation and layout wrappers (THRotator)

**THRotator and this patch work together** on TH10–13. They divide the job rather than share
it: THRotator owns the picture — the render target, the rotation, the HUD layout, the window
and the presentation — and this patch owns everything upstream of that, which is the frame
rate and its pacing, sub-stepping, interpolation, dimming, replays and input.

Install both as each normally wants; `d3d9.dll` and `dinput8.dll` do not collide, so the game
loads them both. This patch recognises THRotator when it starts and steps out of the picture
by itself. The log says so, in one block listing what each side is doing.

What this patch stops doing in that mode: scaling mode, upscaling filters, sharpening,
internal resolution, texture upscaling, window sizing and borderless fullscreen. THRotator is
doing its own version of all of those, in its own coordinate system, and two programs
composing one image is how you get a picture nobody can explain. Configure them in THRotator.

`external_renderer` under `[video]` decides this: `-1` (the default) recognises it,
`1` forces the same treatment for another renderer this patch does not know by name, and `0`
keeps composing the picture here.

**F11 works here too.** Drawing it over someone else's composed image needs a scene of this
patch's own, and with a wrapper in the way the call that closes that scene is the wrapper's
compositor — which would draw its picture over the menu. This patch gets past that by drawing
through the device underneath the wrapper, so the menu lands on the presented image, in screen
space, the right way up. Rotating and resizing are handled: the surface is picked up again
after every device reset, which is what THRotator does each time it turns the picture.

Confirmed on TH12 with THRotator 2.1.0 at 360 Hz. If the menu ever does not appear, the log
says which of the two paths it took, and the INI is read normally either way.

### Practice tools (thprac)

**thprac and this patch work together** on TH10–13. They want different parts of the game:
thprac owns the practice menu, the stage and section jumps and the replay tools, and this patch
owns the frame loop underneath them. Their patch sites do not touch anywhere —
`tools/check_thprac_overlap.py` compares thprac's own hook declarations against every byte this
patch writes and every byte it verifies, and for all four games the two sets are disjoint.

Install both as each normally wants and launch from thprac's launcher, or attach thprac to a
running game. `dinput8.dll` loads this patch either way.

**One thing to change in thprac's launcher: untick "Use VsyncPatch (if avaliable)" and "Use
OpenInputLagPatch (if avaliable)".** Both are on by default, and thprac will load either one it
finds sitting in the game's folder — an old `vpatch_th12.dll` you have not thought about in
years is enough. Those patches replace the game's frame limiter, which is the one job this patch
cannot share, so it refuses to install beside them. If that happens you get a message box naming
those two options; untick them, or delete the file from the game's folder.

**This needs v0.5.3-test or newer.** Before that, thprac's menu never appeared with this patch
installed — no error, nothing in either log. This patch replaces the game's update runner, and
thprac's menu hook sits on that function's very last instruction, so replacing it took the hook
away. The replacement now finishes on the game's own instruction instead of one of its own, and
anything hooked there runs as it always did. Nothing in this patch knows thprac exists; the
instruction simply stopped being taken.

Two smaller things:

- **The menu updates at the display rate**, not at 60 Hz, which is what keeps it in step with
  the drawing. Hotkeys are unaffected — they trigger on the press — but anything in thprac you
  hold down to repeat will repeat proportionally faster.
- **Leave `internal_scale` at 1 while using thprac** if you can. thprac sizes its interface from
  the back buffer at startup, where it agrees with this patch, but takes it from the
  presentation parameters again after a device reset, where it does not once the internal
  resolution is higher. The first launch looks right; the first window resize or Alt-Tab after
  that can leave its menu the wrong size.

Confirmed on TH12 with thprac 2.3.1.1 at 360 Hz, and on TH10, TH11 and TH13 against a stand-in
reproducing thprac's hook mechanism. Running thprac and THRotator together with this patch has
not been tried.

### Translation patches (thcrap)

**thcrap works alongside this patch**, on Steam and standalone copies of TH10–13 alike. The two
change different things: thcrap replaces text, fonts and images, this patch replaces the frame
loop and the renderer, and they do not write over each other anywhere. Install both and start
the game through thcrap's shortcut as usual; `dinput8.dll` loads this patch on that launch like
any other.

That is checked rather than assumed: `tools/check_patch_overlap.py` compares thcrap's own game
definitions against every byte this patch writes and every byte it verifies, and for TH10, TH11,
TH12 and TH13 the two sets are disjoint.

Two things to know:

- **Install order does not matter, but both must be set up against the same executable.** thcrap
  identifies the game by the hash of the `.exe`; this patch never modifies the file on disk, so
  thcrap sees exactly what it expects either way.
- **thcrap's own Direct3D features do not engage while `d3d9ex=1`** (the default). This patch
  creates the Direct3D object through 9Ex, which steps over whoever else is in that chain, so
  thcrap's translation notes and its device-lost handling are skipped. Everything thcrap does to
  text, fonts, images and files is unaffected — that is where the translation actually lives. If
  you want those extras, set `d3d9ex=0`, at the cost of `max_frame_latency`. The log says when
  this applies.
- **This needs v0.5.2-test or newer.** Earlier builds were silently switched off by thcrap: it
  redirects the same imports, matching by name and overwriting whatever it finds, which removed
  this patch's Direct3D hook and left the game running with no sign that anything was missing.
  This build takes those imports back once the game is running, with thcrap left in the call
  path rather than discarded, and says so in the log when it happens.

The one combination that does not work is an **executable that has already been translated** —
the old pre-thcrap English patches that ship a modified `th10e.exe` and friends. Those rewrite
the game's code, so this patch no longer recognises it and declines. thcrap is the supported way
to play in English; it translates at run time and leaves the executable alone.

### New Classic is experimental, and different

New Classic is a modern 64-bit remake, so nothing from the x86 runtime transfers. It has its own
backend, and a smaller feature set:

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
  [§18](TH06NC_DEVNOTES.md#18-lasers-and-where-the-parity-with-th10-13-actually-is-2026-09-13)
  and [§22](TH06NC_DEVNOTES.md#22-items-fell-at-the-tick-rate-and-the-finished-dimming-map-2026-09-13).
- **Neither setting is safe for replays, and nothing turns them off for you.** New Classic's
  replay format stores one input word per 60 Hz frame, which cannot describe a player who moved
  from six input samples. Turn both off by hand before recording or watching a replay. (A flag
  that looked like it would disable them automatically turned out not to mean that —
  [§17](TH06NC_DEVNOTES.md#17-why-neither-feature-had-ever-run-and-what-the-fps-readout-counts-2026-09-13).)

[What New Classic has that TH10–13 do not, and the other way round](TH06NC_VS_TH10_13.md) is the
full comparison.

## Settings

To edit by hand, open `touhou_hfr.ini` beside the game and restart. For TH10–13 the defaults
detect your display's refresh rate and turn on sub-stepping, sub-tick input and D3D9Ex. New
Classic's own settings live in `[fixed60]` and its two gameplay options are off by default.

Settings marked **INI only** do not appear in the F11 menu; those marked **restart** take effect
the next time the game starts.

### `[hfr]` — timing

| Setting | Default | Meaning |
| --- | --- | --- |
| `fps` | `0` | Automatic display rate; explicit logic rates of 60–1000 are accepted |
| `vsync` | `1` | Vsync pacing; `0` uses the software presentation limiter |
| `substep` | `1` | Smooth gameplay updates; `0` keeps logic at 60 Hz with HFR presentation |
| `subtick_input` | `1` | Sample movement and focus between stock frame inputs |
| `enemy_interp` | `1` | Interpolate enemy sprites between 60 Hz positions |
| `d3d9ex` | `1` | Use Direct3D 9Ex when available |
| `max_frame_latency` | `1` | D3D9Ex queued-frame limit; `0` leaves the driver default |
| `fullscreen_refresh` | `0` | Exclusive fullscreen rate; `0` follows automatic/explicit `fps`. **INI only** |
| `flipex` | `0` | Experimental windowed flip presentation. **INI only, restart** |
| `log` | `1` | Write `touhou_hfr.log` beside the game. **INI only**; New Classic always logs |
| `debug` | `0` | Periodic state diagnostics; `2` and `3` add denser draw tables |

### `[video]` — picture, window and readability

| Setting | Default | Meaning |
| --- | --- | --- |
| `scaling` | `1` | `0` stretch to fill, `1` fit with letterboxing, `2` whole-number scale only |
| `filter` | `sharp-bilinear` | `nearest`, `bilinear`, `sharp-bilinear`, `mmpx`, `xbr-lv2`, `super-xbr`, `scalefx`, or any `.hlsl` in `shaders/` |
| `sharpen` | `none` | Post-filter sharpening: `none`, `cas`, `unsharp-mask` |
| `sharpen_strength` | `50` | 0–100 |
| `resizable` | `1` | Resize border on the window |
| `window_scale` | `0` | Startup size as a percentage of 640x480 (`200` = 1280x960); `-1` = largest whole multiple that fits the screen; `0` = as the game made it |
| `snap_aspect` | `0` | Hold the window at 4:3 while dragging |
| `fullscreen_mode` | `1` | `1` the game's fullscreen becomes a borderless window covering the monitor; `0` leave it alone |
| `cursor` | `2` | Mouse pointer in borderless fullscreen: `0` never, `1` always, `2` while it moves. Always shown while the menu is open |
| `menu_key` | `122` | Virtual-key code of the in-game menu (F11); `0` disables it. **INI only** |
| `size_cycle_key` | `121` | Key that steps 640x480 → 960x720 → 1280x960 → borderless fullscreen (F10); `0` = off. **INI only** |
| `warn_wrapper` | `1` | Say at startup when a d3d9 wrapper is presenting the game. **INI only** |
| `own_present` | `-1` | Which swap chain reaches the screen; `-1` decides automatically |
| `internal_scale` | `1` | Draw the game at N times 640x480, 1–4. **INI only, restart** |
| `texture_scale` | `0` | Magnify every loaded texture N times at load time, 2–4 (`1` does nothing); pair with `internal_scale`. **INI only, restart** |
| `texture_filter` | `xbr-lv2` | The filter for that: `xbr-lv2`, `mmpx`, `super-xbr`, `scalefx`, or a `.hlsl` from `shaders/`. **INI only, restart** |
| `dim_background` | `0` | Fade the stage background towards black by this percentage |
| `dim_items` | `0` | Fade the P and point pickups towards transparent |
| `dim_effects` | `0` | Fade explosions, hit sparks, bullet cancels and particles |
| `dim_special` | `0` | Fade the game's own extra class — TH13's divine spirits; nothing in TH10–TH12 or New Classic |
| `dim_player_shots` | `0` | Fade your own shots and options |

### `[fixed60]` — New Classic only

`[hfr] fps` sets the presentation rate for New Classic too; these control its backend.

| Setting | Default | Meaning |
| --- | --- | --- |
| `interpolate` | `1` | Smooth sprite position, rotation and scale between 60 Hz frames |
| `subtick` | `0` | Sub-tick player movement (see the limitations above) |
| `substep` | `0` | Sub-stepped enemy bullets and lasers (see the limitations above) |
| `vsync` | `0` | D3D11 vsync; separate from `[hfr] vsync` so the D3D9 default is untouched |
| `diag_seconds` | `0` | Run for N seconds, log the draw and update lists, and quit. Leave at 0 |

## For developers

**`[systems]` in the INI** holds per-system switches for troubleshooting, defaulting to each
adapter's audited classification of which game systems may run at sub-tick rate. Turning one on
does not make a system classified as frame-only run at sub-tick rate; it only removes one that
was allowed to.

**Architecture and porting.** [ARCHITECTURE.md](ARCHITECTURE.md) is the source map, the replay
format and the validation scope; [ADDING_A_GAME.md](ADDING_A_GAME.md) is what a new game needs.
[DEVNOTES_RUNTIME.md](DEVNOTES_RUNTIME.md) is what was learned taking the patch to four games,
and [RESOLUTION.md](RESOLUTION.md) covers the scaler, filters, window and menu.

**Per-game reverse-engineering records.** [TH10](TH10_DEVNOTES.md), [TH11](TH11_DEVNOTES.md),
[TH12](DEVNOTES.md), [TH13](TH13_DEVNOTES.md) and
[New Classic](TH06NC_DEVNOTES.md). The original per-game release instructions survive in
[TH11_README.md](TH11_README.md) and [TH12_README.md](TH12_README.md).

Two notes on the engines. TH10 predates the single game-speed float that TH11 and TH12 hang
their sub-stepping off, so its speed model is described on its own terms
([DEVNOTES_RUNTIME.md](DEVNOTES_RUNTIME.md) §7); everything the other games have works on it.
TH13 is TH12's engine with a few structural changes, each named by a profile field (§7a). The
current source also fixes TH10's recurring HFR hitches, which came with a brief `0.0fps`
reading: its native FPS watchdog mistook rates above 65 FPS for a broken clock and reset the
timer ([TH10_DEVNOTES §6b](TH10_DEVNOTES.md#6b-hfr-hitches-and-the-fps-counter-briefly-reading-zero-2026-09-12)).

### Build and test

Windows needs a **32-bit MinGW-w64 GCC** (default `C:\msys64\mingw32\bin\gcc.exe`; pass
`-Compiler` to override):

```powershell
.\build.ps1
.\test.ps1 -GameExe 'G:\Touhou\TH11 ~ Subterranean Animism\th11.exe','G:\Touhou\TH12 ~ Undefined Fantastic Object\th12.exe','G:\Touhou\TH13 ~ Ten Desires\th13.exe' -Python 'C:\Python313\python.exe'
.\package.ps1
```

For New Classic, also install **64-bit MinGW-w64 GCC/G++** (default
`C:\msys64\mingw64\bin\gcc.exe`):

```powershell
.\build64.ps1
.\test64.ps1 -GameExe 'C:\Program Files (x86)\Steam\steamapps\common\th06nc\'
.\package.ps1 -Version '0.5.3-test' -IncludeExperimental64
```

Run `build.ps1` before `test64.ps1`, which checks both launcher architectures. `build64.ps1`
produces `touhou_hfr64.dll` and its launcher helper.

`build.sh`, `build64.sh`, `test.sh`, `test64.sh` and `package.sh` do the same jobs in a shell
with `i686-w64-mingw32-gcc`, `x86_64-w64-mingw32-gcc` and `zip`, running the Windows binaries
under Wine. They are not quite equivalent: `test.ps1` additionally runs the per-game executable
identification and the Unicorn emulation of the emitted x86 stubs, which `test.sh` does not, so
a release should be validated from PowerShell. `test64.sh` needs a 64-bit Wine and finds one
itself. The Python tests need `pefile` and `unicorn` (`python -m pip install pefile unicorn`).

What the tests cover: the shared scheduler, the installer, the replay codec, input and pauses,
the emitted x86 stubs executed in Unicorn, and each supported executable identified from its
frozen signatures; for the x64 runtime, the clock, the pose history, the sub-step schedule, the
real patch transaction, every emitted AMD64 relay, the frozen signatures, the dimming rules and
pools, and the menu's contents. The native harness maps your real executables as inert fixtures
and never ships them; artifacts stay in ignored `build/tests/`. Include both Japanese and
English executables when validating a release.

## Credits, tools and resources

The mod was developed by Vittorio Romeo with AI assistance. The record of what model found what
is in the git history and the per-game devnotes.

**AI models.** The original TH12 patch (v0.10–v0.11), the restructuring into a shared
multi-game runtime with the TH11 adapter and the native regression harness, and the first
TH10 profile were authored with OpenAI's ChatGPT ("ChatGPT 6.0 Astra" in the import commit;
the TH10 work on a Codex branch). ChatGPT Astra also found the cause of TH10's HFR hitches —
the game's FPS-counter watchdog resetting its clock — after a long hunt elsewhere had failed
([TH10_DEVNOTES §6b](TH10_DEVNOTES.md)). Everything from the scaler onward — output scaling and
the filter chain, the Dear ImGui menu, Direct3D 9Ex presentation through an additional swap
chain, the TH10 validation and the TH13 port, internal resolution and texture upscaling,
dimming, sharpening, the joystick thread, the SSE clock fix, the whole New Classic backend and
most of the devnotes — was developed with Anthropic's Claude (Opus 4.8, Opus 5 and Fable 5.1,
through Claude Code and Cowork), which also extended the harness and built and drove the Wine
test rig. Each commit names its co-author.

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
scripts; the native harness in `tools/test_hfr.c` runs against the real executables as inert
fixtures. Development and every automated run happened under [Wine](https://www.winehq.org/) 9
on Linux with Xvfb, `xdotool` driving the games and ImageMagick reading the screen; Direct3D
behaviour was confirmed on Windows in play. Microsoft's Direct3D 9, Direct3D 11 and Win32
documentation was the reference for everything the patch asks of the system.

Touhou Project is © Team Shanghai Alice / ZUN. This mod contains no game files and patches the
games only in memory.
