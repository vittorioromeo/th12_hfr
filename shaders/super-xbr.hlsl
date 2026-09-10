/* Super-xBR, by Hyllian.
 *
 * Ported to this patch's multi-pass filter format from the Cg version in
 * libretro/common-shaders (xbr/shaders/super-xbr/super-xbr-pass{0,1,2}.cg). The algorithm,
 * the weights and the structure are Hyllian's; what changed is that the sampling offsets are
 * computed in the pixel shader instead of the vertex shader, that the three passes share one
 * filter function instead of repeating it, and that the tunables are fixed at the defaults
 * from the original's #pragma parameter lines.
 *
 * Copyright (c) 2015 Hyllian - sergiogdb@gmail.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* Defaults from the original's "#pragma parameter" lines. */
#define XBR_EDGE_STR     2.0
#define XBR_WEIGHT       1.0
#define XBR_ANTI_RINGING 1.0

static const float3 Y = float3(0.2126, 0.7152, 0.0722);

float luma(float3 c) { return dot(c, Y); }
float df(float a, float b) { return abs(a - b); }

float3 min4(float3 a, float3 b, float3 c, float3 d) { return min(a, min(b, min(c, d))); }
float3 max4(float3 a, float3 b, float3 c, float3 d) { return max(a, max(b, max(c, d))); }

/* wp holds the six directional weights; passes 0 and 2 differ only in these.
     |P0|B |C |P1|
     |D |E |F |F4|
     |G |H |I |I4|
     |P2|H5|I5|P3|
*/
float d_wd(float4 wpa, float2 wpb,
           float b0, float b1, float c0, float c1, float c2,
           float d0, float d1, float d2, float d3,
           float e1, float e2, float e3, float f2, float f3) {
    return wpa.x * (df(c1, c2) + df(c1, c0) + df(e2, e1) + df(e2, e3))
         + wpa.y * (df(d2, d3) + df(d0, d1))
         + wpa.z * (df(d1, d3) + df(d0, d2))
         + wpa.w *  df(d1, d2)
         + wpb.x * (df(c0, c2) + df(e1, e3))
         + wpb.y * (df(b0, b1) + df(f2, f3));
}
float hv_wd(float4 wpa, float i1, float i2, float i3, float i4,
            float e1, float e2, float e3, float e4) {
    return wpa.w * (df(i1, i2) + df(i3, i4))
         + wpa.x * (df(i1, e1) + df(i2, e2) + df(i3, e3) + df(i4, e4));
}

/* The shared core of all three passes: given the sixteen neighbours, produce one colour.
   Passes 0 and 2 read them from a square grid; pass 1 reads them from a rotated one. */
float3 super_xbr(float4 wpa, float2 wpb, float w1s, float w2s,
                 float3 P0, float3 P1, float3 P2, float3 P3,
                 float3 B, float3 C, float3 H5, float3 I5,
                 float3 D, float3 F4, float3 G, float3 I4,
                 float3 E, float3 F, float3 H, float3 I) {
    float weight1 = XBR_WEIGHT * w1s / 10.0;
    float weight2 = XBR_WEIGHT * w2s / 10.0 / 2.0;

    float b = luma(B), c = luma(C), d = luma(D), e = luma(E);
    float f = luma(F), g = luma(G), h = luma(H), i = luma(I);
    float i4 = luma(I4), p0 = luma(P0);
    float i5 = luma(I5), p1 = luma(P1);
    float h5 = luma(H5), p2 = luma(P2);
    float f4 = luma(F4), p3 = luma(P3);

    /* Edgeness along the diagonals, and along the horizontal/vertical. */
    float d_edge  = d_wd(wpa, wpb, d, b, g, e, c, p2, h, f, p1, h5, i, f4, i5, i4)
                  - d_wd(wpa, wpb, c, f4, b, f, i4, p0, e, i, p3, d, h, i5, g, h5);
    float hv_edge = hv_wd(wpa, f, i, e, h, c, i5, b, h5)
                  - hv_wd(wpa, e, f, h, i, d, f4, g, i4);

    float limits = XBR_EDGE_STR + 0.000001;
    float edge_strength = smoothstep(0.0, limits, abs(d_edge));

    /* Two taps per direction. */
    float4 w1 = float4(-weight1, weight1 + 0.5,  weight1 + 0.5,  -weight1);
    float4 w2 = float4(-weight2, weight2 + 0.25, weight2 + 0.25, -weight2);

    float3 c1 = mul(w1, float4x3(P2,    H,    F,     P1));
    float3 c2 = mul(w1, float4x3(P0,    E,    I,     P3));
    float3 c3 = mul(w2, float4x3(D + G, E + H, F + I, F4 + I4));
    float3 c4 = mul(w2, float4x3(C + B, F + E, I + H, I5 + H5));

    /* Blend the strongest diagonal against the strongest horizontal/vertical. */
    float3 color = lerp(lerp(c1, c2, step(0.0, d_edge)),
                        lerp(c3, c4, step(0.0, hv_edge)), 1.0 - edge_strength);

    /* Anti-ringing: never leave the range of the four pixels actually being interpolated. */
    float3 ring = lerp((P2 - H) * (F - P1), (P0 - E) * (I - P3), step(0.0, d_edge));
    float3 lo = min4(E, F, H, I) + (1.0 - XBR_ANTI_RINGING) * ring;
    float3 hi = max4(E, F, H, I) - (1.0 - XBR_ANTI_RINGING) * ring;
    return clamp(color, lo, hi);
}

/* Passes 0 and 2 read the same square neighbourhood; only the weights differ. */
float4 square_pass(float2 uv, float4 wpa, float2 wpb) {
    float2 ps = SourceSize.zw;
    float dx = ps.x, dy = ps.y;
    float4 t1 = uv.xyxy + float4(-dx, -dy, 2.0 * dx, 2.0 * dy);
    float4 t2 = uv.xyxy + float4(0.0, -dy,       dx, 2.0 * dy);
    float4 t3 = uv.xyxy + float4(-dx, 0.0, 2.0 * dx,       dy);
    float4 t4 = uv.xyxy + float4(0.0, 0.0,       dx,       dy);

    float3 P0 = tex2D(Source, t1.xy).xyz, P1 = tex2D(Source, t1.zy).xyz;
    float3 P2 = tex2D(Source, t1.xw).xyz, P3 = tex2D(Source, t1.zw).xyz;
    float3  B = tex2D(Source, t2.xy).xyz,  C = tex2D(Source, t2.zy).xyz;
    float3 H5 = tex2D(Source, t2.xw).xyz, I5 = tex2D(Source, t2.zw).xyz;
    float3  D = tex2D(Source, t3.xy).xyz, F4 = tex2D(Source, t3.zy).xyz;
    float3  G = tex2D(Source, t3.xw).xyz, I4 = tex2D(Source, t3.zw).xyz;
    float3  E = tex2D(Source, t4.xy).xyz,  F = tex2D(Source, t4.zy).xyz;
    float3  H = tex2D(Source, t4.xw).xyz,  I = tex2D(Source, t4.zw).xyz;

    return float4(super_xbr(wpa, wpb, 1.29633, 1.75068,
                            P0, P1, P2, P3, B, C, H5, I5, D, F4, G, I4, E, F, H, I), 1.0);
}

//! pass
//! scale 1
/* Pass 0: diagonal edge detection at the original size. */
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    return square_pass(uv, float4(2.0, 1.0, -1.0, 4.0), float2(-1.0, 1.0));
}

//! pass
//! scale 2
/* Pass 1: the actual doubling. Pixels on the original grid are copied straight through --
   half from pass 0, half from the game's own image -- and only the new ones are interpolated,
   from a neighbourhood rotated 45 degrees. */
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    /* SourceSize is the size of the input, so this identifies the sub-pixel of the output. */
    float2 fp = frac(uv * SourceSize.xy);
    float2 dir = fp - 0.5;
    if ((dir.x * dir.y) > 0.0)
        return (fp.x > 0.5) ? tex2D(Source, uv) : tex2D(Original, uv);

    float2 g1 = (fp.x > 0.5) ? float2(0.5 * SourceSize.z, 0.0) : float2(0.0, 0.5 * SourceSize.w);
    float2 g2 = (fp.x > 0.5) ? float2(0.0, 0.5 * SourceSize.w) : float2(0.5 * SourceSize.z, 0.0);

    float3 P0 = tex2D(Original, uv - 3.0 * g1).xyz;
    float3 P1 = tex2D(Source,   uv - 3.0 * g2).xyz;
    float3 P2 = tex2D(Source,   uv + 3.0 * g2).xyz;
    float3 P3 = tex2D(Original, uv + 3.0 * g1).xyz;

    float3  B = tex2D(Source,   uv - 2.0 * g1 -       g2).xyz;
    float3  C = tex2D(Original, uv -       g1 - 2.0 * g2).xyz;
    float3  D = tex2D(Source,   uv - 2.0 * g1 +       g2).xyz;
    float3  E = tex2D(Original, uv -       g1          ).xyz;
    float3  F = tex2D(Source,   uv            -       g2).xyz;
    float3  G = tex2D(Original, uv -       g1 + 2.0 * g2).xyz;
    float3  H = tex2D(Source,   uv            +       g2).xyz;
    float3  I = tex2D(Original, uv +       g1          ).xyz;

    float3 F4 = tex2D(Original, uv +       g1 - 2.0 * g2).xyz;
    float3 I4 = tex2D(Source,   uv + 2.0 * g1 -       g2).xyz;
    float3 H5 = tex2D(Original, uv +       g1 + 2.0 * g2).xyz;
    float3 I5 = tex2D(Source,   uv + 2.0 * g1 +       g2).xyz;

    return float4(super_xbr(float4(2.0, 1.0, 0.0, 0.0), float2(0.0, 0.0), 1.75068, 1.29633,
                            P0, P1, P2, P3, B, C, H5, I5, D, F4, G, I4, E, F, H, I), 1.0);
}

//! pass
//! scale 1
/* Pass 2: orthogonal cleanup over the doubled image. */
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    return square_pass(uv, float4(1.0, 0.0, 2.0, 3.0), float2(-2.0, 1.0));
}
