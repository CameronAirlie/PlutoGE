#include "Fullscreen.slang"
#include "ScreenSpaceCommon.slang"

[[vk::binding(0, 0)]] cbuffer DebugViewParameters : register(b0, space0)
{
    column_major float4x4 inverseViewProjection;
    float4 cameraPosition;
    uint debugViewMode;
    uint flipY;
    uint2 debugPadding;
};

[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[[vk::binding(4, 0)]] Sampler2D albedoTexture : register(t4, space0);
[[vk::binding(5, 0)]] Sampler2D materialTexture : register(t5, space0);
[[vk::binding(6, 0)]] Sampler2D debugTexture : register(t6, space0);

float3 cascadeColor(float index)
{
    if (index < 0.1667) return float3(1.0, 0.20, 0.15);
    if (index < 0.50) return float3(0.20, 1.0, 0.25);
    if (index < 0.8333) return float3(0.20, 0.45, 1.0);
    return float3(1.0, 0.85, 0.15);
}

float3 lodColor(float lod)
{
    lod = saturate(lod);
    if (lod < 0.3333)
        return lerp(float3(0.05, 0.28, 1.0), float3(0.10, 0.85, 0.25), lod / 0.3333);
    if (lod < 0.6667)
        return lerp(float3(0.10, 0.85, 0.25), float3(1.0, 0.78, 0.15), (lod - 0.3333) / 0.3334);
    return lerp(float3(1.0, 0.78, 0.15), float3(1.0, 0.10, 0.08), (lod - 0.6667) / 0.3333);
}

float3 debugSample(uint mode, float2 uv)
{
    const float depth = depthTexture.Sample(uv).r;
    const float4 debugData = debugTexture.Sample(uv);
    if (mode == 2)
    {
        if (depth <= 0.000001) return 0.0;
        const float3 world = reconstructWorldPosition(uv, depth, inverseViewProjection, flipY);
        return frac(abs(world) * 0.05);
    }
    if (mode == 3) return normalTexture.Sample(uv).rgb;
    if (mode == 4) return albedoTexture.Sample(uv).rgb;
    if (mode == 5)
    {
        if (depth <= 0.000001) return 0.0;
        const float3 world = reconstructWorldPosition(uv, depth, inverseViewProjection, flipY);
        const float linearized = 1.0 - exp(-length(world - cameraPosition.xyz) * 0.02);
        return linearized.xxx;
    }
    if (mode == 6) return depth <= 0.000001 ? sceneTexture.Sample(uv).rgb : cascadeColor(debugData.y);
    if (mode == 7) return debugData.zzz;
    if (mode == 8) return debugData.www;
    if (mode == 9) return lodColor(debugData.x);
    return sceneTexture.Sample(uv).rgb;
}

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    return fullscreenVertex(vertexId);
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, flipY);
    if (debugViewMode == 0)
        return float4(saturate(sceneTexture.Sample(uv).rgb), 1.0);
    if (debugViewMode == 1)
    {
        const float2 quadrantUv = frac(uv * 2.0);
        const bool right = uv.x >= 0.5;
        const bool top = uv.y >= 0.5;
        const uint mode = !right && !top ? 2u : right && !top ? 3u : !right ? 4u : 5u;
        float3 color = debugSample(mode, quadrantUv);
        const float2 edge = min(quadrantUv, 1.0 - quadrantUv);
        color = lerp(float3(0.95), color, smoothstep(0.0, 0.006, min(edge.x, edge.y)));
        return float4(color, 1.0);
    }
    return float4(debugSample(debugViewMode, uv), 1.0);
}
