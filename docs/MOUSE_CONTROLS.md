# New Classic mouse controls

Experimental, opt-in mouse controls for `th06nc_0914_game` only. The existing
full-executable SHA256 check gates installation; the original New Classic profile
and other games do not expose these controls. Start the game through Steam with
the existing x64 proxy/runtime installation.

## Controls

Open F11 > Mouse and enable Mouse control, or press F10. Left click fires,
right click focuses, and XBUTTON1 bombs. The menu can select XBUTTON2 instead.
Use Save to touhou_hfr.ini to retain preferences. Mouse control starts disabled
unless enabled in the INI.

- **Direct movement:** with its speed limit off, applies displacement at presentation rate and keeps
  the pointer and crosshair on the live character position. Right click halves
  sensitivity. The player sprite bypasses historical interpolation; other sprites
  retain the existing interpolation setting.
- **Limit to normal character speed:** optionally caps direct displacement using
  live normal/focused speed and axis scale. Mouse reports are collected between
  native updates, but the live player position changes only on a native 60 Hz
  update, with at most one capped step per native tick. Diagonal displacement
  shares the same total speed budget. All excess travel is discarded after that
  step. Game speed follows the native update schedule. A pause, keyboard takeover
  or lost focus clears unconsumed movement; stalls cannot bank extra steps.
- With Direct movement off, the character follows the cursor using ordinary
  native direction input. The closest legal step, including staying still, is
  selected at each 60 Hz input poll. The crosshair marks the target in this mode.

Keyboard/controller directions take precedence. Mouse input suspends outside
the client area, on lost foreground focus, in the HFR menu, on Escape and when
the native movement/projectile callbacks were not active. Buttons held across
these transitions must be released before they fire again. Direct mode reanchors
across transitions, avoiding a jump when control resumes. F10 disables control.

## Limits

Direct mode writes the live player position; its optional speed cap does not
make it replay compatible. Native replay files contain no record of those
positions. Disable mouse control before replay playback. Replay playback is not
automatically detected by this implementation. Cursor-following replay fidelity
has not been verified either.

Collision, scripts and other native gameplay still run at 60 Hz unless the
separate HFR projectile sub-step setting is enabled. Very fast direct movement
does not introduce swept collision detection. The existing player sub-tick
movement feature stands aside while mouse control is enabled, preventing double
movement. The feature is intended for maintainer evaluation and playtesting.

## Implementation and verification

`mouse_nc.c` wraps the native input poll, retaining native input and button-edge
handling. Additional presentation frames sample mouse movement without calling
the native poll or mutating the game's input history. Limited mode stores only
the net displacement for the next native update; presentation-only samples do
not write the live player position. `mouse_follow.h` holds the
native-step target selection, client/backbuffer conversion and button latches.

The verified renderer adds the live playfield origin at RVA `0x53cac0`, scaling
both axes by render height / 480. Render dimensions live at `0xc6e064/0xc6e068`.
The draw callback at `0x6ba5a..0x6ba98` feeds player VM `player+0x78c8` directly
from `pl_position`; only that VM bypasses smoothing in direct mode. The existing
profile supplies bounds, position, speed and scale fields. Native movement and
projectile activity gates were checked against a live paused stage.

`tools/test_mouse.c` includes the actual runtime with deterministic device/time
samples and runs from `test64.ps1` / `test64.sh`. Coverage includes buttons and
bomb edges, gates, keyboard priority, F10 debounce, viewport scaling, 320 follow
targets, direct displacement, focus sensitivity, wall reversal, reanchoring,
256 keyboard/mouse parity comparisons across 60/144/360/1000 Hz displays and
60/125/500/1000 Hz devices, two character speeds, focus, 50/100 percent game
speed, no position writes on presentation-only frames, and transition/stall/
duplicate-poll limits. Unconsumed input and excess travel cannot produce a
sequence of catch-up steps. It also
installs and removes the poll detour on an inert copy of the supplied executable;
only the test replacement is executed, never original game code.

Build with `build64.ps1 -Compiler <MinGW-gcc.exe>` and run
`test64.ps1 -GameExe <th06nc.exe> -Compiler <MinGW-gcc.exe> -Python <python.exe>`.
The original test dependencies and launcher requirements still apply. No game
executable or assets are included. The automated harness does not substitute for
live gameplay testing of collision, replay and focus/respawn edge cases.
