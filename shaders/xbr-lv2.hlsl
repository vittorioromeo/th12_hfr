/*
   Hyllian's xBR-lv2 Shader

   Copyright (C) 2011-2022 Hyllian - sergiogdb@gmail.com

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the
   "Software"), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to permit
   persons to whom the Software is furnished to do so, subject to the
   following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.

   Incorporates some of the ideas from SABR shader. Thanks to Joshua Street.

   Ported to Direct3D 9 HLSL (ps_3_0) for this project from the libretro
   slang version (xbr-lv2-standalone.slang). The runtime-tweakable slang
   #pragma parameters are baked in below at their upstream defaults.
*/

/* The runtime prepends:
     sampler2D Source : register(s0);
     float4 SourceSize : register(c0);
     float4 TargetSize : register(c1);
     #define SourceSampler Source
   so none of that is declared here. */

#define XBR_EQ_THRESHOLD   0.32   // COLOR DISTINCTION THRESHOLD
#define XBR_LV2_COEFFICIENT 0.30  // SMOOTHNESS THRESHOLD
#define XBR_BLENDING       1.0    // 0.0 = NOBLEND, 1.0 = AA

// Uncomment just one of the three params below to choose the corner detection
//#define CORNER_A
//#define CORNER_B
#define CORNER_C

#define lv2_cf (XBR_LV2_COEFFICIENT + 2.0)

static const float4 Ao = float4( 1.0, -1.0, -1.0, 1.0 );
static const float4 Bo = float4( 1.0,  1.0, -1.0,-1.0 );
static const float4 Co = float4( 1.5,  0.5, -0.5, 0.5 );
static const float4 Ax = float4( 1.0, -1.0, -1.0, 1.0 );
static const float4 Bx = float4( 0.5,  2.0, -0.5,-2.0 );
static const float4 Cx = float4( 1.0,  1.0, -0.5, 0.0 );
static const float4 Ay = float4( 1.0, -1.0, -1.0, 1.0 );
static const float4 By = float4( 2.0,  0.5, -2.0,-0.5 );
static const float4 Cy = float4( 2.0,  0.0, -1.0, 0.5 );
static const float4 Ci = float4(0.25, 0.25, 0.25, 0.25);

static const float3 v2f = float3( 65536.0, 256.0, 1.0 ); // vec to float encode
static const float3 Y   = float3( 0.2627, 0.6780, 0.0593 );

// GLSL mat4x3 stand-in: four columns, each a float3.
struct M43 { float3 c0; float3 c1; float3 c2; float3 c3; };

M43 mk43(float3 a, float3 b, float3 c, float3 d)
{
    M43 m; m.c0 = a; m.c1 = b; m.c2 = c; m.c3 = d; return m;
}

// v2f * mat4x3 in GLSL: component i is dot(v2f, m[i]).
float4 enc43(M43 A)
{
    return float4(dot(v2f, A.c0), dot(v2f, A.c1), dot(v2f, A.c2), dot(v2f, A.c3));
}

// Return if A components are less than or equal B ones.
float4 LTE(float4 A, float4 B)
{
    return step(A, B);
}

// Return if A components are less than B ones.
float4 LT(float4 A, float4 B)
{
    return float4(A.x < B.x, A.y < B.y, A.z < B.z, A.w < B.w);
}

// Return logically inverted vector components. BEWARE: Only works with 0.0 or 1.0 components.
float4 NOT(float4 A)
{
    return (float4(1.0, 1.0, 1.0, 1.0) - A);
}

// Compare two vectors and return their components are different.
float4 diff(float4 A, float4 B)
{
    return float4(A.x != B.x, A.y != B.y, A.z != B.z, A.w != B.w);
}

float dist(float3 A, float3 B)
{
    return dot(abs(A - B), Y);
}

// Calculate color distance between two vectors of four pixels
float4 dist4(M43 A, M43 B)
{
    return float4(dist(A.c0, B.c0), dist(A.c1, B.c1), dist(A.c2, B.c2), dist(A.c3, B.c3));
}

// Tests if color components are under a threshold. In this case they are considered 'equal'.
float4 eq(M43 A, M43 B)
{
    return step(dist4(A, B), float4(XBR_EQ_THRESHOLD, XBR_EQ_THRESHOLD, XBR_EQ_THRESHOLD, XBR_EQ_THRESHOLD));
}

// Determine if two vector components are NOT equal based on a threshold.
float4 neq(M43 A, M43 B)
{
    return (float4(1.0, 1.0, 1.0, 1.0) - eq(A, B));
}

// Calculate weighted distance among pixels in some directions.
float4 weighted_distance(M43 a, M43 b, M43 c, M43 d, M43 e, M43 f, M43 g, M43 h)
{
    return (dist4(a,b) + dist4(a,c) + dist4(d,e) + dist4(d,f) + 4.0 * dist4(g,h));
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float4 edri, edr, edr_l, edr_u, px; // px = pixel, edr = edge detection rule
    float4 irlv0, irlv1, irlv2l, irlv2u;
    float4 fx, fx_l, fx_u; // inequations of straight lines.
    float3 res1, res2;
    float4 fx45i, fx45, fx30, fx60;

    // --- folded-in vertex stage ------------------------------------------
    float2 texCoord = uv * 1.0001;

    float aa_factor = 2.0 * TargetSize.z * SourceSize.x;

    float dx = SourceSize.z;
    float dy = SourceSize.w;

    //    A1 B1 C1
    // A0  A  B  C C4
    // D0  D  E  F F4
    // G0  G  H  I I4
    //    G5 H5 I5

    float4 t1 = texCoord.xxxy + float4( -dx, 0, dx, -2.0*dy); // A1 B1 C1
    float4 t2 = texCoord.xxxy + float4( -dx, 0, dx,     -dy); //  A  B  C
    float4 t3 = texCoord.xxxy + float4( -dx, 0, dx,       0); //  D  E  F
    float4 t4 = texCoord.xxxy + float4( -dx, 0, dx,      dy); //  G  H  I
    float4 t5 = texCoord.xxxy + float4( -dx, 0, dx,  2.0*dy); // G5 H5 I5
    float4 t6 = texCoord.xyyy + float4(-2.0*dx, -dy, 0,  dy); // A0 D0 G0
    float4 t7 = texCoord.xyyy + float4( 2.0*dx, -dy, 0,  dy); // C4 F4 I4
    // ---------------------------------------------------------------------

    float2 fp = frac(texCoord * SourceSize.xy);

    float3 A1 = tex2D(Source, t1.xw).xyz;
    float3 B1 = tex2D(Source, t1.yw).xyz;
    float3 C1 = tex2D(Source, t1.zw).xyz;
    float3 A  = tex2D(Source, t2.xw).xyz;
    float3 B  = tex2D(Source, t2.yw).xyz;
    float3 C  = tex2D(Source, t2.zw).xyz;
    float3 D  = tex2D(Source, t3.xw).xyz;
    float3 E  = tex2D(Source, t3.yw).xyz;
    float3 F  = tex2D(Source, t3.zw).xyz;
    float3 G  = tex2D(Source, t4.xw).xyz;
    float3 H  = tex2D(Source, t4.yw).xyz;
    float3 I  = tex2D(Source, t4.zw).xyz;
    float3 G5 = tex2D(Source, t5.xw).xyz;
    float3 H5 = tex2D(Source, t5.yw).xyz;
    float3 I5 = tex2D(Source, t5.zw).xyz;
    float3 A0 = tex2D(Source, t6.xy).xyz;
    float3 D0 = tex2D(Source, t6.xz).xyz;
    float3 G0 = tex2D(Source, t6.xw).xyz;
    float3 C4 = tex2D(Source, t7.xy).xyz;
    float3 F4 = tex2D(Source, t7.xz).xyz;
    float3 I4 = tex2D(Source, t7.xw).xyz;

    M43 b  = mk43(B, D, H, F);
    M43 c  = mk43(C, A, G, I);
    M43 d  = mk43(D, H, F, B);
    M43 e  = mk43(E, E, E, E);
    M43 f  = mk43(F, B, D, H);
    M43 g  = mk43(G, I, C, A);
    M43 h  = mk43(H, F, B, D);
    M43 i  = mk43(I, C, A, G);

    M43 i4 = mk43(I4, C1, A0, G5);
    M43 i5 = mk43(I5, C4, A1, G0);
    M43 h5 = mk43(H5, F4, B1, D0);
    M43 f4 = mk43(F4, B1, D0, H5);

    float4 b_  = enc43(b);
    float4 c_  = enc43(c);
    float4 d_  = b_.yzwx;
    float4 e_  = enc43(e);
    float4 f_  = b_.wxyz;
    float4 g_  = c_.zwxy;
    float4 h_  = b_.zwxy;
    float4 i_  = c_.wxyz;

    float4 i4_ = enc43(i4);
    float4 i5_ = enc43(i5);
    float4 h5_ = enc43(h5);
    float4 f4_ = h5_.yzwx;

    // These inequations define the line below which interpolation occurs.
    fx   = ( Ao*fp.y + Bo*fp.x );
    fx_l = ( Ax*fp.y + Bx*fp.x );
    fx_u = ( Ay*fp.y + By*fp.x );

    irlv0 = diff(e_, f_) * diff(e_, h_);
    irlv1 = irlv0;

#ifdef CORNER_B
    irlv1 = saturate(irlv0 * ( neq(f,b) * neq(h,d) + eq(e,i) * neq(f,i4) * neq(h,i5) + eq(e,g) + eq(e,c) ) );
#endif
#ifdef CORNER_C
    irlv1 = saturate(irlv0 * ( neq(f,b) * neq(f,c) + neq(h,d) * neq(h,g) + eq(e,i) * (neq(f,f4) * neq(f,i4) + neq(h,h5) * neq(h,i5)) + eq(e,g) + eq(e,c)) );
#endif

    irlv2l = diff(e_, g_) * diff(d_, g_);
    irlv2u = diff(e_, c_) * diff(b_, c_);

    if (XBR_BLENDING == 1.0) {
        float4 delta  = float4(aa_factor, aa_factor, aa_factor, aa_factor);
        float4 deltaL = float4(0.5, 1.0, 0.5, 1.0) * aa_factor;
        float4 deltaU = deltaL.yxwz;

        fx45i = saturate( 0.5 + (fx   - Co - Ci) / delta  );
        fx45  = saturate( 0.5 + (fx   - Co     ) / delta  );
        fx30  = saturate( 0.5 + (fx_l - Cx     ) / deltaL );
        fx60  = saturate( 0.5 + (fx_u - Cy     ) / deltaU );
    }
    else {
        fx45i = LT( Co + Ci, fx   );
        fx45  = LT(      Co, fx   );
        fx30  = LT(      Cx, fx_l );
        fx60  = LT(      Cy, fx_u );
    }

    float4 wd1 = weighted_distance( e, c,  g, i, h5, f4, h, f);
    float4 wd2 = weighted_distance( h, d, i5, f, i4,  b, e, i);

    float4 d_fg = dist4(f, g);
    float4 d_hc = dist4(h, c);

    edri  = LTE(wd1, wd2) * irlv0;
    edr   = LT( wd1, wd2) * irlv1 * NOT(edri.yzwx * edri.wxyz);
    edr_l = LTE( lv2_cf * d_fg, d_hc ) * irlv2l * edr * (NOT(edri.yzwx) * eq(e, c));
    edr_u = LTE( lv2_cf * d_hc, d_fg ) * irlv2u * edr * (NOT(edri.wxyz) * eq(e, g));

    fx45i = edri  * fx45i;
    fx45  = edr   * fx45;
    fx30  = edr_l * fx30;
    fx60  = edr_u * fx60;

    px = LTE(dist4(e,f), dist4(e,h));

    float4 maximos = max(max(fx30, fx60), max(fx45, fx45i));

    res1 = lerp(E, lerp(H, F, px.x), maximos.x);
    res2 = lerp(E, lerp(B, D, px.z), maximos.z);

    float3 res1a = lerp(res1, res2, step(dist(E, res1), dist(E, res2)));

    res1 = lerp(E, lerp(F, B, px.y), maximos.y);
    res2 = lerp(E, lerp(D, H, px.w), maximos.w);

    float3 res1b = lerp(res1, res2, step(dist(E, res1), dist(E, res2)));

    float3 res = lerp(res1a, res1b, step(dist(E, res1a), dist(E, res1b)));

    return float4(res, 1.0);
}
