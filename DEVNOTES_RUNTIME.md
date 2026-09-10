# Developer notes: the shared runtime

What was learned building the video path (scaling, filters, the in-game menu) and taking the
patch from one game to three. Written for whoever picks this up next, including me.

The other documents, and why this is not one of them:

| | |
| --- | --- |
| `ARCHITECTURE.md` | where the code lives and how the pieces fit |
| `ADDING_A_GAME.md` | the procedure for a new game |
| `RESOLUTION.md` | how the scaling, filter and menu machinery works, in detail |
| `DEVNOTES.md`, `TH11_DEVNOTES.md` | historical, from the original per-game builds. Left alone: they describe a build that no longer exists, and rewriting them would destroy the record of how the sub-stepping design was arrived at. |

This file is the findings — the reasoning, the wrong turns, and the things that will bite again.

---

## 1. What exists now

Three games are recognised. TH11 and TH12 are fully supported. TH10 is identified and then
deliberately left alone; §7 says why.

Everything about the picture is one implementation shared by every game, and none of it uses a
game address: arbitrary window resizing with letterboxing, three scaling modes, borderless
fullscreen, a multi-pass filter chain with four bundled upscalers, a Dear ImGui menu covering
every setting, the screenshot fix, the crash reporter, and the guards against other patches.

The high frame rate is the opposite: almost all of it is per-game and needs addresses.

That split is the single most useful structural fact about this codebase, and it is worth
defending. It is why TH11 got the entire feature set with no code change, and why a game whose
engine has not been worked out can still have all of it.

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

## 4. Bugs met, and what they taught

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
not overlap ours — all 60 frozen signatures checked against all 16 of its TH12 addresses — so
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

### Finding the two newest per-game fields

Both were found for TH11 and TH10 by pattern rather than by full analysis, and the method
generalises.

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

## 7. TH10, and why it is provisional

Verified: identity (40 signatures byte-identical in `th10.exe` and `th10j.exe`), five vpatch
conflict sites, the screenshot routine, and the whole shader chain compiling once the compiler
probe picks a capable `d3dx9`.

Not working: the game faults at `0x42b1e0` shortly after the first frames are presented, where
vanilla TH10 in the same rig does not. Ruled out, each by removing it and reproducing the fault
unchanged — the screenshot stub, Direct3D 9Ex and its texture conversion, and the sub-tick input
hook. What remains is the device redirect itself: the render target handed to the game in place
of its back buffer, which TH10 evidently uses in some way TH11 and TH12 do not. The faulting
instruction reads through EBX at the entry of a function near code that writes a `"TH10"` file
header, so the next step is to find that function's caller and see what it expects to be holding.

**Full support is not a matter of filling in more addresses.** TH10 predates the single
game-speed float that TH11 and TH12 each write a literal 1.0 into at 22 sites, which is what the
whole sub-stepping design hangs off. TH10's top three candidate globals take 12, 12 and 10
writes. Its speed model has to be worked out on its own terms before any of that design applies,
and `install_sites` is per-game hand-written code on top of that.

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

1. **TH10's fault at `0x42b1e0`** (§7). Everything else about TH10 is done.
2. **The alt-tab input fix** vpatch has and we do not — a foreground check on the DirectInput
   path. Cheap, and it affects both supported games.
3. **`ReplaySlowFPS`** — slow-motion replay on a held key. The tick-rate machinery makes this
   nearly free.
4. **The `UI_*` settings live in four places** — the enum, both switches and the save function,
   with a `default:` that stops the compiler noticing an omission. A table of
   `{id, name, section, &cfg.field}` would collapse all four and the INI read as well, which is
   currently a fifth. Nothing is inconsistent today; it is a drift risk, not a bug.
5. **`tools/embed_shaders.py` restates the pass-splitting rule** that `shader_parse.h` owns,
   because the build step is Python and the runtime is C. The checker tool includes the real
   header, so the two implementations that matter cannot disagree.
6. **TH13 and beyond.** TH13 is closer to TH12 than TH10 is to TH11, so the speed-float pattern
   in §6 is worth trying first — one `fstp` target dominating at ~22 sites means the existing
   sub-stepping design applies.
