> Written while this work was done on TH11 and TH12. The scaler, filter chain, window
> management and menu described here are shared by all four x86 games today; addresses
> and "both games" phrasing date from that period. New Classic's D3D11 backend has none
> of it — see [TH06NC_VS_TH10_13.md](TH06NC_VS_TH10_13.md).

# Output resolution, scaling and the in-game menu

These notes cover the second strand of work on the patch: getting the game out of its three
fixed window sizes and its 640x480 fullscreen mode switch, and giving the upscale a filter we
choose rather than one the driver picks. They assume the engine background in `DEVNOTES.md`.

Addresses below are TH12 v1.00b, but almost nothing here is game-specific: the work is in the
Direct3D and Win32 layers, which TH11 shares, and neither needed a new profile field.

## 1. What the game actually does

Reverse engineering the window and device setup turned up one fact that shaped everything
else: **the game always renders to a 640x480 back buffer.**

`FUN_450cc0` fills a `D3DPRESENT_PARAMETERS` on the stack and copies it to the global at
`0x4ce9dc`; `BackBufferWidth` and `BackBufferHeight` are set to 640x480 there and are never
written again. The window-mode global at `0x4cf428` holds a two-bit size index in bits 2-3:

| `(0x4cf428 >> 2) & 3` | result |
| --- | --- |
| 0 | exclusive fullscreen, display mode switched to 640x480 |
| 1 | windowed 640x480 |
| 2 | windowed 960x720 |
| 3 | windowed 1280x960 |

Switching between them only calls `SetWindowPos` with a different window size and then
`Reset`s the device with the same 640x480 parameters. Everything above 640x480 is therefore
Direct3D stretching the back buffer to the client area during `Present`, with whatever filter
the driver happens to use — which is the whole of "scaled modes look bad". Exclusive
fullscreen is worse: it drops the display to 640x480 and hands the upscale to the monitor.

The good news is that this makes the engine completely resolution-independent internally. The
projection matrix is built with a hard-coded 4:3 aspect (`D3DXMatrixPerspectiveFovLH` with
`0x3faaaaab`), and every viewport it sets is relative to its own 640x480 surface.

Other things worth knowing before touching the device:

* The game uses **fixed-function** rendering throughout: `DrawPrimitiveUP`, `SetFVF`,
  `SetTextureStageState`. The only `SetVertexShader` call passes `NULL`. No pixel shaders.
* It calls `GetBackBuffer` twice: once in the screenshot path (`0x42f000` region, which
  allocates `640*480*3` bytes for a 24-bit BMP from its own present parameters) and once in
  the ANM "capture the screen into a sprite" path (`0x4609fd` region, which
  `D3DXLoadSurfaceFromSurface`s a sub-rectangle out of it). Both would break if the real back
  buffer stopped being 640x480 — the first with a buffer overrun.
* It never calls `SetDepthStencilSurface` or `GetDepthStencilSurface`, so whatever depth
  surface is bound stays bound.
* The `SetRenderTarget` calls at `0x42f0xx` are all guarded by `0x4cea94`, which is never
  assigned anywhere in the binary. That render-to-texture effect path is dead code inherited
  from an earlier game.
* The window class is `BASE`, the procedure is `FUN_44fe00` and the handle is at `0x4cf3f0`.
  The procedure handles very little: `WM_ACTIVATEAPP`, `WM_CLOSE`, `WM_ERASEBKGND`,
  `WM_SETCURSOR`, two `WM_SYSCOMMAND` filters, and a `WM_SIZE` case that turns a maximise
  into a request for exclusive fullscreen. There is no `WM_SIZING` or resize handling at all,
  and the window style (`0x10cb0000` windowed) has no `WS_THICKFRAME`.

## 2. Design

### 2.1 Redirect the game, resize the swap chain

The swap chain is given the window's size, and the game is handed a 640x480 render target to
draw into. At `Present` time that render target is drawn into the real back buffer as a
textured quad, scaled and filtered as configured.

The game is never told. `hook_CreateDevice` and `hook_Reset` copy the present parameters and
change the copy, so the game's globals keep saying 640x480, and `GetBackBuffer` is hooked to
return our render target's surface. Both of the paths in section 1 therefore still see exactly
the surface and size they expect: screenshots come out 640x480, and the screen-capture sprite
samples the right pixels.

We create our own 640x480 depth stencil and bind it once, rather than relying on the device's
automatic one — that one follows the swap chain, and a window smaller than 640x480 would make
it too small for the render target. Since the game never touches the depth stencil binding,
ours simply stays.

### 2.2 Scaling geometry

`scale_rect()` places a `sw x sh` image inside a `dw x dh` target three ways: stretch, aspect
fit, and integer. Integer picks the largest whole multiple that fits and centres it, falling
back to aspect fit when the target is too small for even 1:1. It is pure integer arithmetic
and is checked exhaustively in the native harness rather than by looking at the screen.

The default filter is **sharp bilinear**: point-magnify to the smallest whole multiple that
covers the destination, then let bilinear handle only the remainder. That keeps every pixel
edge crisp and every pixel the same size, without the uneven pixel widths that plain nearest
produces at non-integer scales. It needs no shader, so it is available even when shader
filters are not.

### 2.3 Filters as shaders

Filters beyond the three built-in sampler behaviours are Direct3D 9 pixel shaders compiled at
run time by the `d3dx9` the game already ships (`d3dx9_40.dll` for TH12), so no bytecode has
to be built or shipped, and a filter can be edited without rebuilding anything. Any `.hlsl`
in the game's `shaders/` folder is offered under its file name and overrides a built-in of the
same name; the DLL also carries its own copies. `shaders/README.md` documents the contract.

A filter can declare `//! scale N`, which renders it into a target exactly N times the game's
resolution before the result is placed; otherwise it is the shader of the final draw and sees
the destination size. MMPX is the former, xBR-lv2 the latter.

Two details that are easy to get wrong:

* **Half-pixel offset.** The quad is drawn with the Direct3D 9 half-pixel shift, so `uv`
  arrives at output-pixel centres and `frac(uv * SourceSize.xy)` identifies the sub-pixel.
  Fixed-scale filters like MMPX depend on that to pick the right quadrant; without it MMPX
  degrades into an offset blur.
* **`ps_3_0` cannot be used with the fixed-function vertex pipeline.** Shader filters are
  therefore drawn through a pass-through vertex shader with clip-space vertices, while the
  built-in filters keep using pre-transformed `D3DFVF_XYZRHW` ones. The half-pixel shift is
  applied before the clip-space conversion so both paths rasterise identically.

### 2.3.1 Passes

The published upscalers worth having are not single shaders. Super-xBR is three passes,
ScaleFX is five, and a pass generally needs to read more than the one before it: Super-xBR's
second pass reads the game's own image alongside its first pass, and ScaleFX's last pass
reaches back five passes to the original. So a filter file is now a shared header followed by
`//! pass` blocks, each with its own `//! scale` and, if it writes values outside 0..1, its
own `//! float`. Every pass sees `Source` (the pass before it), `Original`, and `Pass0`..
`Pass5`. A file with no directives is still one free-scale pass, so nothing written before
this changed.

The rules live in `src/core/shader_parse.h`, which both the runtime and `tools/shader_check.c`
include. They used to be written out twice, and the copy in the tool had already drifted from
the runtime -- it was still checking shaders against a two-sampler prologue that no longer
existed. One definition means the tool cannot be wrong about what the game accepts.

Two things had to be got right before the ported filters looked like themselves:

**Intermediates always carry alpha.** They used to be created in the back buffer's format,
which for this game windowed is `X8R8G8B8` -- no alpha channel at all. ScaleFX's first pass
writes an edge distance in each of four components, so a quarter of its data was being
dropped, and the later passes then chose the wrong neighbour along every edge. On screen that
is a dotted outline tracing every sprite, which reads as "the filter is broken" rather than
"the target has three channels". Intermediates are now `A8R8G8B8` whatever the back buffer is;
the extra channel costs nothing.

**A chain that overshoots the window is averaged down, not sampled.** A fixed-scale filter
magnifies by a whole number, usually more than the window asks for -- ScaleFX's 3x image in a
1.5x window. One bilinear tap per destination pixel keeps two source pixels out of every
three, which also produces a dotted edge, from the resample rather than the filter. Four
bilinear taps at the quarter points of the destination pixel's footprint average it instead.
This applies to every fixed-scale filter, not just the new ones.

The lesson from both: a filter that is subtly wrong looks broken in the same way a filter that
is completely wrong does, so "it renders something" is not evidence the port is right. What
settled each of these was changing one thing and comparing the same frame.

### 2.3.2 What is not bundled, and why

Of the six upscalers asked for, three can be shipped and three cannot. `shaders/README.md`
carries the detail, with the reasoning for each; the short version is that hqx is LGPL-2.1 in
every implementation whose provenance can be traced (including two forks that ship permissive
licence files while documenting derivation from copyleft sources), NNEDI3 is GPL/LGPL down to
its trained weights, and FSRCNNX is LGPL-3.0. Anime4K is genuinely MIT but its cheapest useful
preset is around 25 passes and it is trained to repair compression-damaged anime video, which
is close to the opposite of what a 640x480 sprite needs.

Because a filter can be dropped into `shaders/` at run time, none of this stops anyone using
those algorithms -- it only stops this project distributing them.

### 2.4 Window management

The window gets `WS_THICKFRAME`, a minimum size, and aspect snapping while dragging (the edge
under the cursor is the one that survives; `snap_client()` is unit tested). The device is
reset on `WM_EXITSIZEMOVE` rather than on every `WM_SIZE`, so dragging stays smooth.

The game rewrites the window style and size whenever it resets the device, so rather than
fighting it message by message the wanted geometry is re-asserted once per frame from the
frame hook. Anything the game changes is undone by the next frame at the latest; the cost is
that a mode switch can show one frame at the old geometry.

"Fullscreen" becomes a borderless window covering the monitor at the desktop resolution: the
present parameters are flipped to windowed with the monitor's size, and the frame-by-frame
enforcement moves the window to cover the monitor. The game still believes it is fullscreen,
which conveniently keeps its own cursor hiding correct. The original behaviour is one INI key
away.

The maximise message is swallowed rather than forwarded, because the game's own handler turns
it into a request for exclusive fullscreen; maximising now fills the monitor and letterboxes.

### 2.5 Presentation, and why the device is never reset

The obvious way to change the output size is to resize the device's own swap chain, which
means `Reset`. **The game cannot survive a reset**, and the reason is worth spelling out
because it is not obvious and it bites in more than one place:

* Direct3D 9Ex has no managed pool, so the patch converts every `D3DPOOL_MANAGED` texture the
  game creates into `D3DPOOL_DEFAULT`. That conversion is required for 9Ex, and 9Ex is what
  gives us `SetMaximumFrameLatency`.
* Default-pool contents are undefined after a reset. Managed ones would have been restored
  from their system-memory copy automatically, which is what the game was written for.
* The game has no code to reload them. Its pre-reset release routine, `FUN_00431700`, only
  releases `0x4cea94`, `0x4cea98` and `0x4cea9c` — three pointers that are **never assigned
  anywhere in this build**, left over from an earlier game. Its post-reset routine
  `FUN_00431630` re-acquires surface levels from textures it assumes still hold their pixels.

The symptom is a white screen: every sprite samples undefined texture memory. It applies to
the game's own resets too, so Alt+Enter and the resolution-cycle key were already broken by
the 9Ex conversion before any of this work.

So the device is never reset. Presentation goes through an **additional swap chain** created
with `CreateAdditionalSwapChain`, sized to the client area, on the game's window. The device's
own chain stays at 640x480 and is simply never presented. Resizing releases and recreates our
chain, which touches nothing the game owns. `Reset` calls from the game are answered without
resetting anything: the window work it does around them still happens, and the resulting
`WM_SIZE` rebuilds our chain.

One consequence: the device is kept windowed even when the game asks for exclusive fullscreen,
because an exclusive-fullscreen device cannot carry the windowed chain we present through. The
640x480 mode switch is what this work replaces anyway, so `fullscreen_mode` now chooses
between "borderless over the monitor" and "leave the window where the game puts it", not
between borderless and a real mode change.

### 2.5.0 Anything handed to the game as its back buffer must be lockable

`GetBackBuffer` is hooked so the game sees its own 640x480 surface rather than the real one
(2.5), but *which* surface matters more than its size. The screenshot routine `FUN_0042fca0`
locks what it is given and writes a 640x480x3 BMP straight out of the locked rectangle,
**without checking whether the lock succeeded**. A render target in the default pool cannot be
locked, so returning our render target made that routine write through an uninitialised
pointer -- an access violation inside the game, at a site with no obvious connection to
anything the patch does.

The hook therefore returns the render target to every caller **except** the screenshot, which
gets a lockable system-memory copy. A stub around the game's screenshot routine (its single
call site) sets a flag, so the hook knows which caller it is talking to.

Getting there took three attempts, and the two failures are worth recording because both
looked reasonable:

1. *Return a system-memory copy to everyone, refreshed with `GetRenderTargetData`.* Correct,
   but `FUN_0044f4b0` runs once per frame servicing pending screen captures, so this put a
   GPU-to-CPU readback on a per-frame path.
2. *Return a lockable render target to everyone, refreshed with `StretchRect`.* No readback,
   but the game still hung -- on the **first** capture, with the request counter reading 1.

The counter is what separated the two theories: a per-frame cost would have shown a rising
count. One request followed by silence meant the very first copy wedged the device. Both
attempts copied *from* `g_src_surf` while it was still bound as the device's render target,
which is what the driver would not tolerate. The screenshot path now unbinds, copies, and
puts the target, depth stencil and viewport back.

The general lessons: **find out how often the engine calls a hook before putting work in it**
(two call sites in the disassembly looked rare; one is per frame), and **do not read from a
surface that is currently bound as the render target**.

### 2.5.1 When a d3d9 wrapper is in the way

A `d3d9.dll` dropped in the game folder — PivotDX9, dgVoodoo, a d3d8-to-9 shim — replaces the
real one for the whole process, and such a wrapper only ever expects the one implicit swap
chain. PivotDX9 in particular accepts `CreateAdditionalSwapChain`, hands back a swap chain
that looks fine, and then **never returns from `Present` on it**. The game hangs on its very
first present, during startup, before the main loop is ever entered.

So the presentation path is chosen from what `d3d9.dll` actually resolves to. If the loaded
module is not the one in the system directory, we present through the game's own chain and
resize it with `Reset` — which in turn forces Direct3D 9Ex off, because 9Ex is what pushes the
game's textures into the default pool where a reset destroys them (2.5). That costs
`SetMaximumFrameLatency`, and the log says so.

`video.own_present` overrides the choice if the detection is ever wrong: `1` always uses our
chain, `0` always the game's, `-1` (the default) decides as above.

If our chain cannot be created at all, we fall back to the game's; if that also fails, the
scaler disables itself and the game presents exactly as it always did — no filters and no
resizing, but nothing broken. A failed `Reset` rebuilds the render target against the size the
chain still has, so the game is never left drawing into a surface we have released.

### 2.6 The menu

The menu key is deliberately **F11**. The engine reads F10, Insert and Home for its own
purposes -- Insert reaches the snapshot path on at least some configurations -- and a menu key
that also triggers a screenshot is a poor default.

Opening the menu must not depend on how the game took the keyboard. It reads through
DirectInput or `GetKeyboardState` rather than the message queue, and DirectInput can stop key
messages reaching the window at all. Both the window procedure and a `GetAsyncKeyState` poll
therefore raise a *request*, and the frame hook acts on it once per frame; when both toggled
directly, a press arriving by both routes cancelled itself out and the menu never opened.

### 2.6.1 Layout

Dear ImGui, drawn into the real back buffer after the game's image has been placed in it, so
the menu renders at the display's resolution instead of being magnified with the game.

It is mouse-driven on purpose. The game reads the keyboard through `GetKeyboardState` and
DirectInput rather than the message queue, so any keystroke the menu consumed would still
reach the player. Restricting the menu to sliders, combo boxes and check boxes means the game
can keep running underneath with its input untouched. Only mouse messages ImGui actually wants
are swallowed, plus `WM_SETCURSOR` while the menu is open, since the game hides the cursor.

`src/ui/ui_api.h` is the entire interface between the C runtime and the C++ menu. The menu
never sees the runtime's globals, and a build can leave it out — the test harness defines
`HFR_NO_UI` and gets no-ops.

### 2.6.2 The menu that could not be brought back

Reported twice, and the second report had the detail that mattered: the menu was open when
the game changed resolution, and after that it was invisible and reopening did nothing.

The window was keeping a position from a viewport that no longer existed. Open centred on a
1817x1156 surface, the window sits around (900, 580); the game then switches to 640x480 and
that position is off the edge. ImGui does clamp windows, but only enough to keep the title bar
reachable, which still leaves nearly all of a 620-wide window outside a 640-wide surface.

The reason reopening did not rescue it is the interesting half. The placement used
`ImGuiCond_Appearing`, chosen precisely so that every reopen would put the window somewhere
visible. It never fires: while the menu is hidden this file returns before `ImGui::NewFrame()`,
so ImGui's frame counter does not advance, and "appearing" is defined as the window not having
been submitted for at least two frames. The counter is frozen, the window was submitted on the
last frame that happened, and so it is never new. A condition that looked like the fix was
doing nothing at all -- and nothing failed to say so.

Now the menu tracks its own placement: opening it sets a flag that centres it with
`ImGuiCond_Always` for one frame, and a change in surface size pushes the whole window back
inside the surface rather than just its title bar.

### 2.6.3 When a d3d9 wrapper is presenting

TH11 in the reporter's install still had PivotDX9's `d3d9.dll` active, where TH12's had been
renamed to `d3d9.dllx`. That is the whole explanation for "the shaders work but the scaling
modes and borderless fullscreen do not": with a wrapper in the way the patch presents through
the game's own chain, so the scaling geometry is computed against the game's back buffer and
the wrapper then stretches that to the window however it likes. Filters run before that point
and are unaffected.

The patch cannot fix this from inside -- the wrapper owns the last step -- so it says so
instead: three lines in the log naming the consequence and the remedy, the two controls that
cannot take effect greyed out in the menu with the reason above them, and a dialog at startup.
Offering a scaling mode that silently does nothing is worse than not offering it.

The dialog is deliberately not the vpatch one. vpatch is a conflict: two frame schedulers, one
of them wrong, so that refuses to install. A wrapper is not -- the patch installs, the game
runs, the filters work -- so this only warns, and only when it is actually costing something.
Someone who stretches to fill and never uses borderless fullscreen loses nothing to a wrapper
and gets no dialog, just a line in the log; `video.warn_wrapper=0` silences it for anyone who
keeps the wrapper on purpose. Both share one piece of dialog machinery and show at most one
box a run, because two warnings stacked over a game nobody has looked at yet is worse than
one. Verified in all three states: wrapper with affected settings, wrapper with unaffected
settings, and no wrapper.

## 2.7 Making failures speak

Two mechanisms exist purely so that a fault in a windowed process is not silent, because the
log is the only thing a tester can send back:

* `IM_ASSERT` is routed to the log and switches the menu off rather than calling `assert()`,
  which aborts with nothing written anywhere.
* A vectored exception handler reports the first fatal exception with its module and offset.
  This is what identified the screenshot crash in 2.5.0, after two rounds of guessing had
  failed to.

## 2.8 When another patch is in the same game

vpatch (VsyncPatch, by swmpLV/75E) replaces the game's frame limiter and calls Present on its
own schedule. So does this patch. Their sites do not overlap ours -- I checked all 69 frozen
signatures against all 16 of vpatch's th12 patch addresses -- so nothing in the identity check
notices, both installs report success, and the game ends up running at whichever scheduler got
the last word, with no clue anywhere as to why.

The timing is the whole difficulty, and it is worth recording because it is the opposite of
what it looks like. vpatch launches the game suspended and injects itself with
CreateRemoteThread + LoadLibrary, so it seems obvious that it must get there first. It does
not: the injected thread runs loader initialisation before its LoadLibrary call, and loader
initialisation is what loads this DLL, because the game imports it. Traced under Wine with
`WINEDEBUG=+loaddll`, the game process loads DINPUT8.dll and only then vpatch_th12.dll. So at
the moment this patch installs, the game is still clean and there is nothing to detect. The
first version of this guard checked only at install time and reported nothing at all when
launched through vpatch.exe, which is how the ordering came to light.

So there are two checks, in `src/core/conflict.c`:

- **At install**, which catches an executable already modified on disk and anything that did
  get in earlier: refuse outright, log, and say so in a dialog.
- **On the first frame**, by which point everything that is going to load has loaded: far too
  late to refuse, so say plainly what is happening rather than leave someone wondering why a
  patch they installed appears to do nothing.

Detection is two independent tests. A module whose name contains `vpatch`, and the game's own
frame-loop code no longer being the code it shipped with -- four sites recorded in
`src/games/th12_conflicts.h` with the bytes a clean executable has there, read from JP and
English builds, which agree at all four. The byte test is the more general of the two: it does
not care whose patch it is, only that something has taken the frame loop.

These sites are deliberately **not** part of identification. Identity is settled first, from
signatures no known patch touches, and only then are they checked, so a mismatch always means
"something else patched this game" and never "this is the wrong game" -- the test asserts that
a game with vpatch's jump written over each site is still identified as the game. TH11 has no
sites recorded, because the equivalent addresses have not been read out of a vpatch build for
it and guessing would either miss or accuse the innocent; the module test still covers it.

The dialog runs on a thread of its own. A message box called from `DllMain` would hold the
loader lock while it waited for the user; the thread cannot start until that lock is released,
which is exactly when showing one becomes safe.

Verified end to end under Wine: launching through `vpatch.exe` names `vpatch_th12.dll` and the
frame limiter at `0x4503f8`; launching normally with the vpatch files sitting in the same
folder reports nothing.

### 2.8.1 Is vpatch still needed?

No, for TH12. Its live features there are a frame limiter, its own Present scheduling, window
geometry, and an input fix; the first three are exactly this patch's territory and it does them
with a whole-number frame cap and a stretched 640x480 window. It does not change the render
resolution, whatever the guides say. Three things it does that this patch does not, recorded so
they are not lost: `BugFixGetDeviceState` (a foreground check on the DirectInput path, the fix
for input running away after alt-tab -- this patch calls ZUN's poll routine rather than
replacing it, so it inherits stock behaviour); `BugFixTh12Shadow`, a rev6-only one-line render
state change fixing UFO's Palanquin Ship shadow on Radeon and Intel; and `ReplaySlowFPS`,
slow-motion replay on Shift.

## 2.9 Both games, and the shape that makes a third cheap

Everything above was built against TH12 and then verified on TH11: the menu and all four of its
tabs, arbitrary resizing with letterboxing, the scaling modes, the multi-pass filters, the
startup hint, the screenshot stub and the conflict guard. The audit that mattered was a
negative one -- no address, game name or 640x480 assumption appears anywhere outside
`src/games/`, so the entire video, menu and filter path applied to TH11 with no code change.
Only two things had to be found per game, and `ADDING_A_GAME.md` records how.

Three things are worth recording from doing it, because each is a way to be confidently wrong:

**The compiler is whichever d3dx9 the game imports, and they differ.** TH12 imports
`d3dx9_40`, TH11 `d3dx9_37`. Super-xBR appeared to fail to compile on TH11 with
`E5017: Aborting due to not yet implemented feature`, which reads like an old-compiler
limitation and nearly earned a rewrite of the compiler selection. It is Wine's own incomplete
HLSL compiler: Wine prefers its builtin `d3dx9_NN` unless told otherwise, and the test rig had
no native copy. With the genuine `d3dx9_37.dll` in place all four filters compile on TH11, and
so they do on every real d3dx9 from 33 to 41. The selection was left alone.

**The game blacks out on its own.** TH11 and TH12 both go black about twenty seconds after
being left at the title screen, and a screenshot taken then is black too. Vanilla TH11 with no
patch loaded does exactly the same, so it is the games' idle demo under a software renderer,
not the patch. This was dismissed once as "demo mode" without checking, which is how it came
back later disguised as a screenshot bug.

**A timed-out test leaves its state behind.** A resize test that appeared to show stretching
instead of letterboxing had actually run vanilla, because an earlier command had timed out
before restoring the DLL it renamed. Check the patch is loaded before believing what a test
says about it.

## 3. What is not done

* **The 3D stage still renders at 640x480.** The stage background is real 3D and would look
  better rendered at the output resolution, with only the 2D sprites upscaled. That means
  splitting the render target in two and reconciling the depth buffer between them, and the
  sprite layer would need its own pass. It is the obvious next step for image quality.
* **The HUD and text are upscaled with everything else**, because they are sprites in the same
  640x480 surface. Rendering them separately at native resolution has the same shape as the
  problem above.
* **No CRT or scanline filters** are bundled. They fit the existing shader interface; there
  are permissively licensed ones that could be ported.
* **`snap_aspect` assumes the game's aspect**, which is 4:3 for every supported game.
* **Exclusive fullscreen is gone**, for the reason in 2.5. Restoring it would mean teaching
  the patch to reload the game's textures itself, or keeping them in the managed pool by
  giving up Direct3D 9Ex and the frame-queue control with it.
* **Sharp bilinear costs fill rate at high refresh rates.** Its prepass renders the game at
  the next whole multiple every frame — at 1548x1161 that is a 1920x1440 intermediate, and at
  360 Hz that measurably eats into the frame budget. A single-pass shader that shapes the
  bilinear weights instead would do the same job for much less.

## 4. Verification

`test.sh` runs the native harness under Wine when no Windows host is available, which is how
the geometry was developed. The scaling and snapping tests are exhaustive over the ranges that
matter rather than spot checks.

The game itself runs under Wine on a virtual X display with llvmpipe, which is how the
PivotDX9 hang was found and A/B tested: with the wrapper in the folder the log stops at the
first present, without it the game runs indefinitely. Software rendering is slow enough that
the game takes ~20 s to reach device creation, and anything else touching the X display while
it runs will starve it, but for questions of the form "does this code path get reached" it is
far quicker than a round trip to a real machine.

`tools/shader_check.c` compiles a shader exactly as the runtime does, against a real
`d3dx9_40.dll`, so a bundled or user shader can be checked without starting the game. Both
bundled filters were verified against the genuine Microsoft compiler at `ps_3_0`.

None of that says anything about how the result looks, which needs a human at a screen. The
things most worth checking there are: screenshots and the pause-menu background (both go
through the redirected back buffer); MMPX at an exact 2x window, where a half-pixel error
would show as softness; and the borderless transition, which is the one place a frame can be
displayed at the wrong geometry.
