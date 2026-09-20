# Replays

## TH10–14

A replay recorded with the patch has extra data appended after the game's own, untouched
payload: the logic rate, the per-tick input stream, the game and simulation revision, and the
gameplay settings. On playback the patch reads it and runs the simulation at the recorded rate,
so a run recorded at 360 Hz with sub-stepping is reproduced at 360 Hz. The format is in
[ARCHITECTURE.md](../ARCHITECTURE.md).

| Replay | Plays back |
| --- | --- |
| Recorded with this build | At its recorded rate |
| Recorded without the patch | At 60 Hz logic, presented at the display rate |
| Recorded with an older HFR build | With its rate and input, but the simulation revision is unknown, so it may desynchronise. A warning is shown when the metadata is not understood |
| Recorded with the patch, played in the unmodified game | Not guaranteed. Record with `substep=0` if this matters |

Sub-stepped and 60 Hz play are not the same simulation. Collision is tested at every tick, and
on TH14 bullet delay timers start a frame early under sub-stepping, which is enough to change a
pattern. Enemy logic, script execution and the random number generator are the same at any
rate. Keep the build you recorded with if a replay matters.

Locations: TH13 and TH14 keep replays under `%APPDATA%\ShanghaiAlice\th13\replay\` and
`...\th14\replay\`; the others keep them beside the executable.

Full-run parity between the stock game and the patched game has not been compared frame by
frame.

## TH08

Default settings do not change game state, so replays record and play back exactly as in the
unpatched game, in both directions. `[fixed60] subtick` and `substep` are not replay-safe; both
switch themselves off during playback, but a replay recorded with either on will not play back.

## New Classic

The replay format stores one input word per 60 Hz frame, so it cannot describe sub-tick
movement. `subtick` and `substep` are not replay-safe and do **not** switch themselves off:
disable both before recording or watching a replay.
