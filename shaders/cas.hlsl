//! post
/*
   CAS -- AMD FidelityFX Contrast Adaptive Sharpening, as a post-process on the upscaled image

   Copyright (c) 2019 Advanced Micro Devices, Inc. All rights reserved.
   License: MIT

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.

   Ported to Direct3D 9 HLSL (ps_3_0) for this project from the sharpen-only path of
   ffx_cas.h (CasFilter with CAS_BETTER_DIAGONALS). It runs after the filter has
   produced the final image, at the window's resolution, so what it sharpens is the
   resampled picture -- the soft edges a bilinear or sharp-bilinear resample leaves,
   or the smoothing an xBR-style filter does -- and never the game's own pixels.

   CAS raises local contrast in proportion to how much room the 3x3 neighbourhood has
   before it clips, so flat areas and already-hard edges are left alone and the amount
   of ringing is bounded. Params.x is the menu's strength, 0..1: it maps onto the
   sharpness AMD's setup function takes, 0 being the mildest CAS and 1 the strongest.
*/

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 px = SourceSize.zw;
    //  a b c
    //  d e f
    //  g h i
    float3 a = tex2D(Source, uv + float2(-px.x, -px.y)).rgb;
    float3 b = tex2D(Source, uv + float2( 0.0,  -px.y)).rgb;
    float3 c = tex2D(Source, uv + float2( px.x, -px.y)).rgb;
    float3 d = tex2D(Source, uv + float2(-px.x,  0.0 )).rgb;
    float3 e = tex2D(Source, uv).rgb;
    float3 f = tex2D(Source, uv + float2( px.x,  0.0 )).rgb;
    float3 g = tex2D(Source, uv + float2(-px.x,  px.y)).rgb;
    float3 h = tex2D(Source, uv + float2( 0.0,   px.y)).rgb;
    float3 i = tex2D(Source, uv + float2( px.x,  px.y)).rgb;

    // Soft min and max: the cross, plus the full 3x3 at half weight.
    //  a b c             b
    //  d e f * 0.5  +  d e f * 0.5
    //  g h i             h
    // These are 2x bigger (the extra multiply is factored out).
    float3 mn = min(min(min(d, e), min(f, b)), h);
    float3 mn2 = min(mn, min(min(a, c), min(g, i)));
    mn += mn2;
    float3 mx = max(max(max(d, e), max(f, b)), h);
    float3 mx2 = max(mx, max(max(a, c), max(g, i)));
    mx += mx2;

    // Smooth minimum distance to the signal limit, divided by the smooth max.
    float3 rcpM = 1.0 / max(mx, 2.0 / 255.0);
    float3 amp = saturate(min(mn, 2.0 - mx) * rcpM);
    // Shaping amount of sharpening.
    amp = sqrt(amp);

    // Filter shape: a peak of -1/8 at sharpness 0 and -1/5 at sharpness 1.
    float peak = -1.0 / lerp(8.0, 5.0, saturate(Params.x));
    float3 w = amp * peak;

    // Filter: the weighted cross, normalised.
    float3 o = ((b + d + f + h) * w + e) / (1.0 + 4.0 * w);
    return float4(saturate(o), 1.0);
}
