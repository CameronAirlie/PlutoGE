#include "Fullscreen.slang"
#include "ScreenSpaceCommon.slang"
struct TemporalParameters
{
    column_major float4x4 inverseViewProjection;
    column_major float4x4 view;
    column_major float4x4 previousView;
    float2 inverseResolution;
    float temporalBlend;
    float depthThreshold;
    float normalThreshold;
    uint flipY;
    uint hasHistory;
    uint debugView;
    uint zeroToOneDepth;
    uint indirectOnly;
    uint modulateIndirect;
};
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { TemporalParameters pass; };
[[vk::binding(1, 0)]] Sampler2D currentIndirect : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[[vk::binding(5, 0)]] Sampler2D motionTexture : register(t5, space0);
[[vk::binding(6, 0)]] Sampler2D historyColor : register(t6, space0);
[[vk::binding(7, 0)]] Sampler2D historyMetadata : register(t7, space0);
[[vk::binding(8, 0)]] Sampler2D sceneTexture : register(t8, space0);
[[vk::binding(4, 0)]] Sampler2D materialTexture : register(t4, space0);
[[vk::binding(9, 0)]] Sampler2D albedoTexture : register(t9, space0);
[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }
float2 signNotZero(float2 value) { return float2(value.x >= 0 ? 1 : -1, value.y >= 0 ? 1 : -1); }
float3 decodeOctahedron(float2 encoded)
{
    const float2 value = encoded * 2.0 - 1.0;
    float3 normal = float3(value, 1.0 - abs(value.x) - abs(value.y));
    if (normal.z < 0) normal.xy = (1.0 - abs(normal.yx)) * signNotZero(normal.xy);
    return normalize(normal);
}
// Reconstruct low-resolution irradiance using the geometry at each trace texel.
float3 sampleCurrent(float2 uv)
{
    uint width, height;
    currentIndirect.GetDimensions(width, height);
    const float2 size = float2(width, height);
    if (all(abs(size * pass.inverseResolution - 1.0) < 0.001))
        return currentIndirect.Sample(uv).rgb;
    const float depth = depthTexture.Sample(uv).r;
    if (depth <= 0.000001) return 0.0;
    const float3 position = reconstructWorldPosition(uv, depth, pass.inverseViewProjection, pass.flipY, pass.zeroToOneDepth);
    const float viewDepth = max(-mul(pass.view, float4(position, 1)).z, 0.0);
    const float3 normal = decodeNormal(normalTexture.Sample(uv).xyz);
    const float2 pixel = uv * size - 0.5;
    const float2 base = floor(pixel), fraction = frac(pixel);
    float3 sum = 0.0;
    float weights = 0.0;
    for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 2; ++x)
    {
        const float2 tap = clamp((base + float2(x,y) + 0.5) / size, 0.5 / size, 1.0 - 0.5 / size);
        const float tapDepth = depthTexture.Sample(tap).r;
        if (tapDepth <= 0.000001) continue;
        const float3 tapPosition = reconstructWorldPosition(tap, tapDepth, pass.inverseViewProjection, pass.flipY, pass.zeroToOneDepth);
        // Plane distance accepts sloped surfaces whose view depth changes
        // rapidly across a coarse texel, while rejecting separate surfaces.
        const float planeDistance = abs(dot(tapPosition - position, normal));
        const float similarity = pow(saturate(dot(normal, decodeNormal(normalTexture.Sample(tap).xyz))), 16.0);
        const float weight = (x == 0 ? 1.0 - fraction.x : fraction.x) *
            (y == 0 ? 1.0 - fraction.y : fraction.y) * similarity *
            exp(-planeDistance / max(0.05, viewDepth * 0.02));
        sum += currentIndirect.Sample(tap).rgb * weight;
        weights += weight;
    }
    return weights > 0.00001 ? sum / weights : 0.0;
}
float4 resolveIndirect(float2 uv)
{
    float3 current = sampleCurrent(uv);
    const float depth = depthTexture.Sample(uv).r;
    if (depth <= 0.000001) return float4(current, 0.0);
    const float3 position = reconstructWorldPosition(uv, depth, pass.inverseViewProjection,
                                                      pass.flipY, pass.zeroToOneDepth);
    const float3 normal = decodeNormal(normalTexture.Sample(uv).xyz);
    const float currentViewDepth = max(-mul(pass.view, float4(position, 1.0)).z, 0.0);
    if (pass.hasHistory == 0) return float4(current, currentViewDepth);
    const float2 orientation = float2(1.0, pass.flipY != 0 ? -1.0 : 1.0);
    const float2 motion = motionTexture.Sample(uv).xy * orientation;
    const float2 historyUv = uv - motion;
    if (any(historyUv < 0.0) || any(historyUv > 1.0)) return float4(current, currentViewDepth);
    const float4 metadata = historyMetadata.Sample(historyUv);
    const float expectedDepth = max(-mul(pass.previousView, float4(position, 1.0)).z, 0.0);
    const float allowedDepth = max(0.01, min(pass.depthThreshold, expectedDepth * 0.01));
    const bool valid = metadata.w > 0.5 && abs(metadata.z - expectedDepth) <= allowedDepth &&
                       dot(decodeOctahedron(metadata.xy), normal) >= pass.normalThreshold;
    float3 history = historyColor.Sample(historyUv).rgb;
    float3 minimum = current, maximum = current;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        const float3 sample = currentIndirect.Sample(saturate(uv + float2(x,y) * pass.inverseResolution)).rgb;
        minimum = min(minimum, sample); maximum = max(maximum, sample);
    }
    history = clamp(history, minimum - 0.03, maximum + 0.03);
    const float motionRejection = saturate(length(motion / max(pass.inverseResolution, 0.000001)) * 0.125);
    const float weight = valid ? pass.temporalBlend * (1.0 - motionRejection) : 0.0;
    const float3 resolved = lerp(current, history, weight);
    if (pass.debugView != 0) return float4(valid ? float3(0, weight, 0) : float3(1,0,0), currentViewDepth);
    return float4(max(resolved, 0.0), currentViewDepth);
}

struct TemporalOutput
{
    float4 history : SV_Target0;
    float4 composite : SV_Target1;
};

[shader("fragment")]
TemporalOutput fragmentMain(FullscreenVertexOutput input)
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    TemporalOutput output;
    output.history = resolveIndirect(uv);
    const float4 scene = sceneTexture.Sample(uv);
    float3 indirect = output.history.rgb;
    if (pass.modulateIndirect != 0)
        indirect *= albedoTexture.Sample(uv).rgb * (1.0 - saturate(materialTexture.Sample(uv).r));
    output.composite = pass.indirectOnly != 0
        ? float4(indirect, 1.0)
        : float4(scene.rgb + indirect, scene.a);
    return output;
}
