# Touhou HFR

Runs Touhou at your monitor's refresh rate. On a 144, 240 or 360 Hz display the game is drawn —
and the player, bullets and shots move — that many times a second, while game logic keeps its
60 Hz schedule.

Also: a resizable window, borderless fullscreen, pixel-art upscaling filters, optional dimming
of backgrounds and effects, and an in-game settings menu on **F11**.

No game file is modified. You add four files to the game's folder and delete them to uninstall.

**[Download the latest release](https://github.com/vittorioromeo/th12_hfr/releases)** · [Changelog](CHANGELOG.md)

| [![Touhou HFR — v0.4 showcase](https://img.youtube.com/vi/EmfJLZxlHZI/hqdefault.jpg)](https://www.youtube.com/watch?v=EmfJLZxlHZI) | [![Touhou HFR — High Framerate and Refresh Rate Patch](https://img.youtube.com/vi/H7FcMXPgPFQ/hqdefault.jpg)](https://www.youtube.com/watch?v=H7FcMXPgPFQ) |
| :---: | :---: |
| **[v0.4 showcase](https://www.youtube.com/watch?v=EmfJLZxlHZI)** | **[High Framerate and Refresh Rate Patch](https://www.youtube.com/watch?v=H7FcMXPgPFQ)** |


## Supported games

| Game | Version | Executables | State |
| --- | --- | --- | --- |
| Touhou 8 — Imperishable Night | v1.00d | `th08.exe` | experimental — [notes](#imperishable-night) |
| Touhou 10 — Mountain of Faith | v1.00a | `th10.exe`, `th10e.exe` | supported |
| Touhou 11 — Subterranean Animism | v1.00a | `th11.exe`, `th11e.exe` | supported |
| Touhou 12 — Undefined Fantastic Object | v1.00b | `th12.exe`, `th12e.exe` | supported |
| Touhou 13 — Ten Desires | v1.00c | `th13.exe`, `th13e.exe` | supported |
| Touhou 14 — Double Dealing Character | v1.00b | `th14.exe` | partial — [notes](#touhou-14-and-15) |
| Touhou 15 — Legacy of Lunatic Kingdom | v1.00b | `th15.exe` | partial, new — [notes](#touhou-14-and-15) |
| Touhou Koumakyou: New Classic | [see notes](docs/games/TH06NC_DEVNOTES.md#2-product-and-inspected-build) | `th06nc.exe` | experimental — [notes](#new-classic) |

- Requires Windows and a display above 60 Hz. On a 60 Hz display the patch does nothing.
- Steam releases of TH10–13 are supported. **Start them from Steam**: the Steam executable is
  encrypted until it runs, so `touhou_hfr.exe` cannot launch it, but `dinput8.dll` still loads
  the patch.
- The patch verifies the executable's code and does not install on anything it does not
  recognise: other versions, other games, pre-patched English executables, or `th06c.exe`.
- THRotator, thprac and thcrap work alongside it — see [Other mods](#other-mods).

## Install

Close the game, extract the release, and copy these next to the game's executable:

```
dinput8.dll
touhou_hfr.dll
touhou_hfr.exe
touhou_hfr.ini
```

Start the game as usual; `dinput8.dll` loads the patch. `touhou_hfr.exe` is an alternative
launcher (not for Steam copies). The same four files work for every game. With several games
in one folder, set `[launcher] exe=th12.exe` or pass the name to `touhou_hfr.exe`.

The `shaders/` folder is optional: the bundled filters are built into the DLL, and the folder is
only for adding your own `.hlsl` filters.

**Uninstall:** delete the four files.

**New Classic** is 64-bit and uses different files. Put these in the folder containing
`th06nc.exe` and start the game from Steam:

```
dxgi.dll
touhou_hfr64.dll
touhou_hfr.ini
```

Do not add the 32-bit `dinput8.dll` there. ReShade also installs as `dxgi.dll`; only one of the
two can be present.

Upgrading from the old `th11_hfr` / `th12_hfr` patches: see [docs/UPGRADING.md](docs/UPGRADING.md).

## Using it

| Key | |
| --- | --- |
| **F11** | Settings menu. Changes apply immediately; **Save** writes them to `touhou_hfr.ini`. Options that cannot work in the current setup are disabled, with the reason |
| **F10** | Cycles the window: 640x480, 960x720, 1280x960, borderless fullscreen |

Both keys can be rebound or disabled in the INI.

To check that it works, open F11 → Timing: the presented rate should be your refresh rate (or
`fps`), and the simulated rate 60. The game's own FPS counter shows the presented rate.

`touhou_hfr.log`, next to the game, records what was recognised, patched and measured. Attach
it to any bug report.

## Settings

Edit `touhou_hfr.ini` beside the game and restart; every setting is commented in the file.
**INI only** settings are not in the F11 menu. **restart** settings apply at the next start.

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
| `max_frame_latency` | `1` | D3D9Ex queued-frame limit, for lower input lag; `0` leaves the driver default |
| `fullscreen_refresh` | `0` | Exclusive fullscreen rate; `0` follows automatic/explicit `fps`. **INI only** |
| `flipex` | `0` | Experimental windowed flip presentation. **INI only, restart** |
| `log` | `1` | Write `touhou_hfr.log` beside the game. **INI only**; New Classic always logs |
| `debug` | `0` | Periodic state diagnostics; `2` and `3` add denser draw tables |

### `[video]` — picture, window and readability

| Setting | Default | Meaning |
| --- | --- | --- |
| `scaling` | `1` | `0` stretch to fill, `1` fit with letterboxing, `2` whole-number scale only |
| `filter` | `sharp-bilinear` | `nearest`, `bilinear`, `sharp-bilinear`, `mmpx`, `xbr-lv2`, `super-xbr`, `scalefx`, or any `.hlsl` in `shaders/` |
| `sharpen` | `none` | Sharpening over the finished picture: `none`, `cas`, `unsharp-mask` |
| `sharpen_strength` | `50` | 0–100 |
| `resizable` | `1` | Resize border on the window |
| `window_scale` | `0` | Startup size as a percentage of 640x480 (`200` = 1280x960); `-1` = largest whole multiple that fits the screen; `0` = as the game made it |
| `snap_aspect` | `0` | Hold the window at 4:3 while dragging |
| `fullscreen_mode` | `1` | `1` the game's fullscreen becomes a borderless window covering the monitor; `0` leave it alone |
| `cursor` | `2` | Mouse pointer in borderless fullscreen: `0` never, `1` always, `2` while it moves. Always shown while the menu is open |
| `menu_key` | `122` | Virtual-key code of the in-game menu (F11); `0` disables it. **INI only** |
| `size_cycle_key` | `121` | Virtual-key code of the window-size cycle (F10); `0` disables it. **INI only** |
| `warn_wrapper` | `1` | Say at startup when a d3d9 wrapper is presenting the game. **INI only** |
| `external_renderer` | `-1` | Hand the picture to a rotation wrapper such as THRotator: `-1` when one is recognised, `1` always, `0` never. **INI only** |
| `own_present` | `-1` | Which swap chain reaches the screen; `-1` decides automatically |
| `internal_scale` | `1` | Render the game at N × 640x480, 1–4: sub-pixel sprite positions and sharper 3D backgrounds. **INI only, restart** |
| `texture_scale` | `0` | Magnify every loaded texture N times at load time, 2–4 (`0` and `1` are off); pair with `internal_scale`. **INI only, restart** |
| `texture_filter` | `xbr-lv2` | The filter for that: `xbr-lv2`, `mmpx`, `super-xbr`, `scalefx`, or a `.hlsl` from `shaders/`. **INI only, restart** |
| `dim_background` | `0` | Fade the stage background towards black by this percentage |
| `dim_items` | `0` | Fade the P and point pickups towards transparent |
| `dim_effects` | `0` | Fade explosions, hit sparks, bullet cancels and particles |
| `dim_special` | `0` | Fade the game's own extra class — TH13's divine spirits; nothing in TH10–TH12 or New Classic |
| `dim_player_shots` | `0` | Fade your own shots and options |

Dimming never touches enemies, bullets, lasers, the player or the interface.

### `[fixed60]` — Imperishable Night and New Classic

For the two games whose simulation stays at 60 Hz. `[hfr] fps` and `vsync` still set the
presentation rate; `[hfr] substep`, `subtick_input` and `enemy_interp` are ignored.

| Setting | Default | Meaning |
| --- | --- | --- |
| `interpolate` | `1` | Smooth sprite position, rotation and scale between 60 Hz frames |
| `predict` | `1` | TH08 only. Extrapolate the playfield and draw the player from live input instead of showing them up to a frame late ([details](#imperishable-night)). Does not change game state |
| `subtick` | `0` | Sub-tick player movement. Not replay-safe |
| `substep` | `0` | Sub-stepped enemy bullets and lasers (and items, on TH08). Experimental, not replay-safe |
| `d3d9ex` | `0` | TH08 only. Direct3D 9Ex through the Direct3D 8 bridge. **INI only, restart**; unconfirmed on Windows |
| `vsync` | `0` | New Classic only. D3D11 vsync |
| `diag_seconds` | `0` | New Classic only. Diagnostic: log the draw and update lists for N seconds, then quit |

`[systems]` holds per-system sub-stepping switches for troubleshooting. A switch can turn
sub-stepping off for a system, never on for one the game's profile keeps at 60 Hz.

## Other mods

| Mod | Works? | What to do |
| --- | --- | --- |
| [THRotator](https://github.com/massanoori/THRotator) | Yes, TH10–13 | Nothing. It takes over the picture (scaling, filters, window); this patch keeps timing, and F11 still works |
| [thprac](https://github.com/touhouworldcup/thprac) | Yes, TH10–13 | Untick *Use VsyncPatch* and *Use OpenInputLagPatch* in its launcher. Keep `internal_scale=1` |
| [thcrap](https://github.com/thpatch/thcrap) | Yes, TH08 and TH10–13 | Decline vpatch when thcrap offers it. For thcrap's own Direct3D extras (translation notes) on TH10–13, set `d3d9ex=0` |
| vpatch, OpenInputLagPatch | No | Remove them; this patch replaces them |
| Direct3D 9 wrappers (PivotDX9 etc.) | Partly | Scaling and borderless fullscreen are left to the wrapper |

Known conflicts are reported in the log and on screen. If something looks wrong, test with
only this patch installed. Details: [docs/OTHER_MODS.md](docs/OTHER_MODS.md).

## Limitations

- **Replays.** A replay recorded with the patch stores its tick rate and per-tick input after
  the game's own data, and plays back at that rate. It may not play back correctly in the
  unmodified game or in another build of this patch. To record a replay the stock game can
  play, set `substep=0`. Replays recorded without the patch play normally.
- **Scores are not comparable** with the unmodified game. Do not submit them to leaderboards.
- **Sub-stepping can change outcomes.** Collision is tested several times per frame, so a
  bullet that would skip past the hitbox between two 60 Hz frames can now hit. Some timers
  also start a frame early. Patterns may differ slightly from 60 Hz play.
- Replay parity against the stock game has not been verified over full runs.
- Internal resolution is complete on TH13 only; other games lack sprite-snapping support.
- `dim_special` only affects TH13 (divine spirits).

More on replays: [docs/REPLAYS.md](docs/REPLAYS.md).

## Touhou 14 and 15

The two share an engine and are at the same level. TH15 is new in this build and has had less
play time.

Working: the full video path, the F11 menu, sprite animation, and display-rate movement for
bullets, the player, items and lasers. Enemy sprites and the player's options are interpolated.
Replays carry their recording rate.

Missing: sub-tick input (input is sampled once per 60 Hz frame), and the English and Steam
executables, which are unverified.

## Imperishable Night

TH08 uses an older engine (Direct3D 8, a simulation built for one speed), so the approach
differs: **the simulation stays at 60 Hz, unchanged, and is drawn at the display's rate.**
Direct3D 8 is translated to 9 inside the patch, so the video features and the F11 menu work.

- **Prediction** (`predict=1`, default). Interpolating between the last two 60 Hz states would
  show the game up to a frame late. Instead, bullets, enemies and the stage are extrapolated
  along their last step, and the player is drawn from the keys held right now. A sprite that
  turns sharply is off for one frame. Menus and the HUD are interpolated. `predict=0`
  interpolates everything.
- **Game state is untouched**, verified frame by frame against the unpatched game. Replays work
  in both directions.
- `subtick=1` (off): real sub-tick player movement. Not replay-safe; disabled during playback.
- `substep=1` (off, experimental): sub-stepped bullets, lasers and items with collision at every
  step. Not replay-safe; disabled during playback.
  [Measurements](docs/games/TH08_DEVNOTES.md#9-sub-stepped-projectiles-fixed60-substep1-off-by-default-experimental).
- Only the Japanese `th08.exe` v1.00d; for English, use [thcrap](docs/OTHER_MODS.md#thcrap), which
  works. Untested: thprac, texture upscaling, `d3d9ex`.

## New Classic

A 64-bit Direct3D 11 remake with its own backend and fewer features:

- The simulation stays at 60 Hz; sprites are interpolated to the display rate, menus included.
- Dimming works. Scaling, filters, internal resolution and window management do not; use the
  game's own display settings.
- `subtick=1` (off): input polled and the player moved every drawn frame.
- `substep=1` (off): enemy bullets and lasers sub-stepped, with culling, grazing and collision
  at every step. Enemies, player shots, items and scripts stay at 60 Hz
  ([why](docs/games/TH06NC_DEVNOTES.md#16-systems-left-at-60-hz)).
- **Neither setting is replay-safe, and neither turns itself off.** Disable both before
  recording or watching a replay.

[Feature comparison with TH10–13](docs/games/TH06NC_VS_TH10_13.md).

## Troubleshooting

If the game does not start, or starts unpatched: check for an unsupported executable version,
another `dinput8.dll`, or a second patch. Test with only this patch installed and read
`touhou_hfr.log`, which says what was recognised or why the patch declined.

On-screen messages: a **conflict** message means another patch (such as vpatch) owns the frame
loop — remove it. A **wrapper** warning means a Direct3D 9 wrapper is presenting the game, so
scaling and borderless fullscreen are unavailable; filters still work.

## For developers

[ARCHITECTURE.md](ARCHITECTURE.md) is the source map; [ADDING_A_GAME.md](ADDING_A_GAME.md) is
the porting procedure; [docs/](docs/README.md) has everything else, including
[building](docs/BUILDING.md), [testing](docs/TESTING.md) and
[what differs per game](docs/GAME_DIFFERENCES.md).

## Credits

Developed by Vittorio Romeo with AI assistance: OpenAI's ChatGPT for the original TH12 patch
and the shared runtime, Anthropic's Claude from the scaler onward. Commits name their co-author.

| Component | Author | Licence | Used for |
| --- | --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.91.8 | Omar Cornut | MIT | the F11 menu (`third_party/imgui`) |
| [MinHook](https://github.com/TsudaKageyu/minhook) | Tsuda Kageyu | BSD-2-Clause | function hooks in the x64 runtime (`third_party/minhook`) |
| [d3d8to9](https://github.com/crosire/d3d8to9) | Patrick Mours | BSD-2-Clause | Direct3D 8 to 9 translation for TH08 (`third_party/d3d8to9`) |
| MMPX | Morgan McGuire and Mara Gagiu; slang port by hunterk | MIT | `shaders/mmpx.hlsl` |
| xBR-lv2, Super-xBR | Hyllian | MIT | `shaders/xbr-lv2.hlsl`, `shaders/super-xbr.hlsl` |
| ScaleFX | Sp00kyFox | MIT | `shaders/scalefx.hlsl` |
| FidelityFX CAS | Advanced Micro Devices | MIT | `shaders/cas.hlsl` |


Tools, references and shader sources: [docs/CREDITS.md](docs/CREDITS.md).

Touhou Project is © Team Shanghai Alice / ZUN. This mod contains no game files and patches the
games only in memory.
