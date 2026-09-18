**Touhou 14 — Double Dealing Character now runs at your display's refresh rate.** It is the first
release to cover TH14's gameplay rather than only its picture, and it is the reason for the
version. TH10–13 and New Classic are untouched: no scheduler, scaler, renderer or input code
changed for them, and a replay recorded on v0.5.4-test behaves the same here.

## TH14 gameplay runs at the display's rate

Everything you aim with, dodge and shoot now moves between 60 Hz frames:

- **Bullets** — motion, off-screen culling, grazing and player collision.
- **The player** — movement, with the fixed-point truncation carried across sub-steps, so a slow
  focused drift is not rounded away six times a frame. The invincibility blink, the shot rates
  and the weapon timers ask their questions once a frame, as they did at 60 Hz.
- **Items** — falling, collection and the auto-collect line.
- **Lasers** — all four classes, including the widths that are interpolated from a timer.
- **Enemies and the player's options** are drawn between their 60 Hz positions rather than
  stepped. Enemy logic is a script interpreter; running it faster would be a different game.
  The options chase the player by a proportion of the remaining distance each frame, so
  sub-stepping them would change how far they trail.

TH14 keeps its timers differently from TH10–13 — each one carries a pointer to the game speed and
scales itself — which is why this needed twelve hooks where TH13 needed three hundred lines. What
a rate pointer cannot fix is a counter the update moves itself, a block gated on a timer's integer
*being* a particular value, and a truncation; those are what the hooks are for.

## Replays on TH14 are not rate-aware yet

TH10–13 stamp the recording's tick rate into the replay and play it back at that rate. TH14 does
not yet, so a TH14 replay plays back at 60 Hz whatever it was recorded at. (New Classic has no
replay extension either; its own warning about sub-stepping and replays is in the README, and is
unchanged.)

This is not cosmetic and it is worth being plain about. A run recorded with sub-stepping on and
watched back at 60 Hz can diverge, and in a traced case the player died at a frame she survived
in the recording. The cause is measured rather than guessed: a bullet's delay timer starts a
frame early when the update runs six times a frame, which is enough to change a pattern. Enemy
behaviour, script execution and everything drawing from the random number generator are
bit-identical across rates — 667 frames compared field by field — so this is confined to bullets.

Nothing is corrupted and recording works normally. If a particular run matters to you, play it
with `substep=0`.

## Also in this release

- **The runtime reports its own version correctly.** The loading line in the log was hard-coded
  and had been left behind at v0.5.3-test through the v0.5.4-test release. The version now comes
  from one place, and the F11 menu's title carries it too, so a screenshot identifies the build.
- **The crash reporter no longer runs out of room before the game starts.** It kept a budget of
  four reports; four harmless first-chance exceptions from the system libraries used it up before
  the title screen, so a real crash later produced nothing. It now keeps a slot per distinct
  faulting address.
- **Two debugging aids**, both off by default and only active with `debug=1`: `replay_trace`
  writes one line a frame with the player's position and a fingerprint of the bullets and
  enemies, and `replay_trace_from`/`replay_trace_to` add a per-bullet dump between two frames.
  They are what found the replay divergence above and they are documented in `touhou_hfr.ini`.
- **Research, not a change**: `docs/FIXED_STEP_RESEARCH.md` and `tools/research/` audit what it
  would take to run the simulation on a fixed rate independent of the display. Nothing in the
  runtime changed for it; the probe is worth keeping because it prints the actual integrated
  step for each rate from the production scheduler code.

## Compatibility

TH10 v1.00a, TH11 v1.00a, TH12 v1.00b and TH13 v1.00c, Japanese and English, Steam and
standalone, alongside thcrap, THRotator and thprac. TH14 v1.00b Japanese only so far — the
English and Steam executables have not been checked, and TH14 has not been tested with the other
patches. New Classic support is still experimental.

Scores set with this patch are not comparable to unmodified play, and sub-stepping can change
outcomes. See the README.
