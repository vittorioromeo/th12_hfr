# Filter shaders

Any `.hlsl` file in this folder is offered as a filter, under its file name, and replaces a
built-in filter of the same name. The DLL carries its own copy of the files listed below, so
the folder is only needed to add filters or to edit one without rebuilding.

## Writing a filter

The runtime prepends this and compiles for `ps_3_0` (or the best profile the device reports)
using the `d3dx9` the game already ships. Do not redeclare any of it:

```hlsl
sampler2D Source : register(s0);
float4 SourceSize : register(c0);   // x=width, y=height, z=1/width, w=1/height  of the input
float4 TargetSize : register(c1);   // x=width, y=height, z=1/width, w=1/height  of the output
```

Define exactly:

```hlsl
float4 main(float2 uv : TEXCOORD0) : COLOR0 { ... }
```

`uv` arrives at output-pixel centres, so for a target that is an exact multiple of the source,
`frac(uv * SourceSize.xy)` identifies the sub-pixel.

A filter with a fixed magnification declares it:

```hlsl
//! scale 2
```

It then renders into a target exactly that many times the game's resolution, and the result is
scaled to fit afterwards. Without the directive the filter renders straight into the
destination rectangle at whatever size the window happens to be.

Compilation errors go to `touhou_hfr.log`, and a filter that fails falls back to sharp
bilinear rather than taking the game down. `tools/shader_check.c` compiles a shader the same
way outside the game, which is quicker than restarting it.

## What is bundled, and under what licence

| File | Author | Licence |
| --- | --- | --- |
| `mmpx.hlsl` | Morgan McGuire and Mara Gagiu; slang adaptation by hunterk | MIT |
| `xbr-lv2.hlsl` | Hyllian (Sérgio Gouveia de Barros) | MIT |

Both were ported to Direct3D 9 HLSL for this project from the `.slang` versions in
[libretro/slang-shaders](https://github.com/libretro/slang-shaders); the full licence text and
attribution are kept at the top of each file, which is a condition of both licences.

**xBRZ is deliberately not bundled.** It is GPLv3, and so are its shader ports — the libretro
`xbrz-freescale` file opens with Hyllian's MIT block but carries a GPLv3 block from Zenju
underneath, whose linking exception names only MAME, FreeFileSync and Snes9x. Bundling it
would put this whole project under GPLv3. If you want it, drop your own copy in this folder;
nothing stops a filter you add locally from being under any licence you like.
