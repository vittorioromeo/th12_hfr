**New Classic gets its sub-stepping right, and loads on Proton.** TH10–13 are untouched: no
scheduler, scaler, renderer or input code changed, and a replay recorded on v0.5.3-test behaves
the same here.

## New Classic: bullets that were still appearing flew too far

With **sub-stepped projectiles** on, a spread of bullets fanned out visibly wider as it spawned
than it does in the unmodified game.

A bullet does not start at full speed. For as long as its spawn animation plays it drifts
outward at a half, two fifths or a third of its velocity, and only then begins travelling
properly. Sub-stepping moved every bullet between ticks without asking which of those it was, so
a bullet that was still appearing got a whole extra frame of full-speed motion on top of the
fraction it was already moving — three to four times too fast, for the length of the animation.
Since every bullet of a spread appears at the same instant, what grew was the ring.

The same oversight ran the player collision and the graze test during those frames, which the
game never does at 60 Hz: a bullet could graze, or kill, while it was still appearing. Both are
now what the unmodified game does. Ordinary bullet flight is unchanged and still sub-stepped,
with motion, culling, collision and graze.

Separately, and also under sub-stepping: each bullet's animation script was being advanced once
per sub-step rather than once per tick, so at 360 Hz it ran six times per game frame. Scripts
that only choose a sprite were unaffected; scripts that move one — the cancel bursts — scattered
much further than they should. Those now advance once per tick and are interpolated for drawing,
so they are still smooth.

## New Classic on Proton

The patch could sit in the folder, correctly installed, and simply never start — no error, no
log, just an ordinary 60 Hz game. The game asks the system for its DXGI factory in a way that
resolved to the system library rather than to the proxy beside the executable, so the proxy's
own entry points were never the ones called.

The proxy now recognises that startup path as well as its own, and starts the runtime from
either. Nothing about the runtime, the fingerprint checks or the settings changed. The launch
option is still:

```
WINEDLLOVERRIDES="dxgi=n,b" %command%
```

## Also

- `CHANGELOG.md` now ships in the archive and is linked from the README.

## Compatibility

Unchanged from v0.5.3-test: TH10 v1.00a, TH11 v1.00a, TH12 v1.00b and TH13 v1.00c, Steam and
standalone alike, alongside thcrap, THRotator and thprac. New Classic support is still
experimental.

Scores set with this patch are not comparable to unmodified play, and sub-stepping can change
outcomes. See the README.
