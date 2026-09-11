#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(6, 0)]] Sampler2D volumeTexture : register(t6, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

float viewDistance(float2 uv, float depth)
{
    return abs(mul(pass.view, float4(reconstructWorldPosition(uv, depth,
        pass.inverseViewProjection, pass.flipY, pass.zeroToOneDepth), 1)).z);
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 scene = sceneTexture.Sample(uv);
    const float depth = depthTexture.Sample(uv).r;
    const bool sky = depth <= 0.000001;
    const float distance = sky ? 0.0 : viewDistance(uv, depth);
    const float2 traceSize = pass.parameters[5].xy;
    const float2 position = uv * traceSize - 0.5;
    const float2 base = floor(position);
    const float2 fraction = frac(position);
    float4 sum = 0;
    float weightSum = 0;
    float bestError = 1e30;
    float4 closest = float4(0, 0, 0, 1);
    // Bilateral reconstruction of scattering only, using the exact depth
    // samples consumed by the low-resolution rays (nearest sampling).
    for (uint y = 0; y < 2; ++y)
        for (uint x = 0; x < 2; ++x)
        {
            const float2 sampleUv = (clamp(base + float2(x, y), 0.0, traceSize - 1.0) + 0.5) / traceSize;
            const float sampleDepth = depthTexture.Sample(sampleUv).r;
            const bool sampleSky = sampleDepth <= 0.000001;
            const float error = sky != sampleSky ? 1e20 : sky ? 0.0 :
                abs(viewDistance(sampleUv, sampleDepth) - distance) / max(distance * 0.02, 0.05);
            const float4 volume = volumeTexture.Sample(sampleUv);
            if (error < bestError) { bestError = error; closest = volume; }
            const float weight = (x == 0 ? 1.0 - fraction.x : fraction.x) *
                                 (y == 0 ? 1.0 - fraction.y : fraction.y) * exp(-error);
            sum += volume * weight;
            weightSum += weight;
        }
    // If a thin foreground surface has no matching low-resolution sample,
    // avoid leaking background scattering across its silhouette.
    const float4 volume = weightSum > 0.00001 ? sum / weightSum :
                          bestError < 4.0 ? closest : float4(0, 0, 0, 1);
    return float4(scene.rgb * saturate(volume.a) + volume.rgb, scene.a);
}
