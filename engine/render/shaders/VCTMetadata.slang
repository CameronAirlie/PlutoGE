#include "Fullscreen.slang"
#include "ScreenSpaceCommon.slang"
struct MetadataParameters
{
    column_major float4x4 inverseViewProjection;
    column_major float4x4 view;
    uint flipY;
    uint3 padding;
};
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { MetadataParameters pass; };
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }
float2 signNotZero(float2 value) { return float2(value.x >= 0 ? 1 : -1, value.y >= 0 ? 1 : -1); }
float2 encodeNormal(float3 normal)
{
    normal /= max(abs(normal.x) + abs(normal.y) + abs(normal.z), 0.0001);
    if (normal.z < 0) normal.xy = (1.0 - abs(normal.yx)) * signNotZero(normal.xy);
    return normal.xy * 0.5 + 0.5;
}
[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float depth = depthTexture.Sample(uv).r;
    if (depth <= 0.000001) return 0.0;
    const float3 position = reconstructWorldPosition(uv, depth, pass.inverseViewProjection, pass.flipY);
    const float3 normal = decodeNormal(normalTexture.Sample(uv).xyz);
    return float4(encodeNormal(normal), max(-mul(pass.view, float4(position, 1.0)).z, 0.0), 1.0);
}
