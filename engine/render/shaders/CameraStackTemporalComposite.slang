#include "Fullscreen.slang"
#include "ScreenSpaceCommon.slang"

[[vk::binding(0, 0)]] cbuffer CameraStackTemporalParameters : register(b0, space0)
{
    column_major float4x4 inverseViewProjection;
    column_major float4x4 previousViewProjection;
    // x: backend flip, y: zero-to-one depth, z: camera layer.
    float4 options;
    float4 clipJitter;
};
[[vk::binding(1, 0)]] Sampler2D cameraColor : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D cameraDepth : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D cameraNormal : register(t3, space0);
[[vk::binding(4, 0)]] Sampler2D cameraMotion : register(t4, space0);

[shader("vertex")] FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenVertex(vertexId);
}

float2 temporalMetadata(float2 uv, float depth)
{
    const float2 jitterUv = -0.5 * clipJitter.xy * float2(1.0, options.x > 0.5 ? -1.0 : 1.0);
    const float3 world = reconstructWorldPosition(uv + jitterUv, depth, inverseViewProjection,
        uint(options.x), uint(options.y));
    const float4 previousClip = mul(previousViewProjection, float4(world, 1.0));
    const float previousDepth = previousClip.z / max(previousClip.w, 0.000001);
    return float2(options.y > 0.5 ? previousDepth : previousDepth * 0.5 + 0.5, options.z);
}

[shader("fragment")] float2 metadataMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, uint(options.x));
    return temporalMetadata(uv, cameraDepth.SampleLevel(uv, 0).r);
}

struct CompositeOutput
{
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
    float2 motion : SV_Target2;
    float2 metadata : SV_Target3;
    float depth : SV_Depth;
};

[shader("fragment")] CompositeOutput fragmentMain(FullscreenVertexOutput input)
{
    const float2 uv = orientFullscreenUv(input.uv, uint(options.x));
    const float depth = cameraDepth.SampleLevel(uv, 0).r;
    if (depth <= 0.000001)
        discard;
    CompositeOutput output;
    output.color = cameraColor.SampleLevel(uv, 0);
    output.normal = cameraNormal.SampleLevel(uv, 0);
    output.motion = cameraMotion.SampleLevel(uv, 0).xy;
    output.metadata = temporalMetadata(uv, depth);
    output.depth = depth;
    return output;
}

[shader("fragment")] float4 transparencyMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, uint(options.x));
    if (cameraDepth.SampleLevel(uv, 0).r > 0.000001)
        discard;
    const float4 color = cameraColor.SampleLevel(uv, 0);
    if (color.a <= 0.000001)
        discard;
    return color; // Premultiplied radiance and coverage from particle rendering.
}
