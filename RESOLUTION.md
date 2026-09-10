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

### 2.5 Resizing without losing the game's resources

Resizing means `Reset`, and `Reset` normally requires every `D3DPOOL_DEFAULT` resource to be
released first — which we cannot do for the game's resources. `IDirect3DDevice9Ex::ResetEx`
does not: surfaces, textures and shaders survive it. Since the patch already creates the
device through Direct3D 9Ex by default for the frame-queue control, resizing is free.

With `d3d9ex=0` the plain `Reset` path still runs and still works, because the only extra
default-pool resources are ours and we release them around the reset — but the game's own
textures are managed in that configuration, so they survive too. It has had far less testing
than the 9Ex path.

### 2.6 The menu

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
* **`snap_aspect` assumes the game's aspect**, which is 4:3 for both supported games.

## 4. Verification

`test.sh` runs the native harness under Wine when no Windows host is available, which is how
the geometry was developed. The scaling and snapping tests are exhaustive over the ranges that
matter rather than spot checks.

`tools/shader_check.c` compiles a shader exactly as the runtime does, against a real
`d3dx9_40.dll`, so a bundled or user shader can be checked without starting the game. Both
bundled filters were verified against the genuine Microsoft compiler at `ps_3_0`.

None of that says anything about how the result looks, which needs a human at a screen. The
things most worth checking there are: screenshots and the pause-menu background (both go
through the redirected back buffer); MMPX at an exact 2x window, where a half-pixel error
would show as softness; and the borderless transition, which is the one place a frame can be
displayed at the wrong geometry.
