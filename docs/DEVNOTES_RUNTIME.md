# Developer notes: the shared runtime

What was learned building the video path (scaling, filters, the in-game menu) and taking the
patch from one game to three. Written for whoever picks this up next, including me.

The other documents, and why this is not one of them:

| | |
| --- | --- |
| `ARCHITECTURE.md` | where the code lives and how the pieces fit |
| `ADDING_A_GAME.md` | the procedure for a new game |
| `RESOLUTION.md` | how the scaling, filter and menu machinery works, in detail |
| [MOD_COMPATIBILITY.md](MOD_COMPATIBILITY.md) | THRotator/thprac source audit, confirmed hook conflicts, and proposed compatibility work |
| `DEVNOTES.md`, `TH11_DEVNOTES.md` | historical, from the original per-game builds. Left alone: they describe a build that no longer exists, and rewriting them would destroy the record of how the sub-stepping design was arrived at. |

This file is the findings — the reasoning, the wrong turns, and the things that will bite again.

---

## 1. What exists now

Four games are supported: TH10, TH11, TH12 and TH13. TH10 was the one that exercised the
multi-game design hardest, because its speed model is unlike the other two; §7 says how. TH13 is
TH12's engine with a handful of structural changes, each absorbed by a profile field; §7a says
which, and what the port taught about porting the per-object hooks.

Everything about the picture is one implementation shared by every game, and none of it uses a
game address: arbitrary window resizing with letterboxing, three scaling modes, borderless
fullscreen, a multi-pass filter chain with four bundled upscalers, a Dear ImGui menu covering
every setting, the screenshot fix, the crash reporter, and the guards against other patches.

The high frame rate is the opposite: almost all of it is per-game and needs addresses.

That split is the single most useful structural fact about this codebase, and it is worth
defending. It is why TH11 got the entire feature set with no code change, why a game whose engine
has not been worked out can still have all of the picture, and why the provisional state (§6)
exists — a profile can ship the shared half while its simulation half is still being worked out.

---

## 2. Direct3D 9 rules learned the hard way

**Never read from a surface that is currently bound as the render target.** `GetRenderTargetData`
and `StretchRect` both hang the device. This cost three attempts at the screenshot fix before
the pattern was visible: unbind, copy, rebind.

**`ps_3_0` cannot be used with the fixed-function vertex pipeline.** Filters are drawn through a
pass-through vertex shader with clip-space vertices; the built-in blits still use the
pre-transformed path.

**The device is never reset.** `FUN_00431700`, the game's pre-reset release, only releases three
pointers that are never assigned anywhere in the build, so the game reloads nothing after a
reset. With Direct3D 9Ex every texture is in `D3DPOOL_DEFAULT`, whose contents are undefined
after one. Presenting through an additional swap chain of our own avoids resets entirely. This
also means the game's own Alt+Enter has been broken since the 9Ex conversion, independently of
any of this work.

**Intermediates must carry alpha whatever the back buffer is.** The windowed back buffer is
`X8R8G8B8` — no alpha channel at all. ScaleFX's first pass writes an edge distance in each of
four components, so a quarter of its data was silently dropped and every later pass then chose
the wrong neighbour. On screen that is a dotted outline tracing every sprite, which reads as
"the filter is broken" rather than "the target has three channels".

**A chain that overshoots the window must be averaged down, not sampled.** A fixed-scale filter
magnifies by a whole number, usually more than the window asks for — ScaleFX's 3x image in a
1.5x window. One bilinear tap per destination pixel keeps two source pixels out of every three,
which produces the same dotted edge, this time from the resample. Four bilinear taps at the
quarter points of the destination pixel's footprint average it instead.

**Reset the texture-coordinate transform before any fixed-function draw of your own.** TH10's
stage renderer leaves `D3DTSS_TEXTURETRANSFORMFLAGS` enabled on stage 0 for its scrolling
cloud layers. The Dear ImGui backend resets a long list of states and not that one, so during
gameplay the menu's UVs went through the game's texture matrix and the whole overlay sampled
nothing — the menu was "open" by every measure the log had and simply did not appear, while at
the title screen it was fine. Found by bisecting a mask of state resets at run time rather
than by reading the game, which is the faster tool when the symptom is "draws nothing".

Those last two produced identical symptoms from unrelated causes, which is worth remembering:
a filter that is subtly wrong looks exactly like a filter that is completely wrong, so
"it renders something" is not evidence that a port is right.

---

## 3. Filters

The shader system compiles at run time with whatever `d3dx9` is available, so no bytecode is
shipped. A filter is a shared header plus `//! pass` blocks, each with its own `//! scale` and
optional `//! float`. Passes can read the previous pass, the game's original image, and any
earlier pass. The rules live in `src/core/shader_parse.h`, included by both the runtime and the
checking tool so the tool cannot drift from what the game accepts — it had already drifted once,
still validating against a two-sampler prologue that no longer existed.

### Which compiler

Not a matter of taste, and not a matter of version numbers either:

- TH10 ships `d3dx9_31` (2006), whose HLSL compiler rejects an early return inside an `if`
  (`error X3500: asymetric returns from if statements not yet implemented`, misspelling and all)
  and therefore cannot build MMPX or Super-xBR.
- From `d3dx9_42` onward, `D3DXCompileShader` forwards to a separate `D3DCompiler_NN.dll` that
  may not be installed, and then it fails too.

So the loader stops reasoning about versions and asks each candidate to compile a probe using
the features the bundled filters use, taking the first that passes. In a real TH10 run it
rejected 43, 42 and 41 and settled on 40.

### What is bundled, and what cannot be

All four bundled filters are MIT and were ported from upstream GLSL or Cg, never from Magpie —
Magpie is GPL-3.0 as a whole and its effect files carry no separate header, so its HLSL port of
an MIT shader is still GPL-3.0. That is the easiest way to contaminate a permissive project.

| Bundled | Licence | Shape |
| --- | --- | --- |
| MMPX | MIT | 1 pass, 2x |
| xBR-lv2 (Hyllian) | MIT | 1 pass, free scale |
| Super-xBR (Hyllian) | MIT | 3 passes, 2x |
| ScaleFX (Sp00kyFox) | MIT | 5 passes, 3x |

ScaleFX is the one actually designed for pixel art: its output contains only colours from the
original, so it cannot ring or halo on sprite edges.

| Not bundled | Why |
| --- | --- |
| xBRZ | GPLv3, including its shader ports. |
| hqx | LGPL-2.1+ in every implementation whose provenance can be traced. Two forks ship permissive licences (brunexgeek claims Apache-2.0, janert claims MIT) but each documents deriving from LGPL/GPL sources, so neither relicence looks like one its author had the right to make. |
| NNEDI3 | GPL/LGPL down to its trained weights. |
| FSRCNNX | LGPL-3.0 shader, GPL-3.0 trainer. |
| Anime4K | MIT and legal, but its cheapest useful preset is ~25 passes (the limit is 8) and it is trained to repair compression-damaged 1080p anime video — close to the opposite of a 640x480 sprite with hard one-pixel edges. |

None of this stops anyone using them: a filter dropped into `shaders/` can be under any licence.
`shaders/README.md` carries the evidence so the next person does not have to establish it again.

---

### Post-processes, and the pointer

Sharpening is not a filter. A filter decides how the game's 640x480 pixels become the
window's; a sharpening pass decides what is done to the result, and it wants the result --
the resampled picture, at the window's size, softness and all -- as its input. So it is a
separate list (`g_posts`, files marked `//! post`), chosen separately in the menu, and it runs
in `scaler_blit` after the final draw: with one selected and its strength above zero, the
final draw goes into a window-sized intermediate in the back buffer's format (`g_post`) and
the pass draws that into the back buffer at the same rectangle; with none, the final draw
goes straight to the back buffer as before, and nothing else changes. Its strength arrives as
`Params.x` (`c3`, added to the prologue and zeroed for every filter pass). `cas.hlsl` is
AMD's CAS with the better-diagonals soft min/max, the strength driving AMD's own
`-1/lerp(8,5,s)` peak; `unsharp-mask.hlsl` is the textbook operator on luma, clamped to
±0.12, kept as the reference point. Both were checked in the rig on TH12 at 1600x1200:
CAS at 100% visibly tightens sprite edges without a halo; the intermediate costs one
window-sized draw.

The pointer: the game hides it whenever it believes it is fullscreen, by
`while (ShowCursor(FALSE) >= 0);` at the switch and `SetCursor(NULL)` on every
`WM_SETCURSOR` -- TH10-13 all do it the same way, through their import tables. Over the
borderless window it leaves the user with no pointer on what is, to them, a window. Both
imports are hooked (window.c): the game's calls only ever test the sign of the count, so
it is given a count kept in -1..0 and never told anything else, while what Windows sees is
decided once a frame by `cursor_want_visible` -- the menu open wins, then the game's own
wish in a real window, then `cursor=` for borderless (hidden / visible / visible for two
seconds after the last mouse message). `SetCursor(NULL)` becomes the arrow whenever the
pointer is meant to be visible, since a null cursor image would hide it regardless of the
count. ShowCursor's count is per thread, so every real call is made on the game's thread:
the hooks run there, and so does `window_pump`.

## 3a. Internal resolution, and where the whole pixels came from

The complaint that led here: at 360 Hz a slow bullet still stepped from pixel to pixel. The
simulation was never the limit — positions are floats, TH12/TH13 round `MotionState` positions
to 1/100 px — the rasteriser was: the game drew into a 640x480 target and we magnified the
result. Sub-pixel information existed in the data and was thrown away at rasterisation.

**`video.internal_scale = N`** hands the game a target N times its own size and scales what it
submits to that target (d3d9.c): viewports, clear rectangles, and every pre-transformed
`XYZRHW` vertex in `DrawPrimitiveUP`, keeping the D3D9 half-texel rule
(`x' = (x + 0.5)·N − 0.5`). Draws to any other target — the game's own render-to-texture
surfaces, our presentation chain — are untouched, which the `SetRenderTarget` hook decides.
Anything drawn through a real projection needs nothing. The screenshot path reduces the
big surface back to 640x480 first, because the game writes its BMP from the locked surface
with sizes from its own present parameters; and the engine's screen capture
(`D3DXLoadSurfaceFromSurface` with a 640x480-coordinate source rect: the pause backdrop,
spell backgrounds) has its source rectangle scaled, or it shows the top-left quarter.

**That alone only sharpens.** With the target at 2× and the vertices scaled, the vertex stream
still carried no fractions other than 0 and ½ — every 2D vertex sat on a whole 640x480 pixel.
The ANM quad builder (`0x467350` in TH13) rounds the four corners with `frndint` before the
half-texel offset whenever the VM's flag bit 0 is set, which is nearly every sprite; the other
draw modes (rotated, 3D) never round. Four two-byte NOPs, kept as frozen `sprite_round_sites`
in the profile and applied only when `internal_scale > 1`, and the stream carried 0.85, 0.52,
0.30 … during gameplay. The game's sampler is bilinear, so a bullet at x = 100.3 now really
sits between pixels. Games without known sites get the sharper raster and the log says so.

**How it was verified, since screenshots cannot show it.** The rig presents at ~25 fps, so two
captures are many frames apart; instead the `DrawPrimitiveUP` hook counted vertices off the
pixel grid and kept the set of distinct fractions seen, before and after the NOPs. Counting
inside the hook is the right instrument for a question about what the game submits.

**What it changes elsewhere.** The upscaling filters now see an N× source: they still run, but
with sprites already magnified by the game's bilinear sampler there is little for MMPX or xBR
to do, and "nearest" or "bilinear" is the natural pairing. "Pixel perfect" is relative to the
N× surface. The device is created at N× once; changing it needs a restart, so it is an INI
setting and not a menu item. The obvious next step is the one this makes attractive: apply
the pixel-art upscalers to the *textures* at load time (the `D3DXCreateTextureFromFileInMemoryEx`
hook already exists), which with N× rasterisation and sub-pixel placement is an HD mode rather
than a smoothing filter.

**Texture upscaling, the other half** (`video.texture_scale = N`, `texture_filter`;
`src/core/texscale.c`). Every texture the game creates goes through the device's
`CreateTexture` — D3DX's loaders included — so that hook registers them. On a texture's first
`SetTexture` a render-target copy N times its size is made and the pixel-art filter runs into
it on the GPU; the bind then substitutes the copy, and because UVs are relative the game is
none the wiser. The bundled filters write alpha = 1 and reason about colour only, and a
sprite sheet is mostly alpha, so each texture is run twice: once as colour premultiplied by
alpha (transparent texels read as black rather than whatever the sheet left there), once as
alpha spread to grey; a final pass divides one by the other and writes the alpha. The
silhouette thereby gets the same treatment as the colours. Textures the game rewrites — the
stage title, dialogue text (GDI into a DIB, then `D3DXLoadSurfaceFromMemory`) — are marked
stale by the surface-load and `LockRect` hooks and redone on their next bind; the class
`Release` hook drops the copy with the texture. Excluded: render targets, mipmapped and
oversize textures, and everything past a 512 MB budget. Our own draws (the presentation blit,
the menu) run with substitution off. Verified in the rig at 2×+2× with xBR-lv2: title art,
gameplay, stage title, trance background, HUD, all clean, ~17 MB of copies in stage 1.

**The one mistake, recorded because it was cheap to make and expensive to see.** Turning the
experiment into the feature, a text splice dropped the three D3D9Ex managed-pool hooks that sat
next to it; every managed vertex buffer creation then failed and the game crashed at startup in
its supervisor init. It presented as "the feature crashes after a screenshot" for an hour
because the crash coincided with a key press. `git diff` on the file, not the log, found it.

---

## 3b. Dimming the background and the pickups, and what draws what

> **The census, added while porting TH14.** The per-frame trace below samples one frame every
> ten seconds, which is fine for the things that are always on screen and useless for the ones
> that are not. An item, a bomb, a death can be happening constantly and still never land in a
> traced frame -- TH14's items were missed twice that way. So with `debug` on, every VM draw of
> every frame now also feeds a `(priority, ANM, layer)` census, reported on the stats line like
> the update-node one, with how the current rules classify each row:
>
> ```text
> draw census: priority 35, bullet.anm layer 0, 41022 draws -- not faded
> ```
>
> That is the table the rules are written from, and it makes "what draws the items?" a question
> the log answers rather than one the trace has to be lucky enough to catch.
>
> **And a callback that draws no VMs at all still draws.** The VM census answers "what sprite
> was that?", and is blind to a manager that builds its own quads -- which is what TH13's items
> do, and the reason its item rule matches on priority with a NULL ANM. So a second row type
> counts, per draw callback, how many draw calls it made and whether any VM was behind them:
>
> ```text
> draw census: priority 65, (no VM: its own quads) layer -1, 8100 draws -- not faded
> ```
>
> Without that, "what draws the items?" has no answer in the log however long the game is
> played, which two stages' worth of looking established the hard way.
>
> **The name is copied, not pointed at.** The first version kept the `const char*` the VM handed
> it and compared pointers. An ANM record is freed and its memory reused, so by the time the
> line printed, the name was whatever had been written there since; every reallocation of the
> same file became another row; and the fixed table filled with rubbish before the short-lived
> thing being looked for ever reached it. A census that hides the answer it was added to find is
> worse than no census, because it looks like evidence.

The request was simple — fade the stage towards black and the P/point items towards
transparent so bullets stand out — and the first two attempts were wrong in instructive ways.

**Attempt one: attribute by sprite layer.** The ANM manager draws its layers through one
function with the layer number in EAX (TH13: `0x46f380`), and thanm's listings say which
layer each script uses (bullets 15, items 10, enemies 8, player 11). Wrapping that function to
record the layer and reading it in the `DrawPrimitiveUP` hook seemed enough. The trace showed
otherwise: there were *no* draws on layers 8, 10 or 15, ever. The managers that own those
objects — EnemyManager, ItemManager, BulletManager, the player — draw their VMs themselves
from their own draw callbacks (24 callers of the VM draw `0x46a700`); the layer lists only
carry free-standing effects and interface pieces. The layer number is a property of the
script, not of who draws it.

**Attempt two: attribute by draw call.** The next trace was stranger: no draw carried the
items' texture at the moment the items were drawn, and the player's draw showed 34 primitives
in one call. The sprite manager *batches*: consecutive 2D sprites with the same texture and
blend accumulate and are flushed as one `DrawPrimitiveUP` when either changes, or when
someone asks (`0x4679a0` in TH13, `0x442f50` TH10, `0x44fd10` TH11, `0x45a3c0` TH12 — all
`ESI` = the ANM manager, whose pointer the profile already has). So the items' quads are
typically flushed by the first sprite of the *next* callback. A draw call, on its own, says
nothing about which object it belongs to.

**What works: attribute by draw callback, and make the batches honest.** Every object
registers a draw callback with a priority; the draw runner walks them in order (TH13
`0x470c30`, list at manager+0x40, dispatch `mov ecx,[esi+0x24]; mov edx,[esi+8]; call edx`).
The profile names that dispatch and `dimming.c` wraps it: flush the batch, record the node's
priority in `g_draw_prio`, call the callback, flush again, forget the priority. Every draw
call now happens under exactly one callback and the hooks can attribute it. The two extra
flushes per callback are no-ops when nothing is pending, which is nearly always.

**Where the background ends.** TH11 on render the stage into an offscreen target, switch to a
second one for the world, copy the first into it, then copy back and forth for effects and
finally onto the back buffer — TH13's trance re-blends the stage texture over the world with
`DESTCOLOR/INVDESTCOLOR`. Dimming "at the first world draw" therefore lost to the copy that
followed it. The quad is instead drawn *before the first callback with priority >=
world_prio*, over the current viewport, which at that moment is the finished stage in
whichever target holds it; every later copy carries the dim. `world_prio` is the callback
that switches targets (TH11 11, TH12 12, TH13 12) or, on TH10 which draws straight into the
back buffer, the first callback after the Stage's 2D pass (11); there the viewport is the
playfield and the interface is painted around it afterwards. The trance overlay in TH13 is
only partly dimmed (its blend brightens towards the texture); rare and short, left alone.

**The items** are the ItemManager's callback (TH10 25, TH11 25, TH12 27, TH13 26): its
draws have their vertex alpha scaled in the copy the internal-resolution path already makes,
or their colour when the destination blend is ONE. Draws that lack a diffuse component are
left alone; none of the four games' item draws do.

**Finer than a callback: the sprite VM.** The next request — fade effects, the player's
shots, TH13's spirits — broke the callback as the unit: the sprite-layer callbacks draw the
player's shots, the spirits, the bullet cancels and the hit sparks from one list, and TH11
even draws the enemies through it. So the sprite VM draw is wrapped as well (TH13 `0x46a700`,
TH12 `0x45c900`, TH11 `0x451ef0`, TH10 `0x4451c0`; VM in EAX, six carried bytes). Before a VM
draws, C classifies it and, when its class differs from the batch's, the batch is flushed
first; the class then rides with the batch to its draw call. What identifies a VM: a pointer
to its *loaded ANM* — a struct that begins with the slot index and the file name, the same in
all four games — and its sprite layer. Dumping the first 300 words of a few VMs per callback
under `debug=1` (the trace still does it) found the pointer at +0x30 (TH13), +0x3f8 (TH12),
+0x3b0 (TH11), +0x308 (TH10) by looking for words that point at "n.anm", and the layer at
+0x24 / +0x20 by matching the layer-thunk numbers. A profile's rules are then readable:
`pl*.anm` on layers 10..13 are the player's shots (the body scripts set no layer, in every
game and every character), `astral.anm` is TH13's spirits, `effect.anm` the effects,
`bullet.anm` on anything but its item and bullet layers the bullet cancels, `enemy.anm`
never touched; and the manager callbacks (lasers, bullets) are excluded by priority first.
The sprite id is also there (TH13/TH11 keep `slot << 16 | sprite` next to the pointer, TH12
a plain id at +0x3e0) but no rule has needed it. The cost is two words stored and a table
walk per VM draw, and a flush where classes alternate — a few per frame.

**Why a classified VM is flushed on its own.** The first version set the class at VM entry
and let the batch run on. Field reports showed boss sprites and spell-card portraits fading
"in certain animations": quads from paths the wrap does not see — a manager building quads
itself (TH13's Effects manager calls the quad builder directly), a VM drawn through another
entry — landed in a batch whose class an earlier effect VM had set. So the VM draw is now
wrapped at both ends (the entry stub swaps the caller's return address for an exit stub and
keeps the real one on a small stack, since children re-enter the draw): a classified VM's
quads are flushed before it if something else is pending and after it always, so no faded
draw call ever carries a stranger's quads. Unclassified VMs batch as before; the cost is one
draw call per faded sprite, which is what 3D-mode sprites cost the game anyway.

**The two ways a sprite gets its colour.** 2D-mode sprites carry it in the vertices, so the
copy the internal-resolution path already makes is faded there. 3D-mode sprites (`ins_302(1)`;
TH13's petals, enemy deaths in TH10-12) are drawn from a static unit-quad vertex buffer with
the colour in `D3DRS_TEXTUREFACTOR`; the `DrawPrimitive` hook fades that and puts it back.
Missing the second path was why "the death explosions do not fade" — nothing to do with the
rules. The background class fades colour rather than alpha in both paths: TH10 draws some
spell backgrounds as sprites *above* the world (stage 2's, on layers 4-5 → priority 16-17), and
a rule can now name them `DIM_BACKGROUND` so they darken like the rest.

**Where the deaths are — found by watching, not reading.** TH13 keeps the enemy-death
bursts in `effect.anm`. For TH10-12 two readings of the ANM listings were wrong in turn: the
sixteen one-sprite additive scripts on `enemy.anm`'s lowest layer (TH10 4, TH11 6, TH12 7)
are spawn-in flashes and auras (effects, so the rule stays right by accident), and the small
circles of `bullet.anm` script 164 never draw at all. The bursts the reports meant — TH12's
coloured disc with a rotating ring, TH10/11's rings and sparks — are `bullet.anm` scripts
that set the *bullet* layer themselves (TH12 78-152 `ins_68(16)`, TH11 75-188 layer 15,
TH10 353-442 layer 13; the two scripts before each range are the hitbox, see below) and draw under the bullet-layer callback (TH12 34, TH11 32, TH10 33).
That layer had been excluded on the assumption it held the bullets; it does not: the
BulletManager draws bullets from its own callback (TH10/11 29, TH12 31) with the VM's layer
left at 0. Dropping the exclusion was the whole fix. What found it: `debug=2` (a draw table
every half second) over the title demo, which kills things on its own, and a burst of `xwd`
screenshots (an `import` takes two seconds, `xwd -root` 90 ms) to see the burst; the
per-quad `uv` in the table then names the sprite, and thanm names the script.

**Script ranges.** The only layer a game really shares between classes so far is TH12's
`enemy.anm` layer 7, where the UFOs (scripts 135-138, enemies the player shoots) sit among the
spawn flashes. Rules therefore carry an optional script range, read from the VM's 16-bit
script index (TH10 +0x38a, TH11 +0x3a2, TH12 +0x3ea, TH13 +0x4aa; found at the VM-init
function, the one that stores the loaded-ANM pointer: `mov word [vm+X], bx` with bx the
script parameter, next to `mov word [vm+X-4], cx` for the slot). The trace prints
`anm:layer/script` per VM so a rule can be written from the table without a listing.

**The hitbox lives on the death layer.** The first two scripts of each of those death
ranges are not deaths: TH10 351-352, TH11 73-74 and TH12 76-77 are the player's hitbox, the
two sprites that turn about the player while focused, drawn on the same bullet layer by the
same callback -- so dropping the layer's exclusion faded the hitbox with the effects on all
three games (TH13 keeps its hitbox in `pl*.anm` layer 12 and already had a rule). Each now
has a script-range `DIM_NONE` rule ahead of the `bullet.anm` catch-all. Found the same way:
a `debug=2` run with the focus key held for eight seconds (the log line count before and
after marks the window), and the VMs present in every focused table and no unfocused one.
The pair is unmistakable once seen: one draw each per frame, `v0` orbiting the player's
centre at a radius of 45 (a rotating 64-pixel square's corner) while the body's `v0` stays
put. The death scripts proper begin right after (TH10 353/355, TH11 75/77, TH12 78).

**Known imprecision.** The options orbit the player on the shot layer, so they fade with the
shots. TH13's trance overlay brightens the dimmed stage back towards its texture (its blend
is DESTCOLOR/INVDESTCOLOR); rare and short, left alone.

**Frame-time diagnostics.** Two lines accompany the five-second stats when a frame ran long:
`hitch:` for any gap over 40 ms between presents, with that frame's draw calls, forced batch
flushes, sprite VM draws and texture upscales; and `frame time:` with the longest time spent
*inside* the game's frame function (its draw and the present, vsync included) and *outside*
it (its loop and its own waits), each with a count of frames over 8 ms, a span breakdown
(before the first draw, drawing, in Present, after Present) and a histogram of the gaps
between presents. `joystick:` counts the real `joyGetPosEx` calls made on our thread and the
longest of them (§4, "the joystick that enumerated"). Under `debug=1` there is more: a `long
frame:` line for each of the first sixty frames over 12 ms with its span breakdown; once,
`clock:`, `process:` (compat layer, version lie, non-Windows modules) and `monitors:` (the
window's placement, styles and every monitor's rate); for the first few windows `threads:`
(affinity, priorities, per-thread CPU), `focus:` (the foreground window and EcoQoS throttling)
and `screen:` (DWM composition rate, dropped frames, the chain's present statistics); and a
1 kHz sampling profile of the main thread (`sampler.c`: modules by share, and the game-side
return addresses seen while it sat in the system). Everything under `debug` is a probe that
was written for one hunt and kept because it is cheap to keep; none of it runs by default.

Read these lines with §4's "the clock that lied" in mind: before the mod's arithmetic was
moved to SSE, every TH10-12 log showed exactly 64 long gaps a second and a histogram sitting
on 15.6 ms, and that was the measurement, not the game. TH10's real hitches (a brief `0.0fps`
reading every few seconds) were its native FPS watchdog: rates over 65 FPS are taken for a
broken clock, the fourth reading zeroes the QPC frequency, and the retained deadline loop
then runs through ~14 M iterations of catch-up (28 ms). The adapter bypasses that recovery
branch; TH10_DEVNOTES §6b has the code, the TH13 comparison and the regression fixture.

**Debug levels.** `debug=1`: state dumps, a draw table every ten seconds of a stage with VM
word dumps. `debug=2`: the table every half second, no word dumps. `debug=3`: every other
frame for the first minute, the log flushed once per frame instead of per line (per-line
flushing under Wine halved the frame rate), and the first vertex of every batched draw.

## 4. Bugs met, and what they taught

### The clock that lied

Every log from TH10-12 at 360 Hz showed the same thing: 64 long gaps a second, a gap
histogram sitting on 15.6 ms, presents arriving in bursts. TH13 showed none of it. That is the
Windows timer's rhythm, so the hunt went where such a rhythm sends it — the compositor
releasing frames in lumps, the driver queueing frames ahead, EcoQoS, timer resolution, the
window style — and a week of probes, an event-query GPU sync, a no-vsync software pacer and
`flipex` all made play worse without changing the pattern (the user's verdict: "much worse").

The pattern was ours. Direct3D 9 created without `D3DCREATE_FPU_PRESERVE` puts the calling
thread's x87 control word into 24-bit precision and leaves it there. TH13 sets 53-bit back
in-game; TH10-12 never do. The mod's clock is `QueryPerformanceCounter / frequency` in a
`double`; the `fild` loads the counter exactly, but the `fdiv` that follows rounds its result
to 24 significant bits, so the seconds since boot came back in steps of (uptime / 2^24): about
5 ms after a day of uptime, 15.6 ms after three. Every `now_s()` was being rounded to a grid
of milliseconds, in the game's thread, by the game's FPU setting. The "stalls" were one step
of that grid, the "bursts" were several frames rounding to the same value, and the limiter's
deadline arithmetic, running on the same rounded clock, was doing exactly what a rounded
clock tells it to. What gave it away was measuring the same 2 ms `NtDelayExecution` three ways: QPC in
our double said 15.6 ms, the kernel's interrupt time said 2.0, `timeGetTime` said 2 — two
clocks that agreed with each other and disagreed with ours.

The fix is a compiler flag, `-msse2 -mfpmath=sse` for every C and C++ unit (build.sh and
build.ps1): SSE arithmetic does not read the x87 control word. The one place the C runtime
still formats through x87, `vfprintf`, is wrapped in `logf_` with `_controlfp(_PC_53)` and
restored after, so logged numbers are also right. The rule for everything that lives on the
game's thread: assume the FPU is in the state the game wants, never the state you want, and
never trust a double computed there unless the arithmetic is SSE. A log full of 15.6 ms
multiples is a clock reading in 24-bit precision before it is a stuttering game.

### The joystick that enumerated

TH10-12 call `joyGetPosEx` from the frame function before the game logic. With no controller
attached, winmm's answer is not a cached "none": it re-enumerates HID devices, on the calling
thread, and does so every second or so — 8 to 30 ms each time. At 60 Hz that hides inside a
frame; at 360 Hz it is a hole of ten frames a few times a second. The call is now made
continuously on a thread of our own (500 Hz with a controller, every quarter second
without), and the game's call is answered from the latest reading under a critical section,
with the caller's `dwSize`/`dwFlags` put back. The hook is installed for every game whether
or not sub-tick input is on, because the stall is the same either way; the `joystick:` stats
line shows how long the real calls take, off the game's thread.

### The menu key that stopped working

A single press was announced twice — once by the window's key messages, once by the runtime's
own poll of the key. The two announcements did not always land in the same frame, and when they
did not, the two toggles cancelled. Eight scripted taps produced four toggles.

Both routes now report whether the key is **down**, and one function turns that into a press.
Levels cannot disagree about how many presses happened, so the failure is gone by construction
rather than by bookkeeping that had already been got wrong twice. Three ways it could still
wedge are closed: a KEYUP lost when the window loses focus mid-press, a message-route "down"
that outlives the hardware, and a press that begins and ends between two polls.

### The menu that could not be brought back

Reported twice; the second report had the detail that mattered — the menu was open when the game
changed resolution.

The window was keeping a position from a viewport that no longer existed. Open centred on a
1817x1156 surface it sits near (900, 580); the game switches to 640x480 and that is off the
edge. ImGui clamps windows only enough to keep the title bar reachable, which still leaves
nearly all of a 620-wide window outside a 640-wide surface.

**The instructive half is why reopening did not rescue it.** The placement used
`ImGuiCond_Appearing`, chosen precisely so that every reopen would put the window somewhere
visible. It never fires: while the menu is hidden the render function returns before
`ImGui::NewFrame()`, so ImGui's frame counter does not advance, and "appearing" means not
submitted for at least two frames. Frozen counter, so never new.

A condition that read exactly like the fix was doing nothing at all, and nothing failed to say
so. That is why the bug survived a fix written specifically for it. The menu now tracks its own
placement rather than asking ImGui to.

### The features that "did not work" on TH10

Everything installed, the log said so, and none of the scaling or filters made any visible
difference. The window was 640x480. TH10's own dialog offers nothing larger — TH11 and TH12
offer 960x720 and 1280x960, which is why their users had never met this — and at exactly the
game's size every scaling mode and every upscaler produce the same 1:1 picture. There was
nothing to scale, and nothing said so. `window_scale` now sizes the window at startup (and
from the menu), and the INI comment explains why it exists.

### Two positional lists that shifted underneath

The config defaults were a bare list of eighteen values that had to stay in the same order as
the struct, so inserting a field anywhere but the end silently shifted every default after it.
Inserting TH10 at the front of the identity table silently repointed TH11's and TH12's profiles
at their neighbours' identities. Both are named now (designated initialisers, and named enum
slots). Same trap, twice in one day, and both were quiet.

### Dead code that duplicated live code

Binding an intermediate and preparing the pipeline existed twice, in `run_prepass` and inside
`run_chain` — and `run_prepass` still carried an entire shader branch that nothing had called
since chains replaced the single-pass filter path. Dead code duplicating live code is the kind
that drifts silently, because nothing fails when only one of the two gets corrected.

---

## 5. Coexisting with other patches

### vpatch (VsyncPatch, swmpLV/75E)

Not a wrapper. `vpatch.exe` is a 46 KB stub that finds `*vpatch*.dll`, and the per-game DLL
launches the game suspended and injects itself with `CreateRemoteThread` + `LoadLibrary`. Its
imports are `KERNEL32`, `USER32`, `SHLWAPI` — no `d3d9`, no `dinput8`. It patches game code in
memory at hardcoded addresses and calls Direct3D through the game's own device global.

It replaces the frame limiter and calls `Present` on its own schedule. So do we. Its sites do
not overlap ours — all 69 frozen signatures checked against all 16 of its TH12 addresses — so
the identity check cannot notice, both installs report success, and the game runs at whichever
scheduler got the last word.

**The load order is the opposite of what it looks like.** Injecting into a suspended process
seems certain to arrive first. It does not: the injected thread runs loader initialisation
before its `LoadLibrary` call, and loader initialisation is what loads this DLL, because the
game imports it. `WINEDEBUG=+loaddll` shows the game process loading `DINPUT8.dll` and only then
`vpatch_th12.dll`. At the moment we install, the game is still clean.

Hence two checks: refuse at install, which catches an executable already modified on disk; and
warn on the first frame, when it is too late to refuse. Detection is a loaded module named for a
known patch, plus the game's frame loop no longer being the code it shipped with — the second
does not care whose patch it is.

An install-time-only guard found nothing at all when launched through `vpatch.exe`, which is how
the ordering came to light. **A guard that never fires looks exactly like a guard with nothing
to find.**

Is vpatch still needed? Not for these games. Its live features are a frame limiter, its own
Present scheduling, window geometry and an input fix; the first three are this patch's territory
and it does them with a whole-number frame cap and a stretched 640x480 window. It does not
change the render resolution, whatever the guides say. Three things it does that we do not, so
they are not lost: `BugFixGetDeviceState` (a foreground check on the DirectInput path — the fix
for input running away after alt-tab; we call ZUN's poll routine rather than replacing it, so we
inherit stock behaviour), `BugFixTh12Shadow` (rev6-only, one render state, fixes UFO's Palanquin
Ship shadow on Radeon and Intel), and `ReplaySlowFPS` (slow-motion replay on Shift).

### OpenInputLagPatch (OILP)

The open replacement for vpatch (a `dinput8.dll` proxy plus `oilp_loader.exe`, per-game address
tables for TH6–TH18; the source is public and was used as an address cross-reference for the
TH13 port). Read for what it does, not what its name suggests:

- **Its own frame limiter in place of the game's**, with vpatch's central trick: wait *first*
  (waitable timer, then spin) until `BltPrepareTime` — 2 ms by default — before the 60 Hz
  deadline, and only then let the game poll input, update, draw and present. In the stock game
  the poll happens at the start of the frame and the limiter then idles for up to ~14 ms, so the
  sampled input is a frame stale by the time it is shown; OILP moves the poll to the end of that
  idle. It forces the game's "fast" input-latency mode and hooks that mode's per-frame call.
- **D3D9Ex** with `SetMaximumFrameLatency(1)` and `D3DPRESENT_INTERVAL_IMMEDIATE`.
- **Replay speed control** on held keys (skip at 240 fps, slow-motion at 30 fps, both
  configurable), a fullscreen refresh-rate choice, and a frame-time overlay.
- `GameFPS` above 60 speeds the game up. There is no sub-stepping and no interpolation.
- No alt-tab/foreground input fix — that one is vpatch's `BugFixGetDeviceState`, not OILP's.

**How it relates to us.** The D3D9Ex part and the disabling of the game's limiter and latency
`Sleep` are identical to ours (same three per-frame call sites, `frame_calls`). The late-poll
limiter is the interesting part, and it is exactly what our design makes unnecessary: OILP saves
up to a frame because at 60 Hz there is a long wait between poll and present; in our loop each
present slot is poll → tick → draw → Present with the only wait inside Present for the next
refresh, and movement/focus are polled again on every minor tick (`subtick_input_begin` calls
the game's own DirectInput routine, merges the movement and focus bits, and records them in the
replay's `HFRI` chunk). At 360 Hz movement input is at most one 2.8 ms slot stale, against
16–30 ms stock. Applying OILP's reserve inside our slot would gain 1–2 ms at the cost of a spin
and a real risk of missed refreshes whenever tick+draw overruns the reserve — less than the
jitter of a keyboard's own USB polling, so not done.

What is still frame-sampled, by design: shot, bomb and pause edges come from the game's own poll
on the boundary tick, once per 60 Hz frame, because the game counts those in frames and the
runner masks them on minor ticks so a bomb cannot fire twice. That is the same sampling rate
stock+OILP has, with a shorter path to the screen afterwards. Processing a bomb on a minor tick
would cut it further but changes game semantics (the deathbomb window is frame-counted) and
would diverge from stock replays; if ever done, it must be an option that the replay records.

Worth taking from this family, in order: vpatch's foreground check on the DirectInput path, and
OILP/vpatch's replay skip and slow-motion on held keys, which the tick-rate machinery makes
nearly free. Coexistence: OILP takes the same frame loop we do; the frame-loop guard in §5
treats it like vpatch (a modified loop is a modified loop, whoever did it), and
`openinputlagpatch` is on the known-module list. Note that OILP normally installs *as*
`dinput8.dll`, the same file name as our proxy, so the two cannot be dropped into one folder;
the module check only matters when it is loaded under its own name by `oilp_loader.exe`.

### d3d9 wrappers (PivotDX9 and friends)

Not a conflict. The patch installs, the game runs, the filters work — the wrapper only takes the
last step, placing the image in the window. That is why the scaling modes and borderless
fullscreen stop having any effect: the geometry is computed against the game's back buffer and
the wrapper then stretches that to the window however it likes.

We cannot fix it from inside, so we say so: three lines in the log, the two controls greyed out
in the menu with the reason above them, and a startup dialog — but only when a setting actually
in use depends on it, and `video.warn_wrapper=0` silences it for anyone keeping the wrapper on
purpose. Offering a scaling mode that silently does nothing is worse than not offering it.

Both notices share one piece of dialog machinery and show at most one box a run.

---

### 5c. Ending your own scene is someone else's compositor

A rotation wrapper such as THRotator is not a rival for the frame loop; it is a renderer. It
replaces `d3d9.dll`, answers `GetBackBuffer` with a target of its own so the game draws there,
and in `EndScene` composes that target onto the real back buffer — rotated, HUD rearranged —
before the game ever calls `Present`. That is the same trick this patch plays on the same
surface, which is the whole conflict: whichever of the two answers `GetBackBuffer` last is the
one the game draws on, and the other composes an empty surface.

So `external_renderer` mode hands the picture over completely (`g_scaler_enabled = 0`) and
keeps only what is upstream of composition. The part worth writing down is the menu.

Drawing an overlay on someone else's composed image needs a scene. Three placements were tried
against a stand-in wrapper — inside the Present hook, inside an `EndScene` hook, and with no
scene at all — and all three *succeeded*: the right surface (confirmed by pointer, against the
stand-in's own), `SetRenderTarget` returning `D3D_OK`, `Clear` returning `D3D_OK`. None of them
appeared on screen.

The reason is that with a wrapper installed, `device->EndScene` **is** the compositor. Opening
a scene to draw the menu and then closing it runs the wrapper's composition over the menu that
was just drawn. It is not a race, a stale surface or a lost render target — the drawing is real
and is then painted over, every frame, by the call used to finish it. Worse, if the patch's own
`EndScene` hook is installed, closing the scene re-enters that hook and recurses.

The way past it is the device underneath. A wrapper wraps the device but has no reason to wrap
the swap chain, so `GetSwapChain(0)->GetDevice()` hands back the real device, whose
`BeginScene`/`EndScene` compose nothing. The overlay is drawn through that, from an `EndScene`
hook, after the compositor has run. When the chain hands back the same object the patch hooked
— which is what a stand-in that patches a vtable instead of wrapping looks like — there is no
safe way to close a scene, and the menu says so rather than drawing nothing in silence.

Lesson, and it generalises: when every call in a sequence returns success and the result is
still invisible, stop checking the calls and ask what runs *after* them. And a stand-in built
to test a class of program tests the parts it actually imitates: this one reproduced the render
target substitution and the compositor faithfully enough to find a real bug, and not the object
wrapping — so the menu path stayed unverified until THRotator 2.1.0 itself ran it. It works:
TH12 at 360 Hz, menu on the presented surface, surface re-acquired across every device reset,
including the rotation between 1280x960 and 960x1280.

One more thing that only a real install shows. `after_device` runs again after every successful
reset, and a rotation wrapper resets whenever it turns the picture — so anything that belongs
there once a run has to say so once a run. The wrapper notice was already guarded by
`show_notice`, which shows at most one box per process, but the log line sat *outside* that
guard and claimed a warning had been shown six times in one session. A log that says something
happened when it did not is worse than no log line at all, because it is the thing you reason
from later; `show_notice` now reports whether it spoke, and only then is it written down.

### 5d. An import slot belongs to whoever wrote it last

thcrap does not fight for the frame loop; it fights for the import table, and for two releases it
won without either side noticing. Its injector lets the Windows loader finish and stops the
game's thread at the executable's entry point — which is *after* this DLL has loaded and
installed, because the game imports it. It then walks the import table, matches by name,
overwrites whatever it finds, and chains to `GetProcAddress(dll, func)`: the library's own
function, not the pointer it replaced. Its own source says as much ("we can override any
existing patches"). So every import this patch had hooked and thcrap also detoured was *dropped*
rather than chained to — `d3d9!Direct3DCreate9` among them, which is how the device is obtained,
so nothing else the patch does could happen either. Both patches reported success.

Two rules came out of it, and they generalise past thcrap:

- **Hook an import by chaining to whatever the slot held**, whoever put it there. Politeness in
  the other direction — abstaining from a slot somebody else owns — was tried first and is
  wrong: it makes the order of arrival decide who works.
- **Re-assert the hooks once, later.** `kernel32!QueryPerformanceCounter` is the trigger: the
  game calls it before it asks for Direct3D (measured, not assumed), no translation patch has
  any reason to take it, and it is readable while a DRM wrapper's stub is the only thing that
  has run — which is why the same entry point identifies a Steam copy. Whoever took the slot
  ends up nested inside this patch rather than discarded, and the log says when it happened.

The attempt in between is worth recording because it worked and was still wrong: redirecting the
*exporting library's export table* rather than the import slot. An import slot belongs to the
game; an export table belongs to the whole process. Handing a foreign address to
`d3d9!Direct3DCreate9` crashed Steam copies on the first frame, in `ntdll`, because Steam's
overlay reads that table too. `tools/check_patch_overlap.py` compares thcrap's own game
definitions against every byte this patch writes and verifies; for all four games the two sets
are disjoint, so the import table really was the whole conflict.

### 5e. The last instruction of a function you replaced is not yours

The replacement update runner is entered by a five-byte jump written over the game's own, so
from that jump onwards the whole function belongs to this patch — including its `ret`. In all
four games that `ret` is exactly where thprac puts its practice-menu hook, and a replacement
that returns by itself takes it away. The failure is silent in the worst way: thprac's
`GameGuiProgress` state machine means its draw-side hook renders nothing without a matching
update, so the menu simply never appears, with no error in either log.

The fix is to stop taking the instruction. The entry thunks are assembly and end by jumping to
the game's own `ret`, leaving EAX, ESP and the callee-saved registers exactly as the game's
epilogue does. TH10's runner takes its argument on the stack and ends in `ret 4`; entering with
the argument still there makes that fall out for free. The address is `addr.runner_ret`, read and
jumped to but never written, and checked before use — a `ret`, a `ret imm16`, or an `int3`, since
a breakpoint already sitting there is the very case this exists for.

The general rule, and the reason this is filed beside the wrapper and the import table: when you
replace a function, the entry is yours and the exit is not. Anything hooked at the end of it —
by a practice tool, a profiler, a debugger — expects to run. The catch-up pass in `limiter.c`
deliberately calls `hfr_runner` directly rather than through the thunk, because it is an extra
update with no frame drawn behind it and a menu should not be told about it.

`tools/test_thprac_stub.c` is how this was verified in a running game: it reproduces thprac's
hook mechanism exactly (one `0xCC`, a VEH at the front of the chain, a codecave holding the
original instruction and a jump back) and its progress state machine. Before the fix, on TH12
under Wine: update hook 0 hits, draw hook 476, every one of them with no frame open. After: the
two fire in step in all four games, in both installation orders.

## 6. Multi-game

### Three states a profile can be in

**Provisional.** Identified, then left completely alone — no hooks at all. A game that installs
and then faults is worse than one the patch does not claim to support: the second is obvious,
the first is a bug report.

**No simulation addresses.** The whole video path installs; the game runs at stock 60 Hz with
every scaling, filter and menu feature. `install()` says what it skipped, the menu disables the
timing controls with the reason, and the harness skips the tests that drive game structures.

**Fully described.** Everything.

Making the second state work meant moving the per-frame housekeeping out of the frame hook, so
it runs from the Present hook when there is no frame hook — a video feature no longer depends on
a gameplay address.

### What is shared and what is not

Shared, one implementation: the entire video path, the menu, the conflict guard, the crash
reporter, the scheduler, the replay machinery, and — since this phase — the speed-site
installation, which was the same function written twice and is now a table in the profile.

Not shared, and not shareable: `install_sites`. It is ~100 lines of hand-written x86 per game
encoding that game's specific quirks — item states, laser classes, fixed-point movement carry,
player shot callbacks. This is the real cost of a new game and there is no shortcut.

### Finding the awkward per-game fields

Each was found for one game and then for the others by pattern rather than by full analysis, and
the method generalises. `addr.runner_ret` is the newest of them and is covered in §5e; it is
simply the `ret` that ends `runner_fn`.

**The screenshot routine.** Find `snapshot/th%.3d.bmp`, find the one reference, disassemble
forward: the filename is built there and the routine is called a few dozen bytes later behind a
`lea eax,[esp+N]` guarded by `cmp esi,0x3e8`. Confirm by comparing instruction sequences — TH11's
was identical to TH12's, 61 instructions to the first `ret` — or, better, by checking the
function calls `GetBackBuffer`, `LockRect`, `UnlockRect` and `Release` through the device vtable,
which is what settled TH10's. The stub must not disturb EAX (the filename) and on TH10 also not
ESI, which the caller sets immediately before.

| | screenshot_fn | screenshot_call |
| --- | --- | --- |
| TH10 | `0x420670` | `0x4392c1` |
| TH11 | `0x429ca0` | `0x446901` |
| TH12 | `0x42fca0` | `0x450891` |

**The conflict sites.** Collect every 32-bit immediate in `vpatch_thNN.dll` landing in the game's
code section, then keep those whose code has the right shape: `a1 xx xx xx 00 8b 08`
(`mov eax,[device]; mov ecx,[eax]`) is a Present call, and the frame limiter and replay timing
are `e8` calls to one shared timing routine (TH10 `0x439540`, TH11 `0x446920`, TH12 `0x4508b0`).
**Check the bytes are identical in the Japanese and English executables** — a per-language
difference would refuse to install for someone whose game is fine. Then verify by running the
game through `vpatch.exe` and watching the guard name the site.

Identification signatures need the same JP/EN check, and are deliberately *not* the conflict
sites: identity is settled first from bytes no known patch touches, so a mismatch always means
"something else patched this game" and never "this is the wrong game".

---

## 7. TH10, and how its speed model differs

(The complete TH10 record — addresses, layouts, every hook and its reason — is
[TH10_DEVNOTES.md](games/TH10_DEVNOTES.md); this section keeps what the runtime learned.)

TH10 is supported. It is worth writing down what made it a different job from adding a second
game that shares TH11's engine, because TH13 and beyond will be one or the other.

**Its speed model is not a single float.** TH11 and TH12 each write a literal `1.0` into one
game-speed float at ~21 sites, fifteen of which this patch redirects, and the whole sub-stepping design hangs off multiplying that by
the sub-step duration. TH10 has no such single global — its top candidates take 12, 12 and 10
writes, spread across per-object timers that carry a pointer to the shared speed at `+0x0c`. So
its `install_sites` is per-game hand-written code: twelve `SpeedSite` entries (the generalised
`{addr,len,op,pop_float}` table the previous engine change introduced), plus movement-ftol,
item-homing and Cartesian-integration hooks that TH11/TH12 do not need in that form. The
generalised `SpeedSite` emitter and the runner's `critical_flag_mask` / `runner_return8_ends`
knobs are what let all of that sit on the shared runtime instead of forking it.

**One runner difference cost real debugging.** TH10 enters the game's critical section
unconditionally (`push 0x492274; call EnterCriticalSection`, no flag test), whereas TH11/TH12
gate it behind `misc_flags & 0x8000`. The generalised runner handles this with
`critical_flag_mask` (0 for TH10 means "always"); the only casualty was the harness, whose bare
fixture has no initialised section — it now initialises one (see `tools/test_runner.h`).

**The guard every game has.** The enemy hit test opens with "player state timer unchanged
since last frame → no damage", a stock double-hit guard. Sub-stepped, the integer timer
advances on the last minor tick of a frame, so on the boundary tick — the only tick the 60 Hz
enemy code runs the test — it always reads unchanged, and no shot lands at all. TH11 and TH12
replace it with "the float timer advanced across the last Player update", which the runner
tracks; TH10's port had everything around it but not this, and shipped with enemies immune
unless Player sub-stepping was switched off. It is at `0x42863e` in TH10, `0x434814` in TH11,
`0x439ef2` in TH12, and the same six-byte `cmp` in each. Look for it first in any new game.

**The fault that wasn't.** An earlier session chased a crash at `0x42b1e0` and suspected the
device redirect. It was not the patch at all: the test rig had a truncated `th10e.dat` and
`thbgm.dat`, and TH10 aborts partway through init when a data file is short, then dereferences a
null in its own cleanup path — vanilla does the same. With the real data files the fault is gone
and TH10 boots, plays at a 240 Hz tick rate with zero repeated frames, and records/replays HFR
replays with per-tick input. The lesson is the same one §8 keeps teaching: confirm the rig before
blaming the patch. The registers-and-stack backtrace the exception handler now logs is what made
this quick to see the second time — the faulting frame's return address landed in the game's own
data-load cleanup, not in any of our stubs.

---

## 7a. TH13, and what porting TH12's hooks taught

(The complete TH13 record is [TH13_DEVNOTES.md](games/TH13_DEVNOTES.md), including the object
layouts and the porting tools; this section keeps what the runtime learned.)

TH13 is supported. It confirmed the prediction in the old §9: one game-speed float
(`0x4c0a28`) written at 16 sites, so the whole `SpeedSite` design applied unchanged, and the
video path came up on the first run. The simulation took longer, for reasons worth writing down.

**Four engine changes, four profile fields, no fork.** The `UpdateFunc` grew a field, so the
callback argument moved from `+0x20` to `+0x24` (`layout.node_arg`). The runner now keeps the
next list node inside itself at `+0x50` and re-reads it after every callback and after every
removal (`layout.runner_next`; TH12 kept it in a register). `remove_node` takes
`(runner, node)` instead of `(node, runner)` (`remove_node_runner_first`). And the critical
section is gated on a byte flag at `0x4e49ed` rather than `misc_flags & 0x8000`
(`critical_flag_mask = 0xff` on that byte). None of these needed a line in the shared runner
beyond reading the field.

**Replays live in `%APPDATA%`.** TH13 (like every game from TH12.5 on) chdirs into
`%APPDATA%\ShanghaiAlice\th13\` around every save and load and keeps the string at
`0x4dd0d1`; the extension chunk was being appended to a file beside the executable that the
game never wrote. `addr.data_dir` names that string, and `replay_path()` prefers it when it is
non-empty (it is empty when `APPDATA` is unset, which is also when the game falls back to its
own directory).

**The MotionState hook is not wanted here.** TH12's `MotionState::step` adds a raw per-frame
velocity, so the runtime scales it by the sub-step. TH13 split the object into a pre-step
(`0x4736a0`) that recomputes the velocity from speed and angle *and multiplies by the game
speed*, and a step (`0x473780`) that adds it. The runtime already sets the game speed per tick,
so the port's first version, which also scaled the step, moved every shot and bullet at
`dt²`. Rule: before porting a "scale this increment" hook, check whether the increment is now
derived from something the speed float already scales.

**Addresses did not port by byte shape; structures did.** Thunks and small helpers matched
across the two binaries by normalised instruction sequence (`th13/match.py`), but the sites
that matter — inside big update functions — mostly did not, and the decompiled-body similarity
matcher (`th13/dmatch.py`) only narrowed the function. What worked was reading the TH12 hook's
*meaning* (which object, which field, which timer gates it) and finding the same operation in
the TH13 function: the ECL variable getter (`0x420380`) gave the enemy layout in one table
(position `+0x1230`, VM ids in a sub-object at `+0x11ec`, flags `+0x521c` with every bit two
places higher than TH12's), the inline timer tick pattern (`mov edx,[+4]; mov [+0],edx;
fld [ptr]; fcomp 0.99…`) marked every per-object timer, and the `Timer::add` callers with a
constant argument (`-14.0`, and the ANM `wait N`, inlined into `AnmVm::update` in TH13 where
TH12 had a helper) were the ones to give stock `value * logical` semantics.

**The guard that only fails at 360 Hz.** TH13's enemy hit test (`0x446870`) has, besides the
player-timer guard every game has, a per-shot one: a shot counts only if *its own* countdown
timer's integer changed on the last tick and `int % interval == 0`. (TH12's test is the
inverse — it *skips* on that tick — so TH11/TH12 never showed this.) Sub-stepped, the shot's
float ticks by `dt` and its integer changes on whichever tick the float crosses a whole
number; the enemy code runs on the boundary tick only. At 120 and 240 Hz `dt` is exact in
float32 and shots fired on the boundary keep crossing on the boundary, so the rig passed. At
360 Hz `dt = 1/6` is not exact, the crossing landed one tick off, and no shot ever hit. The fix
is the principle the runtime already uses for integer counters: a timer that exists for
*frame-counted* decisions is ticked by the logical speed on the boundary tick and not at all
on minor ticks (`0x4436b4`), so it reads exactly as at 60 Hz whatever the rate. Rule: any
"integer timer changed" test that 60 Hz code makes against a sub-stepped object's timer needs
this treatment; test at a rate whose `dt` is not a power of two before calling it done.

**Hooks that exist in TH12 and have no TH13 counterpart.** The curve laser still computes
"graze every 3 frames" but no longer acts on it, and the beam laser has no graze branch, so only
the line laser is gated; the UFO attraction hook has nothing to attach to; the player's
`state_timer % 60` block is gone and the new every-3-frames block at `0x443792` is guarded by
the game itself. Porting is not a checklist of addresses; half the list can legitimately be
empty.

**The enemy list head is a layout field now** (`layout.enemy_list`, `+0xb0` in TH13, `+0x68`
before), because it was the one enemy-interpolation constant still hard-coded in the shared
code.

---

## 8. Verification, and the traps in the rig

### Techniques that repeatedly paid

- **The vectored exception handler**, logging module and offset. It is the only thing a tester
  can send back from a windowed crash, and it identified both the screenshot routine and TH10's
  fault site.
- **The `IM_ASSERT` hook**, which disables the overlay and logs rather than taking the game down.
- **Comparing instruction sequences between games** to confirm a pattern-matched address.
- **Byte-comparing the patched image and the emitted stubs before and after a refactor.** For the
  speed-site refactor the stubs were identical and the images differed only in nine call
  displacements that all moved by the same `0x220` — our own functions shifting on rebuild.
- **A rate whose `dt` is not exactly representable.** 120 and 240 Hz give `dt` of 0.5 and
  0.25; every float32 sum lands exactly and phase-alignment bugs stay hidden. 144 and 360 Hz
  give 0.416667 and 0.166667, which round, and the TH13 shot guard (§7a) only failed there.
  The rig can run any `fps=` value; run the non-power-of-two ones too.
- **Measuring the measurer.** When every log agrees on a pattern that no fix touches, time
  one known quantity three ways (a 2 ms kernel delay by QPC, interrupt time and `timeGetTime`)
  and see which clock disagrees. §4's "the clock that lied" cost a week because the
  instrument was checked last.
- **Measuring the claim instead of arguing it.** A tester said the high frame rate was
  interpolated frames drawn twice. The stats line now counts presents that happened with no logic
  tick behind them, which is exactly what a duplicated frame is. TH11 in gameplay at a 240 Hz
  tick rate: 0 repeated frames out of 579, with 230 ticks/s behind 116 presents/s. That number
  is in the log so anyone can check their own run rather than take our word for it.

### Traps in the Wine/Xvfb rig, all of which cost real time

- **Wine substitutes its own `d3dx9_NN` unless told otherwise.** Super-xBR appeared to fail to
  compile on TH11 with `E5017: Aborting due to not yet implemented feature`, which reads like an
  old-compiler limitation and nearly earned a rewrite of the compiler selection. It is Wine's
  incomplete HLSL compiler. With the genuine DLL, every filter compiles on every real d3dx9 from
  33 to 41. Force native with `WINEDLLOVERRIDES="d3dx9_37=n"` before believing any shader result.
  The genuine failure found later has Microsoft's own error text, spelling mistake included —
  that is how to tell them apart.
- **Both games black out on their own** about twenty seconds after being left at the title
  screen, and a screenshot taken then is black too. Vanilla with no patch loaded does the same;
  it is the idle demo under a software renderer. This was dismissed once as "demo mode" without
  checking, and came back later disguised as a screenshot bug.
- **A timed-out command leaves its state behind.** A resize test that appeared to show stretching
  instead of letterboxing had actually run vanilla, because an earlier command had timed out
  before restoring the DLL it renamed. Check the patch is loaded before believing what a test
  says about it.
- **The harness must not buffer its output.** Under Wine stdout is block-buffered into a pipe, so
  a run that hangs prints nothing at all and gives no clue where. It is unbuffered now.

---

## 9. Open work, in order of value

1. **The alt-tab input fix** vpatch has and we do not — a foreground check on the DirectInput
   path. Cheap, and it affects all four supported games.
2. **`ReplaySlowFPS`** — slow-motion replay on a held key. The tick-rate machinery makes this
   nearly free.
3. **The `UI_*` settings live in four places** — the enum, both switches and the save function,
   with a `default:` that stops the compiler noticing an omission. A table of
   `{id, name, section, &cfg.field}` would collapse all four and the INI read as well, which is
   currently a fifth. Nothing is inconsistent today; it is a drift risk, not a bug.
4. **`tools/embed_shaders.py` restates the pass-splitting rule** that `shader_parse.h` owns,
   because the build step is Python and the runtime is C. The checker tool includes the real
   header, so the two implementations that matter cannot disagree.
5. **thprac beyond the menu hook** (§5e). Its practice menu now runs, but the rest of
   `docs/MOD_COMPATIBILITY.md`'s list is open: anything thprac counts in frames runs at the
   presentation rate, its FPS controls compete with this patch's pacing, its `ReplayClearParam`
   drops foreign replay chunks rather than preserving them, and its `io.DisplaySize` disagrees
   with itself after a device reset once `internal_scale > 1`.
6. **TH14 and beyond.** TH13's port (§7a) is the template: the speed-float pattern held, the
   engine changes were absorbed by profile fields, and the per-object hooks were found by
   meaning rather than by byte shape. Expect the same shape of job.

## Sub-steps per 60 Hz frame at a rate that is not a multiple of 60

Raised from outside, and worth settling with numbers rather than argument: with sub-stepping on
the logic rate follows the display, so at 144 Hz the scheduler runs 144 ticks a second against
60 Hz frames. 144/60 is 2.4, so the number of ticks that fall inside one 60 Hz frame cannot be
constant. Measured over 200 frames:

| rate | ticks per 60 Hz frame | travel between consecutive collision tests |
|---|---|---|
| 144 | 2 or 3, period 5: `3 2 3 2 2` | 106 or 107 units of 256 -> 0.4141 / 0.4180 frames |
| 165 | 2 or 3, period 4: `3 3 3 2` | 93 or 94 -> 0.3633 / 0.3672 |
| 240 | 4, always | 64 -> 0.2500 |
| 360 | 6, always | 42 or 43 -> 0.1641 / 0.1680 |

The counts are exactly as predicted, including the periods. **The conclusion that follows from
them is not.** The worry was that a 2-tick frame bounds hit detection more loosely than the 2.4
average suggests -- but what bounds tunnelling is the distance a bullet travels between two
consecutive collision tests, and collision is tested once per tick. The ticks are evenly spaced
in real time, so that distance is uniform: 0.414 to 0.418 of a game frame at 144 Hz, everywhere,
with no frame in which a bullet moves half a frame between tests. The 2-or-3 split is where the
60 Hz boundaries happen to fall among evenly spaced ticks, not a variation in their spacing.

Note that 360 Hz has the same one-unit dither in `dt` (42 or 43 of 256) while still giving
exactly six ticks a frame, which is the same fact from the other side: the Bresenham step is
what keeps the slices summing to exactly one frame, and it dithers whether or not the rate
divides 60.

What the split does decide is where the *major* tick falls -- the boundary tick on which the
frame-locked systems run. Those still run exactly sixty times a second at every rate, which is
the property the whole design rests on.

So 144 and 165 are not the odd ones out for hit detection. They would be for a design that fixed
N sub-steps per 60 Hz frame and presented whenever the panel was ready: that makes the count
uniform and the *spacing* non-uniform, which is the trade in the wrong direction, and it also
stops a presented frame coinciding with a tick, so every sub-stepped object would need
interpolating to draw. The current arrangement gets uniform spacing and exact 60 Hz boundaries
at the cost of a count that is only constant when the rate divides 60, and the count is the part
that does not matter.

### The same question for New Classic, where the answer is the other one

The section above is about `timing.c`, the x86 scheduler. New Classic is a separate case, and the
concern is live there.

**A correction, because the first version of this section got it wrong.** It analysed
`substep.h`'s nested Bresenham and reported steps of 0.5 and 0.332 frames at 144Hz. `substep.h` is
not called by the live New Classic loop: its only callers are in `tools/test_fixed.c`. Production
uses `fixed_clock_step()` for the frame clock and `subtick_slice()` for the slices, and
`subtick_slice` is not a Bresenham at all -- it returns whatever wall-clock time has elapsed since
the last slice, expressed in frames. Measuring dead code and reporting it as the shipped behaviour
is a worse mistake than the one it was correcting, and the check that would have caught it is one
grep for the function's callers.

`FIXED_STEP_RESEARCH.md` models the live call order and reports a projectile slice ranging from
1.389 ms to 12.5 ms at 144Hz -- a twelfth of a frame to three quarters of one. The asymmetry comes
from `update_first()`: on a major tick it finishes the outgoing frame with
`projectiles_slice(ticks + 1.0)` so the 60Hz pass reads exactly the positions the unmodified game
would, then runs the native logic and moves the *player* to the new phase with
`subtick_move(ticks + phase)` -- but does not advance the projectiles to that phase. The
projectiles' `moved_to` is left on the frame boundary while the phase has already moved on, so the
next minor slice carries the extra.

That asymmetry is deliberate for the player (the native pass must see stock positions) and looks
unintended for the projectiles. It is worth a bounded investigation on its own terms, ahead of any
larger scheduling work: check the player/projectile collision order, the frame transition, native
fast-forward and replay behaviour before changing it.


## Sweeping the TH14 findings back across the other games

Every defect TH14 turned up was checked against TH10-13, because a family of games built on one
engine will usually have the same mistake in the same place. Three of the five were already
handled, and two were not.

| what TH14 turned up | TH10 | TH11 | TH12 | TH13 |
| --- | --- | --- | --- | --- |
| a cycle rewind multiplied by the game speed | already fixed | already fixed | already fixed | already fixed |
| `native_size_cycle` claims an F10 the game does not have | correct (0) | **wrong** | **wrong** | **wrong** |
| only one of the loader's two play sites hooked | correct | **wrong** | **wrong** | **wrong** |
| the loader's header-reading site hooked as well | **wrong** | correct | correct | correct |
| replay magic | correct | correct | correct | correct |
| `data_dir` | correct (none) | correct | correct | correct |

### The rewind was the one thing that was already right everywhere

`Timer::add` with a constant count is scaled by the timer's rate, so sub-stepped a fourteen-frame
rewind becomes a rewind of 14/6. Every game has it, in the same place -- the player's shot cycle --
and every one of TH10-13 already patches it, at two sites each:

```
TH10  0x428243 shot cycle (-15.0)   0x440e3d ANM wait
TH11  0x4343fc shot cycle (-14.0)   0x4355cd ANM wait
TH12  0x439ac2 shot cycle (-14.0)   0x43adbd ANM wait
TH13  0x44647c shot cycle (-14.0)   0x4629ef ANM wait
TH14  0x45101a shot cycle (14)      -- none --
```

TH14 was the only game missing it, which is why its Reimu stopped firing. It has no second site:
of the four rate-aware rewinds in the executable, one is the shot cycle, two are unreferenced
copies of the helper (`0x4085b0` and `0x411530`, zero direct callers), and the fourth is inlined
in the dialogue text routine at `0x442d03`, which is frame-locked and therefore runs at a factor
of one. Found by listing every reference to the `0.99f` guard constant that every rate-aware timer
operation in the game uses -- ninety of them, of which exactly four rewind.

### F10 was wrong in three more games

TH14's `native_size_cycle = 1` was inherited from TH13's profile and was untrue: nothing in the
executable reads VK_F10, and its window procedure handles only `WM_SYSCOMMAND` and swallows
`SC_KEYMENU`. The same two checks clear TH11, TH12 and TH13: none of them compares anything
against `0x79`, none indexes a key array at `0x79`, and all three window procedures have the same
`cmp esi, 0x112` / `sub eax, 0xf090` shape TH14 has and nothing else. So F10 did nothing on four
of the five games, and the README said otherwise. Only TH10 was right, and only because its flag
was 0 for a different reason -- its own resolution dialog is stuck at 640x480.

### The replay loader's modes

Every game dispatches a replay start on a mode of 1 or 2, and the two branches are the same call
with the same real manager; mode 1 additionally stores that manager in a global. Both are play
sites. The loader's remaining call sites build a throwaway manager, write 2 into its `[+0x10]`,
load a file into it to read the header, and throw it away -- that is the menu building its list.

```
            play (mode 1)   play (mode 2)   peek
TH10        0x429257        0x42948c        0x429765
TH11        0x435a0d        0x435c2e        0x435f74
TH12        0x43b1d2        0x43b439        0x43b774
TH13        0x447c1b        0x447d41        0x448043, 0x4523de, 0x4524d6
TH14        0x4549bc        0x454b40        0x454fd3, 0x45ee0f
```

TH11, TH12 and TH13 hooked mode 1 and not mode 2, so a replay started the other way played back
without its recorded rate -- silently, at 60Hz. TH13's adapter even said "(mode 1)" in its comment,
so the second was known about and never added. `replay_load_call` is now `replay_load_calls[2]`.

TH10 had the opposite mistake and the more serious one: it hooked all three sites, including the
peek. That is the same thing that took TH14 down when its extension was first installed -- the
load wrapper reads simulation metadata off the file and puts a message box up for anything it does
not recognise, so opening a menu that lists replays does that once per file on disk. TH10 has one
peek site rather than TH14's two, which is presumably why it had not been noticed.
