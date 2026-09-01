#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }
[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 scene = sceneTexture.Sample(uv);
    const float centerDepth = depthTexture.Sample(uv).r;
    if (centerDepth <= 0.000001) return scene;
    const float3 normal = decodeNormal(normalTexture.Sample(uv).xyz);
    const float radius = max(pass.parameters[0].x, 0.05);
    const float bias = max(pass.parameters[0].y, 0.0);
    const uint samples = clamp(pass.quality, 4u, 32u);
    float occlusion = 0.0;
    for (uint i = 0; i < samples; ++i)
    {
        const float angle = 6.2831853 * float(i) / float(samples) + pass.time * 0.17;
        const float fraction = (float(i) + 0.5) / float(samples);
        const float2 offset = float2(cos(angle), sin(angle)) * sqrt(fraction) *
                              radius * pass.inverseResolution * 18.0;
        const float sampleDepth = depthTexture.Sample(saturate(uv + offset)).r;
        const float3 sampleNormal = decodeNormal(normalTexture.Sample(saturate(uv + offset)).xyz);
        const float depthDelta = sampleDepth - centerDepth;
        occlusion += (depthDelta > bias * 0.001 ? 1.0 : 0.0) *
                     saturate(dot(normal, sampleNormal) * 0.5 + 0.5);
    }
    const float ao = pow(saturate(1.0 - occlusion / float(samples) * pass.parameters[0].z),
                         max(pass.parameters[0].w, 0.25));
    return pass.parameters[1].y > 0.5 ? float4(ao.xxx, 1.0)
                                      : float4(scene.rgb * ao, scene.a);
}
