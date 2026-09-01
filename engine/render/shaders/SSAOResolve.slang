#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D rawAoTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[[vk::binding(5, 0)]] Sampler2D motionTexture : register(t5, space0);
[[vk::binding(6, 0)]] Sampler2D historyTexture : register(t6, space0);

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

float2 encodeOctahedral(float3 normal)
{
    normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
    if (normal.z < 0.0)
        normal.xy = (1.0 - abs(normal.yx)) *
                    float2(normal.x >= 0.0 ? 1.0 : -1.0, normal.y >= 0.0 ? 1.0 : -1.0);
    return normal.xy * 0.5 + 0.5;
}

float3 decodeOctahedral(float2 encoded)
{
    const float2 f = encoded * 2.0 - 1.0;
    float3 normal = float3(f, 1.0 - abs(f.x) - abs(f.y));
    const float t = saturate(-normal.z);
    normal.xy += float2(normal.x >= 0.0 ? -t : t, normal.y >= 0.0 ? -t : t);
    return normalize(normal);
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    // Every sampled render target uses texture space. Vulkan's negative
    // fullscreen viewport means interpolated vertex UVs must be oriented for
    // raw AO and history just as they are for the geometry buffers.
    const float2 textureUv = orientFullscreenUv(input.uv, pass.flipY);
    const float centerDepth = depthTexture.Sample(textureUv).r;
    if (centerDepth <= 0.000001) return float4(1.0, 0.0, 0.5, 0.5);
    const float3 centerWorld = reconstructWorldPosition(textureUv, centerDepth,
                                                        pass.inverseViewProjection, pass.flipY);
    const float3 centerView = mul(pass.view, float4(centerWorld, 1.0)).xyz;
    const float3 centerNormal = decodeNormal(normalTexture.Sample(textureUv).xyz);
    const int blurRadius = clamp(int(pass.parameters[1].x), 0, 4);
    float aoSum = 0.0;
    float weightSum = 0.0;
    float minAo = 1.0;
    float maxAo = 0.0;
    for (int y = -4; y <= 4; ++y)
    {
        if (abs(y) > blurRadius) continue;
        for (int x = -4; x <= 4; ++x)
        {
            if (abs(x) > blurRadius || abs(x) + abs(y) > blurRadius) continue;
            const float2 offset = float2(x, y);
            const float2 sampleUv = saturate(textureUv + offset * pass.inverseResolution);
            const float sampleDepth = depthTexture.Sample(sampleUv).r;
            if (sampleDepth <= 0.000001) continue;
            const float3 sampleWorld = reconstructWorldPosition(sampleUv, sampleDepth,
                                                                pass.inverseViewProjection, pass.flipY);
            const float sampleViewDepth = -mul(pass.view, float4(sampleWorld, 1.0)).z;
            const float depthDelta = abs(sampleViewDepth + centerView.z);
            if (depthDelta > 0.35) continue;
            const float3 sampleNormal = decodeNormal(normalTexture.Sample(sampleUv).xyz);
            float normalWeight = max(dot(centerNormal, sampleNormal), 0.0);
            normalWeight *= normalWeight;
            normalWeight *= normalWeight;
            normalWeight *= normalWeight;
            normalWeight *= normalWeight;
            const float weight = (1.0 / (1.0 + dot(offset, offset) * 0.7)) *
                                 (1.0 / (1.0 + depthDelta * 24.0)) * normalWeight;
            const float sampleAo = rawAoTexture.Sample(sampleUv).r;
            aoSum += sampleAo * weight;
            weightSum += weight;
            minAo = min(minAo, sampleAo);
            maxAo = max(maxAo, sampleAo);
        }
    }
    const float currentAo = weightSum > 0.0001 ? aoSum / weightSum
                                              : rawAoTexture.Sample(textureUv).r;
    float resolvedAo = currentAo;
    if (pass.parameters[5].w > 0.5)
    {
        const float2 orientation = float2(1.0, pass.flipY != 0 ? -1.0 : 1.0);
        const float2 motion = (motionTexture.Sample(textureUv).xy * 2.0 - 1.0) * orientation;
        const float2 historyUv = textureUv - motion;
        if (all(historyUv >= 0.0) && all(historyUv <= 1.0))
        {
            const float4 history = historyTexture.Sample(historyUv);
            const float relativeDepthDelta = abs(history.g + centerView.z) / max(-centerView.z, 0.001);
            const float normalSimilarity = dot(decodeOctahedral(history.ba), centerNormal);
            if (relativeDepthDelta <= pass.parameters[2].x && normalSimilarity >= pass.parameters[2].y)
                resolvedAo = lerp(currentAo, clamp(history.r, minAo, maxAo), pass.parameters[1].z);
        }
    }
    const float2 encodedNormal = encodeOctahedral(centerNormal);
    return float4(resolvedAo, -centerView.z, encodedNormal);
}
