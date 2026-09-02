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
};
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { TemporalParameters pass; };
[[vk::binding(1, 0)]] Sampler2D currentIndirect : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[[vk::binding(5, 0)]] Sampler2D motionTexture : register(t5, space0);
[[vk::binding(6, 0)]] Sampler2D historyColor : register(t6, space0);
[[vk::binding(7, 0)]] Sampler2D historyMetadata : register(t7, space0);
[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }
float2 signNotZero(float2 value) { return float2(value.x >= 0 ? 1 : -1, value.y >= 0 ? 1 : -1); }
float3 decodeOctahedron(float2 encoded)
{
    const float2 value = encoded * 2.0 - 1.0;
    float3 normal = float3(value, 1.0 - abs(value.x) - abs(value.y));
    if (normal.z < 0) normal.xy = (1.0 - abs(normal.yx)) * signNotZero(normal.xy);
    return normalize(normal);
}
[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    float3 current = currentIndirect.Sample(uv).rgb;
    const float depth = depthTexture.Sample(uv).r;
    if (depth <= 0.000001) return float4(current, 0.0);
    const float3 position = reconstructWorldPosition(uv, depth, pass.inverseViewProjection,
                                                      pass.flipY, pass.zeroToOneDepth);
    const float3 normal = decodeNormal(normalTexture.Sample(uv).xyz);
    const float currentViewDepth = max(-mul(pass.view, float4(position, 1.0)).z, 0.0);
    if (pass.hasHistory == 0) return float4(current, currentViewDepth);
    const float2 orientation = float2(1.0, pass.flipY != 0 ? -1.0 : 1.0);
    const float2 motion = (motionTexture.Sample(uv).xy * 2.0 - 1.0) * orientation;
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
