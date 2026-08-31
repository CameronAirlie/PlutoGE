#include "Fullscreen.slang"
#include "PostProcessParameters.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(5, 0)]] Sampler2D motionTexture : register(t5, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID) { return fullscreenVertex(vertexId); }

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 center = sceneTexture.Sample(uv);
    float2 velocity = (motionTexture.Sample(uv).xy * 2.0 - 1.0) * pass.parameters[0].x * pass.parameters[0].y;
    const float maximumUvRadius = pass.parameters[0].z * max(pass.inverseResolution.x, pass.inverseResolution.y);
    const float lengthSquared = dot(velocity, velocity);
    if (lengthSquared < pass.parameters[0].w * pass.parameters[0].w * 0.000001)
        return center;
    const float velocityLength = sqrt(lengthSquared);
    velocity *= min(1.0, maximumUvRadius / max(velocityLength, 0.000001));
    const uint sampleCount = pass.quality == 0 ? 5 : (pass.quality == 1 ? 9 : 13);
    float3 accumulated = center.rgb * max(pass.parameters[1].x, 0.01);
    float weight = max(pass.parameters[1].x, 0.01);
    for (uint sampleIndex = 1; sampleIndex < sampleCount; ++sampleIndex)
    {
        const float position = float(sampleIndex) / float(sampleCount - 1) - 0.5;
        accumulated += sceneTexture.Sample(saturate(uv + velocity * position)).rgb;
        weight += 1.0;
    }
    return float4(accumulated / weight, center.a);
}
