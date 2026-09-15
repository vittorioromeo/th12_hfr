# THRotator and thprac compatibility research

Research snapshot: 2026-09-15. This records source inspection, executable disassembly,
and automated patch-range comparisons. **Neither combination has been run in a game
during this investigation. This is a feasibility report, not a supported installation recipe.**

## Recommendation

Co-loading is feasible in principle for TH10–13. Keep the mods separate and add focused
compatibility work before considering wholesale reimplementation. Loading the modules is
the easy part; agreeing on rendering, timing and input is the actual work.

| Combination | Finding | Recommended direction |
| --- | --- | --- |
| HFR + THRotator, TH10–13 | Different proxy filenames already allow both to load. Their graphics pipelines need coordination. | Prototype an explicit external-renderer mode, then decide whether to integrate filtering and menus further. |
| HFR + thprac, TH10–13 | No intersections in the scanned patch ranges, but HFR bypasses thprac's UI update hooks. Timing and replay editing also need work. | Preserve thprac's practice implementation; bridge its callbacks to HFR's scheduler. |
| HFR + both | Inherits both sets of issues; thprac UI may also be rotated/cropped as game content. | Validate each pair first, then test all three together. |
| Either mod + TH06 New Classic | Existing implementations target different engines/APIs from New Classic. | Separate porting work; a DLL loader cannot supply support. |

Native rotation and HUD layout would be a reasonable HFR feature because it fits the
existing compositor and could eventually serve both D3D9 and D3D11. Reimplementing
thprac's practice options, stage/script edits and replay behavior would create a much
larger maintenance burden. These two reimplementation decisions should be made separately.

## Versions and evidence

| Project | Inspected revision |
| --- | --- |
| Touhou HFR | `5afebd382396db9697a92810114069252a22fee3` (`v0.5.2-test` in source) |
| [THRotator][rotator] | `a9e95cdb7b425ee13bef929ad8beb99fe0e2ebb6` |
| [thprac][thprac] | `585fae1aad4b720f350655a44e3f2ec7fc0dfa25` |

The upstream checkouts were fetched for this investigation. Conclusions are pinned to
these source revisions, not to every past/future release binary. Local clones are outside
the HFR repository in `../analysis/compat-20260915/`.

HFR's existing regression suite was run against clean TH10 v1.00a, TH11 v1.00a,
TH12 v1.00b and TH13 v1.00c executables. It passed and produced fresh patch plans.
This validates the HFR baseline and the addresses used below, **not co-loading**.
Steam co-loading was not tested; the address analysis applies to the supported decrypted
game layouts, not their on-disk Steam wrappers.

No runtime changes, mod installations, game launches or changes to game directories were
made for this investigation. The added code is the read-only audit utility described below.

## What a multiple-DLL proxy can solve

The current x86 HFR autoloader is `dinput8.dll`, which loads `touhou_hfr.dll`.
THRotator's relevant proxy is `d3d9.dll`. These filenames do not collide. The game can
load both through its normal imports. thprac is an executable with its own injection
bootstrap and an existing attach-to-process interface; it does not need a third proxy
filename. Its [documented usage][thprac-readme] supports attaching after launching a
game normally, through Steam, or through thcrap.

For a future controlled test, launch a Steam game through Steam with HFR installed,
then use thprac's attach mechanism. Its `--without-vpatch` and `--without-oilp`
options prevent automatically adding competing timing patches when using its launch
paths. They do not remove a timing patch that is already loaded, and do not disable
thprac's own FPS controls.

A generic `LoadLibrary` list helps only when the modules initialize themselves and
their hooks already cooperate. It cannot merge two Direct3D factories or two frame
schedulers. thprac's [loader][thprac-loader] uses a bootstrap beyond loading its EXE;
loading that EXE as an arbitrary DLL is not its supported injection path.

THRotator's [exports implementation][rotator-exports] loads Direct3D from an absolute
system-directory path. Loading a renamed THRotator DLL is insufficient: the game's
`Direct3DCreate9`/`Direct3DCreate9Ex` calls must actually pass through its exports and
the returned wrapper objects. Two graphics wrappers need a chain with a single returned
device, not a broadcast to both factories.

For genuine same-name collisions, such as New Classic's HFR `dxgi.dll` and another
`dxgi.dll` proxy, a forwarding proxy may be useful. It must preserve exports, architecture,
factory/object ownership and initialization order. That is a separate problem from
making either of these two mods understand New Classic.

## THRotator: graphics ownership is the main issue

HFR already has part of the needed infrastructure. In
[`detect_d3d9_wrapper`](src/backends/d3d9.c), automatic mode notices a non-system
`d3d9.dll`, selects the game's presentation chain and disables HFR's D3D9Ex path.
HFR's [import hooks](src/core/imports.c) preserve the previous IAT target, and its
device hooks retain the previous vtable functions.

These are useful foundations, but `own_present=0` alone does not establish compatibility:

| Source behavior | Consequence for HFR |
| --- | --- |
| THRotator `GetBackBuffer` returns its virtual render target. | HFR's `g_real_bb` can actually be THRotator's input surface. |
| THRotator `CreateAdditionalSwapChain` forwards to the underlying device without wrapping the chain. | HFR presenting on its own additional chain bypasses THRotator's display path. |
| THRotator performs rotation/HUD composition in **`EndScene`**, not just `Present`. | HFR's additional scene inside its Present hook causes another composition pass. |
| THRotator counts/bounds `SetViewport` calls to distinguish game content and menus. | HFR's filter and overlay passes alter the inputs to this heuristic. |
| THRotator records the requested backbuffer dimensions and manages the window. | HFR resolution changes and resizing must preserve the coordinate system expected by its presets. |

These behaviors are in [THRotatorDirect3D.cpp][rotator-device] (`GetBackBuffer` around
line 696, `SetViewport` around 2365, `EndScene` around 2394, `InternalPresent` around
2710), and the viewport counter in [THRotatorEditor.cpp][rotator-editor]. HFR's extra
scene is in [`scaler_blit`](src/core/scaler.c).

With the current code, the likely call sequence is:

```text
Game draws into HFR's offscreen target
  -> game's EndScene calls THRotator's EndScene/compositor
  -> HFR's Present hook opens another scene
       -> HFR filters its target into the surface returned by THRotator
       -> HFR draws F11 UI
       -> another THRotator EndScene/compositor
  -> THRotator Present -> system Direct3D Present
```

The second composition could produce a visible image, but that is not proof of correct
viewport classification, editor behavior, resolution handling or mouse coordinates.
An F11 panel rendered before HUD rearrangement can itself be rotated or cut into pieces.
THRotator's own editor may update twice for one presentation. These are source-derived
risks to test, not observed gameplay failures.

### First implementation experiment

Add an explicit mode in which HFR owns simulation and frame pacing while THRotator owns
render-target setup, scaling, rotation, resizing and presentation. Start with TH12,
then the other profiles. Initially configure HFR through its INI if drawing F11 safely
requires a separate overlay integration; do not count that as full menu compatibility.

The following existing settings remove some variables for a controlled experiment:

```ini
[hfr]
d3d9ex=0
flipex=0

[video]
own_present=0
internal_scale=1
texture_scale=0
```

They **do not implement** that external-renderer mode: HFR still allocates its scaler
targets, opens an extra scene and attaches its window behavior. `g_scaler_enabled` is
an internal variable, not a documented INI bypass. THRotator itself supports D3D9Ex;
disabling it here narrows the experiment and matches HFR's present wrapper policy.

For full integration, explicitly arrange one final THRotator composition per presented
frame. Put any HFR filtering before that composition, and draw screen-space menus after
it, with matching mouse coordinates. Doing this may require a small THRotator-side
interface or changes to the hook placement; an arbitrary wrapper does not expose a
standard "compose now, then draw my overlay" API.

HFR's generic wrapper warning also needs correction for this case: it currently says HFR
replaces what the wrapper does, which is untrue for THRotator's rotation and HUD editor.

## thprac: a scheduler adapter, not an address collision

### Reproducible patch-range audit

[`tools/check_thprac_overlap.py`](tools/check_thprac_overlap.py) compares fresh HFR
patch plans with literal thprac hook declarations, hotkey patches, explicit tracker
hooks, `SetDpadHook` sites and native FPS-operand writes in the four game files.
Optional patches are included even if disabled by default.

thprac [callback hooks][thprac-hooks] write a one-byte `INT3`, but copy an instruction
span into a trampoline. The utility checks both the byte written and the span read,
as well as HFR's frozen signatures and frame-loop guards.

| Game | thprac sites scanned | HFR write ranges | Write overlaps | Signature/guard overlaps | Additional copied-instruction overlaps |
| --- | ---: | ---: | ---: | ---: | ---: |
| TH10 v1.00a | 60 | 50 | 0 | 0 | 0 |
| TH11 v1.00a | 52 | 66 | 0 | 0 | 0 |
| TH12 v1.00b | 44 | 69 | 0 | 0 | 0 |
| TH13 v1.00c | 44 | 59 | 0 | 0 | 0 |

No recognized declarations were left unparsed in this snapshot. This is a scoped source
scan, not a proof that all memory writes are disjoint. It does not cover arbitrary pointer
writes, script/data edits, shared IAT/vtable slots or changes in control flow.
The HFR side describes the emitted test configuration, not every possible setting.

Reproduce from the HFR root, replacing executable paths with clean supported builds:

```powershell
.\test.ps1 -GameExe @('C:\Games\TH10\th10.exe', 'C:\Games\TH11\th11.exe',
                     'C:\Games\TH12\th12.exe', 'C:\Games\TH13\th13.exe')
python tools/check_thprac_overlap.py ..\analysis\compat-20260915\thprac 'build/tests/fixture*.patches.json'
```

Only use the plans emitted for those four inputs; remove unrelated plan paths from the
invocation. On the research machine the test log is `build/compat-plans.log` and the
fresh plans are `build/tests/fixture0.patches.json` through `fixture3.patches.json`.
The scanner's extraction, comment handling, unparsed-declaration reporting, half-open
ranges and distinction between breakpoint writes and copied spans were also checked
with a synthetic source fixture.

### Confirmed unreachable hooks

HFR [installs](src/core/install.c) a jump at the original update-runner entry to its own
[`hfr_runner`](src/backends/update_runner.c). It executes the callback list itself and
returns directly. It never executes the original runner's return instruction.

thprac hooks exactly those original returns:

| Game | Runner entry replaced by HFR | thprac UI update hook | Original instruction | thprac UI render hook |
| --- | --- | --- | --- | --- |
| TH10 | `0x449c00` | `0x449d0e` | `ret 4` | `0x4394fa` |
| TH11 | `0x456cb0` | `0x456deb` | `ret` | `0x456f12` |
| TH12 | `0x4624c0` | `0x4625fb` | `ret` | `0x462722` |
| TH13 | `0x470af0` | `0x470c04` | `ret` | `0x470d27` |

All are virtual addresses in the supported x86 layout, image base `0x400000`.
The return instructions were independently verified with Capstone against the clean
local executables. See the upstream [TH10][thprac10], [TH11][thprac11],
[TH12][thprac12] and [TH13][thprac13] update callbacks.

Those callbacks build thprac's UI and update practice/replay panels, hotkeys and the
tracker. Their render callbacks can still be reached, but `GameGuiRender` has a progress
guard and returns without a completed UI frame. An absent/inert UI is therefore a more
direct prediction than a guaranteed crash. Reversing load order does not restore a
return instruction that HFR never executes.

### Two plausible adapter approaches

**Minimal HFR-side prototype:** explicitly visit the original return-hook site from a
controlled thunk after the appropriate HFR update. The four inspected update callbacks
do not depend on incoming game registers. This could preserve compatibility with an
unmodified thprac binary, including its existing VEH hook, without exposing its private
C++ functions. It is a hypothesis to validate: preserve HFR's return value/registers,
handle TH10's `ret 4` calling convention, verify the site/version, and account for late
attachment. Do not call the complete original runner again; that would double-update
the game. Restoring reachability alone does not solve callback frequency or replay editing.

**Preferred maintained interface:** a small, versioned C ABI between HFR and thprac,
implemented on both sides. Keep per-game practice code upstream in thprac. Share the
adapter/scheduling code in HFR and put only genuine per-game details in its profiles.
Separate simulation-bound practice work from UI preparation and drawing. Start practice
work at the native 60 Hz frame boundaries; allow UI drawing at every presentation.
Audit each callback instead of treating all thprac code as either 60 Hz or high-rate code.

Simply invoking the entire old update callback at 360 Hz could change hotkey repetition,
tracker behavior and speed control. Invoking it only at 60 Hz is also insufficient for
smooth overlay persistence: `GameGuiRender` consumes the prepared frame once. Either
prepare UI independently each presentation or safely reuse completed draw data between
native updates. Pause and frame advance must retain responsive UI.

### Timing, input and device lifecycle

HFR should own the clock. thprac's TH11, TH12 and TH13 `FpsInit` can replace native
limiter operand pointers at `0x44647e`, `0x45044e` and `0x45d36a` respectively. These
writes do not overlap HFR's plans, but HFR replaces native limiting, so the controls
cannot be assumed effective. TH10's inspected path instead controls vpatch/OILP when
available; it does not install an equivalent native operand replacement.

Disable or delegate thprac FPS, fast/slow replay and other timing controls while HFR is
active. Keep simulated game speed, substep frequency and presentation rate distinct.
HFR already rejects known vpatch/OILP modules at installation and warns on its first
frame if they arrived later. Its module check is not a thprac compatibility check and
does not monitor arbitrary late attachments continuously.

Both projects wrap input and device functions. thprac's Reset hook saves and calls the
previous implementation, which is promising. Its `joyGetPosEx` IAT hook also saves the
previous target. HFR's startup import reassertion must not blindly wrap a hook that
already calls HFR: that could create `HFR -> thprac -> HFR` recursion. The current startup
ordering needs testing, and a future general compatibility manager must distinguish
cooperative chains from hooks which bypass earlier owners. See
[thprac input handling][thprac-games] and [its D3D9 backend][thprac-dx9].

Test focus, cursor visibility, mouse coordinate conversion, controller/D-pad input,
resets and menu capture explicitly. HFR's F11 and thprac's default F12/Backspace menu
keys are different; customized bindings and simultaneous menus still need coordination.
Separate embedded ImGui contexts are not inherently a conflict.

### Replay format: append can coexist; editing needs a fix

For TH10–13, both projects append `USER` chunks and can skip foreign chunk types.
thprac uses four-byte `PRAC`; HFR uses `0x48`, `0x49`, `0x4a` for rate, input and
simulation metadata. The identifiers do not collide. HFR wraps the save call and appends
after it returns; thprac hooks inside saving, so `PRAC` followed by HFR chunks is a
plausible normal order. This still needs a real save/load test.

A concrete incompatibility exists in thprac's `ReplayClearParam`: on finding `PRAC`,
it truncates the file there. `CloneReplayWithParams` uses this operation. Any following
HFR chunks would be lost. A compatibility fix should remove/replace only `PRAC` while
preserving other chunks. See [thprac replay handling][thprac-games] and
[HFR replay handling](src/core/replay.c).

Metadata coexistence does not prove replay determinism. Test practice warps, restarts,
resource changes and input streams together, including editing/re-saving replays. A replay
using both mods may need both mods and compatible versions/settings for playback.

## Reimplementation tradeoffs

THRotator provides rotation, selectable playfield/HUD rectangles and an editor. Simple
90-degree rotation is a small subset of that functionality. HFR could independently
implement rotation plus rectangle composition in its own video pipeline, sharing layout
math/configuration across the D3D9 and D3D11 backends. Existing presets could be an
interchange format after validating dimensions, menu detection and field semantics.

THRotator's README specifies GPLv3 for code and public domain for configuration files.
Copying its implementation and independently implementing its features are different
licensing choices; do not treat the code as permissively licensed merely because the
presets are public domain. [Source declaration][rotator-readme].

thprac is [MIT licensed][thprac-license], which makes attributed code reuse possible,
but importing it wholesale would also import its game-specific maintenance burden.
Its per-game code edits scripts, handles individual spell/stage variants, manages
practice state and extends replays. A stable compatibility bridge avoids duplicating
that work and lets users update thprac independently.

New Classic uses HFR's x64/D3D11 backend. THRotator's D3D8/9 wrappers do not intercept
D3D11; thprac's inspected original-TH06 support is not New Classic support. There is no
New Classic profile in the inspected thprac registry. A 64-bit loader or renamed proxy
does not bridge either gap. Native HFR rotation could transfer to New Classic more
readily than thprac's original-TH06 practice patches.

## Implementation status: THRotator mode, 2026-09-15

Step 1 of the order below is implemented and partly verified. What the research above
proposed as an "explicit external-renderer mode" is `[video] external_renderer`
(`-1` recognise, `1` force, `0` never), and it is the first thing `detect_d3d9_wrapper`
decides.

**How it recognises THRotator.** Not by file name: THRotator exports
`THRotator_GetVersionString` for exactly this purpose, so the patch asks and puts the version
in the log. A wrapper it does not recognise is caught a moment later instead, by behaviour:
after the device exists, `external_detect_substitution` compares what the device answers to
`GetBackBuffer` with what the swap chain answers. A wrapper that only places the finished
image in the window returns the same surface from both; one that composes the image itself has
to take over the device's call and has no reason to touch the chain, so the two differ. That
is the distinction that matters, and it needs no list of names.

**What the mode does.** `g_scaler_enabled` goes to 0, which is the single switch the research
was looking for: no render-target redirection, no `GetBackBuffer` substitution, no compositor,
no filters, no letterbox, no swap chain of its own, and — importantly — `scaler_adjust_pp`
stops rewriting the presentation parameters, so the renderer is told the game's own size and
windowed flag rather than the patch's. 9Ex, internal resolution and texture upscaling are
turned off with it. `window_enforce`, `window_override_pp` and the size-cycle key stand down,
and the window procedure passes every message about geometry through: the renderer arranges
that window now. Dimming, sub-stepping, interpolation, pacing, replay and input are untouched,
because none of them are downstream of who composes the image.

**Correction to the section above.** The predicted call sequence is right about the danger and
wrong about the mechanism. It is not that HFR's extra scene causes "another composition pass"
— it is that *ending* that scene is the composition pass. With a wrapper in place, the device's
`EndScene` **is** the compositor, so any scene the patch opens and closes for its own drawing
composes the renderer's picture over whatever it just drew. Three placements of the menu were
tried against a stand-in wrapper before this was measured: in the Present hook, in an EndScene
hook, and with no scene at all. All three drew successfully to the correct surface — verified
by pointer and by `Clear` returning `D3D_OK` — and none of them reached the screen.

The way past it is the device underneath: the wrapper wraps the device but forwards
`GetSwapChain`, so `chain->GetDevice()` hands back the real one, whose `BeginScene` and
`EndScene` compose nothing. The menu is drawn through that, from an `EndScene` hook, after the
compositor has run.

**What is verified.** First against a stand-in `d3d9.dll` reproducing the two behaviours that
matter — substituting the render target, composing in `EndScene` — with TH10 under Wine, which
is what found the `EndScene` problem above. Then against **THRotator 2.1.0 itself, on TH12 at
360 Hz**: recognised by its version export, the game's own 640x480 windowed parameters passed
through unrewritten, 360 presents/s with sub-stepping and sub-tick input, the F11 menu drawn on
the presented surface, and the surface re-acquired across every device reset — including
THRotator's rotation, where it goes from 1280x960 to 960x1280 and back.

The stand-in's one unfaithful detail is worth recording: it patches the real device's vtable
rather than wrapping the device, so `chain->GetDevice()` returned the same object and the patch
declined to draw the menu. That tested the refusal rather than the drawing, and it is why the
menu stayed unverified until a real install ran.

**A THRotator bug, for anyone else who hits it.** v2.1.0 aborts on a first run in a folder with
no configuration. `THRotatorSetting::Load` catches the `std::ios::failure` from the missing
`<exe>.throtator` and calls `LoadIniFormat` *from inside that catch handler*; the missing
`throt.ini` throws the same exception, which a sibling `catch` of the same `try` cannot catch,
and nothing above catches it either — so `std::terminate`. It presents as a Visual C++ runtime
dialog with no explanation, and the last line of `throtator-log.txt` is the failed
`throt.ini` load. Dropping any valid `<exe>.throtator` in beside the game avoids it; the
project's own `sample-config/` has one per game. Nothing to do with this patch, but it looks
exactly like a compatibility failure.

Not run yet: THRotator on Steam copies, and THRotator with thcrap also loaded.

## Suggested implementation and validation order

1. **THRotator + TH12 proof:** add the explicit external-renderer mode, retain HFR
   pacing/simulation, and verify rotation/HUD layout at 60 and high refresh rates.
   Establish ownership of backbuffers, window size and resets before integrating filters
   or promising F11 mouse support. Repeat with TH10/11/13.
2. **thprac + TH12 proof:** restore the bypassed UI callback through a guarded adapter;
   validate its frequency and drawing persistence. Initially exclude competing timing
   options. Verify real stage practice, restarts and replay playback. Use the result to
   choose an unmodified-binary shim versus an explicit upstream ABI.
3. **Complete compatibility:** separate UI and simulation callbacks, delegate speed
   controls, preserve foreign replay chunks during editing, and coordinate input/reset
   hooks. Test startup injection and late attachment, clean retail and Steam launches,
   and thcrap with HFR's latest IAT handling.
4. **Combined regression matrix:** both pairs and all three mods, representative refresh
   rates (including non-integer multiples of 60), pause/resume, stage changes, death/bombs,
   controller input, F11/F12/editor menus, resized/borderless/fullscreen windows, Alt-Tab,
   screenshots, device resets and replay save/load/edit. Include internal resolution,
   texture scaling and filters only after the basic ownership model works.
5. **Optional native video features:** add rotation/HUD layouts if the desired user
   experience is one integrated menu, especially for New Classic. Keep thprac separate.

This provides a path to reuse most existing code. It does not require a universal DLL
loader or rewriting either mod before obtaining the first useful compatibility result.

[rotator]: https://github.com/massanoori/THRotator/tree/a9e95cdb7b425ee13bef929ad8beb99fe0e2ebb6
[rotator-readme]: https://github.com/massanoori/THRotator/blob/a9e95cdb7b425ee13bef929ad8beb99fe0e2ebb6/README.md
[rotator-exports]: https://github.com/massanoori/THRotator/blob/a9e95cdb7b425ee13bef929ad8beb99fe0e2ebb6/common/THRotatorExports.cpp#L55
[rotator-device]: https://github.com/massanoori/THRotator/blob/a9e95cdb7b425ee13bef929ad8beb99fe0e2ebb6/common/THRotatorDirect3D.cpp#L2394
[rotator-editor]: https://github.com/massanoori/THRotator/blob/a9e95cdb7b425ee13bef929ad8beb99fe0e2ebb6/common/THRotatorEditor.cpp#L172
[thprac]: https://github.com/touhouworldcup/thprac/tree/585fae1aad4b720f350655a44e3f2ec7fc0dfa25
[thprac-readme]: https://github.com/touhouworldcup/thprac/blob/585fae1aad4b720f350655a44e3f2ec7fc0dfa25/README.md
[thprac-license]: https://github.com/touhouworldcup/thprac/blob/585fae1aad4b720f350655a44e3f2ec7fc0dfa25/LICENCE
[thprac-loader]: https://github.com/touhouworldcup/thprac/blob/585fae1aad4b720f350655a44e3f2ec7fc0dfa25/thprac/src/thprac/thprac_load_exe.cpp#L252
[thprac-hooks]: https://github.com/touhouworldcup/thprac/blob/585fae1aad4b720f350655a44e3f2ec7fc0dfa25/thprac/src/thprac/thprac_hook.cpp#L22
[thprac-games]: https://github.com/touhouworldcup/thprac/blob/585fae1aad4b720f350655a44e3f2ec7fc0dfa25/thprac/src/thprac/thprac_games.cpp
[thprac-dx9]: https://github.com/touhouworldcup/thprac/blob/585fae1aad4b720f350655a44e3f2ec7fc0dfa25/thprac/src/thprac/thprac_gui_impl_dx9.cpp#L323
[thprac10]: https://github.com/touhouworldcup/thprac/blob/585fae1aad4b720f350655a44e3f2ec7fc0dfa25/thprac/src/thprac/thprac_th10.cpp#L2407
[thprac11]: https://github.com/touhouworldcup/thprac/blob/585fae1aad4b720f350655a44e3f2ec7fc0dfa25/thprac/src/thprac/thprac_th11.cpp#L2084
[thprac12]: https://github.com/touhouworldcup/thprac/blob/585fae1aad4b720f350655a44e3f2ec7fc0dfa25/thprac/src/thprac/thprac_th12.cpp#L1793
[thprac13]: https://github.com/touhouworldcup/thprac/blob/585fae1aad4b720f350655a44e3f2ec7fc0dfa25/thprac/src/thprac/thprac_th13.cpp#L1774
