# Filter shaders

Any `.hlsl` file in this folder is offered as a filter, under its file name, and replaces a
built-in filter of the same name. The DLL carries its own copy of the files listed below, so
the folder is only needed to add filters or to edit one without rebuilding.

## Writing a filter

The runtime prepends this and compiles for `ps_3_0` (or the best profile the device reports)
using the `d3dx9` the game already ships. Do not redeclare any of it:

```hlsl
sampler2D Source : register(s0);      // the previous pass; the game's image, in pass 0
sampler2D Original : register(s1);    // always the game's own 640x480 image
sampler2D Pass0 : register(s2);       // an earlier pass's output, for a pass that reaches
sampler2D Pass1 : register(s3);       // further back than the one immediately before it
sampler2D Pass2 : register(s4);
sampler2D Pass3 : register(s5);
sampler2D Pass4 : register(s6);
sampler2D Pass5 : register(s7);
float4 SourceSize : register(c0);     // x=width, y=height, z=1/width, w=1/height of the input
float4 TargetSize : register(c1);     // the same, for what this pass renders into
float4 OriginalSize : register(c2);   // the same, for the game's own image
```

Define exactly:

```hlsl
float4 main(float2 uv : TEXCOORD0) : COLOR0 { ... }
```

`uv` arrives at output-pixel centres, so for a target that is an exact multiple of the source,
`frac(uv * SourceSize.xy)` identifies the sub-pixel.

Every intermediate is sampled with point filtering and clamped addressing, so a filter that
packs data into a pixel and reads it back gets exactly what it wrote. Intermediates always
carry an alpha channel, so all four components are yours to use.

### Directives

Directives are `//!` followed by a word, one per line, anywhere in the file:

```hlsl
//! pass          begins a pass; everything above the first one is a header shared by all passes
//! scale 2       this pass's output is 2x the size of its input (1 by default)
//! float         this pass writes values outside 0..1, so it needs a float target
```

A file with no directives at all is a single pass rendered straight into the destination
rectangle at whatever size the window happens to be — a free-scale filter, like `xbr-lv2`.

A single pass with `//! scale N` renders into a target N times the game's resolution, and the
result is scaled to fit afterwards — like `mmpx`.

Several passes run in order, each into its own target, and the last one is scaled to fit. The
shared header is compiled into every pass, so an algorithm's passes can share their helper
functions instead of repeating them; `super-xbr.hlsl` is the small worked example and
`scalefx.hlsl` the large one. At most 8 passes, and at most 16x overall.

When the chain ends up larger than the window — ScaleFX's 3x image in a 1.5x window, say —
the result is averaged down rather than sampled at one point per destination pixel. Taking a
single sample would keep two source pixels out of every three and turn every filtered edge
into a dotted line.

Compilation errors go to `touhou_hfr.log`, and a filter that fails falls back to sharp
bilinear rather than taking the game down. `tools/shader_check.c` compiles a shader the same
way outside the game, pass by pass, which is quicker than restarting it — and it includes the
runtime's own header, so it cannot drift from what the game does.

## What is bundled, and under what licence

| File | Author | Licence | Shape |
| --- | --- | --- | --- |
| `mmpx.hlsl` | Morgan McGuire and Mara Gagiu; slang adaptation by hunterk | MIT | 1 pass, 2x |
| `xbr-lv2.hlsl` | Hyllian (Sérgio Gouveia de Barros) | MIT | 1 pass, free scale |
| `super-xbr.hlsl` | Hyllian | MIT | 3 passes, 2x |
| `scalefx.hlsl` | Sp00kyFox | MIT | 5 passes, 3x |

All were ported to Direct3D 9 HLSL for this project — MMPX and xBR-lv2 from the `.slang`
versions in [libretro/slang-shaders](https://github.com/libretro/slang-shaders), Super-xBR and
ScaleFX from the `.cg` versions in
[libretro/common-shaders](https://github.com/libretro/common-shaders). The full licence text
and attribution are kept at the top of each file, which is a condition of every one of these
licences. What the ports changed is noted there too: the sampling offsets move from the vertex
shader into the pixel shader, the passes of an algorithm share one header, and the runtime
tunables are fixed at the defaults from the originals' `#pragma parameter` lines.

Of the four, **ScaleFX is the one designed for pixel art** — its own header puts it plainly:
the filtered picture consists only of colours present in the original. That is why it does not
ring or halo on sprite edges the way an upscaler trained on photographic or video content
does. Super-xBR is smoother and cheaper, and rounds hard corners more.

## What is deliberately not bundled

These are all worth having and none of them can be shipped here, because this project is
permissively licensed and they are not. Nothing stops you dropping your own copy of any of
them into this folder — a filter you add locally can be under any licence you like.

| Wanted | Why not |
| --- | --- |
| **xBRZ** | GPLv3, and so are its shader ports. The libretro `xbrz-freescale` file opens with Hyllian's MIT block but carries a GPLv3 block from Zenju underneath, whose linking exception names only MAME, FreeFileSync and Snes9x. |
| **hqx** (hq2x/hq3x/hq4x) | LGPL-2.1 or later in every implementation whose provenance can be traced: Maxim Stepin's original, Cameron Zemek's and Jules Blok's ports, the libretro Cg shaders, MAME's bgfx port, and hqxSharp. Two forks do ship permissive licence files — brunexgeek/hqx claims Apache-2.0 and janert/pixelscalers claims MIT — but each documents deriving from LGPL or GPL sources, so neither relicence looks like one its author had the right to make. Technically it would have been the cheapest of the lot: two passes and a 5 KB lookup table. |
| **NNEDI3** | GPLv2 for the reference implementation, LGPLv3 for the mpv GLSL prescalers, GPL-3.0 for Magpie's HLSL. The trained weights themselves descend from the GPL original, which is why mpv gates it behind `--enable-gpl3`. |
| **FSRCNNX** | LGPL-3.0 for the shader, GPL-3.0 for the training code. |

Two of those are also a poor fit for this game even setting the licence aside. NNEDI3 and
FSRCNNX — and Anime4K, which *is* MIT — are trained on 1080p anime video, to repair line art
that has been softened and damaged by compression. A 640x480 sprite has hard one-pixel edges,
deliberate dithering and no compression artifacts, so those models tend to soften and ring
exactly where this game wants to stay crisp. They are also luma-only, which leaves sprite
colour edges untouched. Anime4K's cheapest useful preset is around 25 passes; the runtime's
limit is 8, and raising it would not make the result suit the content.

**Avoid copying from [Magpie](https://github.com/Blinue/Magpie)** even where the upstream
algorithm is permissive: Magpie is GPL-3.0 as a whole and its effect files carry no separate
licence header, so its HLSL port of an MIT shader is still GPL-3.0. Port from the upstream
GLSL or Cg instead, as the four bundled filters were.
