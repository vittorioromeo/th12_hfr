//! scale 2
/*
   MMPX -- pixel-art magnification filter (fixed 2x)

   by Morgan McGuire and Mara Gagiu
   https://casual-effects.com/research/McGuire2021PixelArt/
   License: MIT
   adapted for slang by hunterk
   ported to Direct3D 9 HLSL (ps_3_0) for this project

   The upstream slang file (mmpx.slang) states only "License: MIT"; the full
   text of that licence, as it applies to the original MMPX work, is:

   Copyright (c) Morgan McGuire and Mara Gagiu.

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
*/

/* The runtime prepends:
     sampler2D Source : register(s0);
     float4 SourceSize : register(c0);
     float4 TargetSize : register(c1);
     #define SourceSampler Source
   so none of that is declared here.

   PORTING NOTE -- colour identity is tested through a scalar key.
   MMPX is built entirely out of exact "is this pixel the same colour as that
   one" tests.  Done literally (all(a == b) on float3) the shader needs 627
   ps_3_0 instruction slots and 26 temporaries, which is over the 512 slots
   ps_3_0 is only guaranteed to provide.  Here each sampled texel is folded
   once, with a single dp3, into a scalar key

       key = dot(rgb, float3(16711680, 65280, 255))

   For an 8-bit UNORM source (which is what a D3D9 game back buffer is) each
   channel arrives as exactly float32(k/255), k in 0..255, and each of the
   three products is then *exactly* the integer 65536*k / 256*k / k -- this
   was verified by evaluating the expression in float32 for all 2^24 colours,
   under both summation orders.  Every partial sum is an integer below 2^24
   and so is exact too, which makes the key exactly 65536*R8+256*G8+B8: a
   bijection onto 0..2^24-1.  `keyA == keyB` is therefore true for exactly the
   same pairs as `all(colA == colB)`.  (For a hypothetical non-8-bit source
   the key stays a deterministic function of the colour, so there are still no
   false negatives; only colours closer together than ~1/16711680 could
   collide.)  Every rule below is textually identical to the slang original.

   Only E, B, D, F and H are also kept as real colours, because those are the
   only texels whose value is ever written to the output or fed to luma();
   the other 20 taps exist purely to be compared.  That is what keeps the
   register count down.  No rule, threshold or ordering has been changed.
*/

float luma(float3 col) {
   return dot(col, float3(0.2126, 0.7152, 0.0722));
}

// Exact scalar key for an 8-bit RGB colour (see note above).  One dp3.
float key(float3 col) {
   return dot(col, float3(16711680.0, 65280.0, 255.0));
}

bool same(float B, float A0) {
   return B == A0;
}

bool notsame(float B, float A0) {
   return B != A0;
}

bool all_eq2(float B, float A0, float A1) {
   return (same(B,A0) && same(B,A1));
}

bool all_eq3(float B, float A0, float A1, float A2) {
   return (same(B,A0) && same(B,A1) && same(B,A2));
}

bool all_eq4(float B, float A0, float A1, float A2, float A3) {
   return (same(B,A0) && same(B,A1) && same(B,A2) && same(B,A3));
}

bool any_eq3(float B, float A0, float A1, float A2) {
   return (same(B,A0) || same(B,A1) || same(B,A2));
}

bool none_eq2(float B, float A0, float A1) {
   return (notsame(B,A0) && notsame(B,A1));
}

bool none_eq4(float B, float A0, float A1, float A2, float A3) {
   return (notsame(B,A0) && notsame(B,A1) && notsame(B,A2) && notsame(B,A3));
}

// colour at offset (c,d) texels from the current source texel
#define srcc(c,d) tex2D(Source, uv + float2(c,d) * SourceSize.zw).rgb
// identity key at offset (c,d) texels
#define src(c,d)  key(srcc(c,d))

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
// these do nothing, but just for consistency with the original code...
   float srcX = 0.;
   float srcY = 0.;

// Our current pixel (colour, and identity key)
   float3 Ecol = srcc(srcX+0.,srcY+0.);
   float  E    = key(Ecol);

// Input: A-I central 3x3 grid.  B, D, F and H are also needed as colours.
   float  A    = src(srcX-1.,srcY-1.);
   float3 Bcol = srcc(srcX+0.,srcY-1.);
   float  B    = key(Bcol);
   float  C    = src(srcX+1.,srcY-1.);

   float3 Dcol = srcc(srcX-1.,srcY+0.);
   float  D    = key(Dcol);
   float3 Fcol = srcc(srcX+1.,srcY+0.);
   float  F    = key(Fcol);

   float  G    = src(srcX-1.,srcY+1.);
   float3 Hcol = srcc(srcX+0.,srcY+1.);
   float  H    = key(Hcol);
   float  I    = src(srcX+1.,srcY+1.);

// Default to Nearest magnification
   float3 J = Ecol;
   float3 K = Ecol;
   float3 L = Ecol;
   float3 M = Ecol;

// Skip constant 3x3 centers and just use nearest-neighbor
// them.  This gives a good speedup on spritesheets with
// lots of padding and full screen images with large
// constant regions such as skies.
// EDIT: this is a wash for me, but we'll keep it around
   if(same(E,A) && same(E,B) && same(E,C) && same(E,D) && same(E,F) && same(E,G) && same(E,H) && same(E,I)) return float4(Ecol, 1.0);

// Read additional values at the tips of the diamond pattern
   float P = src(srcX+0.,srcY-2.);
   float Q = src(srcX-2.,srcY+0.);
   float R = src(srcX+2.,srcY+0.);
   float S = src(srcX+0.,srcY+2.);

// Precompute luminances
   float Bl = luma(Bcol);
   float Dl = luma(Dcol);
   float El = luma(Ecol);
   float Fl = luma(Fcol);
   float Hl = luma(Hcol);

// Round some corners and fill in 1:1 slopes, but preserve
// sharp right angles.
//
// In each expression, the left clause is from
// EPX and the others are new. EPX
// recognizes 1:1 single-pixel lines because it
// applies the rounding only to the LINE, and not
// to the background (it looks at the mirrored
// side).  It thus fails on thick 1:1 edges
// because it rounds *both* sides and produces an
// aliased edge shifted by 1 dst pixel.  (This
// also yields the mushroom-shaped arrow heads,
// where that 1-pixel offset runs up against the
// 2-pixel aligned end; this is an inherent
// problem with 2X in-palette scaling.)
//
// The 2nd clause clauses avoid *double* diagonal
// filling on 1:1 slopes to prevent them becoming
// aliased again. It does this by breaking
// symmetry ties using luminance when working with
// thick features (it allows thin and transparent
// features to pass always).
//
// The 3rd clause seeks to preserve square corners
// by considering the center value before
// rounding.
//
// The 4th clause identifies 1-pixel bumps on
// straight lines that are darker than their
// background, such as the tail on a pixel art
// "4", and prevents them from being rounded. This
// corrects for asymmetry in this case that the
// luminance tie breaker introduced.

// .------------ 1st ------------.      .----- 2nd ---------.      .------ 3rd -----.      .--------------- 4th -----------------------.
   if (((same(D,B) && notsame(D,H) && notsame(D,F))) && ((El>=Dl) || same(E,A)) && any_eq3(E,A,C,G) && ((El<Dl) || notsame(A,D) || notsame(E,P) || notsame(E,Q))) J=Dcol;
   if (((same(B,F) && notsame(B,D) && notsame(B,H))) && ((El>=Bl) || same(E,C)) && any_eq3(E,A,C,I) && ((El<Bl) || notsame(C,B) || notsame(E,P) || notsame(E,R))) K=Bcol;
   if (((same(H,D) && notsame(H,F) && notsame(H,B))) && ((El>=Hl) || same(E,G)) && any_eq3(E,A,G,I) && ((El<Hl) || notsame(G,H) || notsame(E,S) || notsame(E,Q))) L=Hcol;
   if (((same(F,H) && notsame(F,B) && notsame(F,D))) && ((El>=Fl) || same(E,I)) && any_eq3(E,C,G,I) && ((El<Fl) || notsame(I,H) || notsame(E,R) || notsame(E,S))) M=Fcol;

// Clean up disconnected line intersections.
//
// The first clause recognizes being on the inside
// of a diagonal corner and ensures that the "foreground"
// has been correctly identified to avoid
// ambiguous cases such as this:
//
//  o#o#
//  oo##
//  o#o#
//
// where trying to fix the center intersection of
// either the "o" or the "#" will leave the other
// one disconnected. This occurs, for example,
// when a pixel-art letter "B" or "R" is next to
// another letter on the right.
//
// The second clause ensures that the pattern is
// not a notch at the edge of a checkerboard
// dithering pattern.
//
// >
//  .--------------------- 1st ------------------------.      .--------- 2nd -----------.
   if ((notsame(E,F) && all_eq4(E,C,I,D,Q) && all_eq2(F,B,H)) && notsame(F,src(srcX+3.,srcY))) K=M=Fcol;
   if ((notsame(E,D) && all_eq4(E,A,G,F,R) && all_eq2(D,B,H)) && notsame(D,src(srcX-3.,srcY))) J=L=Dcol;
   if ((notsame(E,H) && all_eq4(E,G,I,B,P) && all_eq2(H,D,F)) && notsame(H,src(srcX,srcY+3.))) L=M=Hcol;
   if ((notsame(E,B) && all_eq4(E,A,C,H,S) && all_eq2(B,D,F)) && notsame(B,src(srcX,srcY-3.))) J=K=Bcol;

// Remove tips of bright triangles on dark
// backgrounds. The luminance tie breaker for 1:1
// pixel lines leaves these as sticking up squared
// off, which makes bright triangles and diamonds
// look bad.
   if ((Bl<El) && all_eq4(E,G,H,I,S) && none_eq4(E,A,D,C,F)) J=K=Bcol;
   if ((Hl<El) && all_eq4(E,A,B,C,P) && none_eq4(E,D,G,I,F)) L=M=Hcol;
   if ((Fl<El) && all_eq4(E,A,D,G,Q) && none_eq4(E,B,C,I,H)) K=M=Fcol;
   if ((Dl<El) && all_eq4(E,C,F,I,R) && none_eq4(E,B,A,G,H)) J=L=Dcol;

//////////////////////////////////////////////////////////////////////////////////
// Do further neighborhood peeking to identify
// 2:1 and 1:2 slopes of constant color.
// The first clause of each rule identifies a 2:1 slope line
// of consistent color.
//
// The second clause verifies that the line is separated from
// every adjacent pixel on one side and not part of a more
// complex pattern. Common subexpressions from the second clause
// are lifted to an outer test on pairs of rules.
//
// The actions taken by rules are unusual in that they extend
// a color assigned by previous rules rather than drawing from
// the original source image.
//
// The comments show a diagram of the local
// neighborhood in which letters shown with the
// same shape and color must match each other and
// everything else without annotation must be
// different from the solid colored, square
// letters.
   if (notsame(H,B)) { // Common subexpression
                       // Above a 2:1 slope or -2:1 slope
                       // First:
      if (notsame(H,A) && notsame(H,E) && notsame(H,C)) {
                       // Second:
                       //     P
                       //   A B C .
                       // Q D E F R
                       //   G H I
                       //     S
         if (all_eq3(H,G,F,R) && none_eq2(H,D,src(srcX+2.,srcY-1.))) L=M;
                       // Third:
                       //     P
                       // . A B C
                       // Q D E F R
                       //   G H I
                       //     S
         if (all_eq3(H,I,D,Q) && none_eq2(H,F,src(srcX-2.,srcY-1.))) M=L;
      }

                       // Below a 2:1 or -2:1 slope (reflect the above 2:1 patterns vertically)
      if (notsame(B,I) && notsame(B,G) && notsame(B,E)) {
                       //     P
                       //   A B C
                       // Q D E F R
                       //   G H I .
                       //     S
         if (all_eq3(B,A,F,R) && none_eq2(B,D,src(srcX+2.,srcY+1.))) J=K;
                       //     P
                       //   A B C
                       // Q D E F R
                       // . G H I
                       //     S
         if (all_eq3(B,C,D,Q) && none_eq2(B,F,src(srcX-2.,srcY+1.))) K=J;
      }
   }

   if (notsame(F,D)) { // Common subexpression

                       // Right of a -1:2 or 1:2 slope (reflect the left 1:2 patterns horizontally)
      if (notsame(D,I) && notsame(D,E) && notsame(D,C)) {
                       //     P
                       //   A B C
                       // Q D E F R
                       //   G H I
                       //     S .
         if (all_eq3(D,A,H,S) && none_eq2(D,B,src(srcX+1.,srcY+2.))) J=L;
                       //     P .
                       //   A B C
                       // Q D E F R
                       //   G H I
                       //     S
         if (all_eq3(D,G,B,P) && none_eq2(D,H,src(srcX+1.,srcY-2.))) L=J;
      }

                       // Left of a 1:2 slope or -1:2 slope (transpose the above 2:1 patterns)
                       // Pull common none_eq subexpressions out
      if (notsame(F,E) && notsame(F,A) && notsame(F,G)) {
                       //     P
                       //   A B C
                       // Q D E F R
                       //   G H I
                       //   . S
         if (all_eq3(F,C,H,S) && none_eq2(F,B,src(srcX-1.,srcY+2.))) K=M;
                       //   . P
                       //   A B C
                       // Q D E F R
                       //   G H I
                       //     S
         if (all_eq3(F,I,B,P) && none_eq2(F,H,src(srcX-1.,srcY-2.))) M=K;
      }
   }

// Determine which of our 4 output pixels we need to use.
// The MMPX pass renders into a target that is exactly 2x the source, so the
// fractional position of uv inside the source texel (0.25 or 0.75 on each
// axis) selects the quadrant, exactly as in the slang original.
   float2 a = frac(uv * SourceSize.xy);
   float3 res = (a.x < 0.5) ? ((a.y < 0.5) ? J : L) : ((a.y < 0.5) ? K : M);
   return float4(res, 1.0);
}
