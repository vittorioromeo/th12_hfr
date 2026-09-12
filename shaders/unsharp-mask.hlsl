//! post
/*
   Unsharp mask on luma -- a post-process on the upscaled image

   Written for this project; same licence as the project.

   The classic sharpening operator: subtract a slightly blurred copy of the picture from
   the picture and add the difference back, scaled. Only the luma is sharpened, so colour
   edges keep their hue and chroma fringing cannot appear, and the added difference is
   clamped so a strong setting cannot produce wide bright or dark halos: the strength
   raises the slope of an edge, and the clamp caps how far past its neighbours a pixel
   can be pushed.

   Params.x is the menu's strength, 0..1. At 0 the picture is returned unchanged; at 1 the
   luma difference is added twice over, which is as much as still looks like the game.

   It is deliberately the plain version of the operation: no thresholding, no edge
   detection. CAS (cas.hlsl) is the adaptive one, and usually the better choice; this one
   is here for comparison and for people who know the look they want.
*/

static const float3 LUMA = float3(0.299, 0.587, 0.114);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 px = SourceSize.zw;
    float4 centre = tex2D(Source, uv);

    // A 3x3 binomial blur (1 2 1 / 2 4 2 / 1 2 1) / 16, sampled at the four diagonal
    // midpoints so the hardware's bilinear filtering does the weighting: each sample
    // averages 2x2 texels, and the four together weight the centre 4/16, its edge
    // neighbours 2/16 and its corners 1/16.
    float2 o = px * 0.5;
    float3 blur = tex2D(Source, uv + float2(-o.x, -o.y)).rgb
                + tex2D(Source, uv + float2( o.x, -o.y)).rgb
                + tex2D(Source, uv + float2(-o.x,  o.y)).rgb
                + tex2D(Source, uv + float2( o.x,  o.y)).rgb;
    blur *= 0.25;

    float detail = dot(centre.rgb, LUMA) - dot(blur, LUMA);
    float strength = saturate(Params.x) * 2.0;
    // The clamp: a pixel is pushed at most this far in luma, however strong the setting.
    float lift = clamp(detail * strength, -0.12, 0.12);
    return float4(saturate(centre.rgb + lift), centre.a);
}
