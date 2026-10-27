#include "Fullscreen.slang"
#include "PostProcessParameters.slang"

// Robust contrast-adaptive sharpening of a temporal upscaler's output.
// Ported from AMD FidelityFX RCAS (ffx_fsr1.h, MIT licence), denoise enabled.
//
// RCAS bounds its sharpening lobe by the 4-neighbour ring so it cannot create
// values outside the local range, which assumes display-referred [0, 1]
// colour. Upscaled scene colour is linear HDR, where bright/dark edges defeat
// that limiter and leave ringing and checkered edges. Filter in a reversible
// tone-mapped space instead, then map back to linear HDR for post-processing.
//
// Lane 0.x: sharpening attenuation, exp2(-stops) (1 = strongest).

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sourceTexture : register(t1, space0);

static const float RcasLimit = 0.25 - 1.0 / 16.0;

float3 compress(float3 color) { return color / (max(max(color.r, color.g), color.b) + 1.0); }
float3 expand(float3 color) { return color / max(1.0 - min(max(max(color.r, color.g), color.b), 0.999), 0.001); }
float max3(float3 v) { return max(max(v.x, v.y), v.z); }
float min3(float3 v) { return min(min(v.x, v.y), v.z); }
float3 tap(int2 pixel, int2 size) { return compress(max(sourceTexture.Load(int3(clamp(pixel, int2(0), size - 1), 0)).rgb, 0.0)); }

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID) { return fullscreenVertex(vertexId); }

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target0
{
    uint width, height;
    sourceTexture.GetDimensions(width, height);
    const int2 size = int2(width, height);
    const int2 pixel = int2(input.position.xy);
    //    b
    //  d e f
    //    h
    const float3 b = tap(pixel + int2(0, -1), size);
    const float3 d = tap(pixel + int2(-1, 0), size);
    const float4 centre = sourceTexture.Load(int3(pixel, 0));
    const float3 e = compress(max(centre.rgb, 0.0));
    const float3 f = tap(pixel + int2(1, 0), size);
    const float3 h = tap(pixel + int2(0, 1), size);

    // Luma times two; noise detection softens sharpening of isolated speckles.
    const float luma[5] = { b.b * 0.5 + (b.r * 0.5 + b.g), d.b * 0.5 + (d.r * 0.5 + d.g), e.b * 0.5 + (e.r * 0.5 + e.g),
                          f.b * 0.5 + (f.r * 0.5 + f.g), h.b * 0.5 + (h.r * 0.5 + h.g) };
    float noise = 0.25 * (luma[0] + luma[1] + luma[3] + luma[4]) - luma[2];
    const float lumaRange = max(max(max(luma[0], luma[1]), max(luma[2], luma[3])), luma[4]) -
                            min(min(min(luma[0], luma[1]), min(luma[2], luma[3])), luma[4]);
    noise = -0.5 * saturate(abs(noise) / max(lumaRange, 1.0e-6)) + 1.0;

    // Largest negative lobe that keeps every channel inside the ring's range.
    const float3 ringMin = min(min(b, d), min(f, h));
    const float3 ringMax = max(max(b, d), max(f, h));
    // Compressed colour stays below one, so only a black ring needs a guard.
    const float3 hitMin = ringMin / max(4.0 * ringMax, 1.0e-6);
    const float3 hitMax = (1.0 - ringMax) / (4.0 * ringMin - 4.0);
    const float3 lobes = max(-hitMin, hitMax);
    const float lobe = max(-RcasLimit, min(max3(lobes), 0.0)) * pass.parameters[0].x * noise;
    const float3 sharpened = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
    return float4(expand(saturate(sharpened)), centre.a);
}
