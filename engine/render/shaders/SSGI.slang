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
    const float depth = depthTexture.Sample(uv).r;
    if (depth <= 0.000001) return scene;
    const float3 normal = decodeNormal(normalTexture.Sample(uv).xyz);
    const uint samples = clamp(pass.quality, 2u, 16u);
    float3 indirect = 0.0;
    float weight = 0.0;
    for (uint i = 0; i < samples; ++i)
    {
        const float angle = 6.2831853 * (float(i) + 0.5) / float(samples);
        const float2 direction = float2(cos(angle), sin(angle));
        const float2 sampleUv = saturate(uv + direction * pass.parameters[0].y *
                                         pass.inverseResolution * (1.0 + float(i)));
        const float sampleDepth = depthTexture.Sample(sampleUv).r;
        const float3 sampleNormal = decodeNormal(normalTexture.Sample(sampleUv).xyz);
        const float geometryWeight = saturate(dot(normal, sampleNormal) * 0.5 + 0.5) *
                                     exp(-abs(sampleDepth - depth) * 80.0);
        indirect += max(sceneTexture.Sample(sampleUv).rgb - scene.rgb * 0.25, 0.0) * geometryWeight;
        weight += geometryWeight;
    }
    indirect = indirect / max(weight, 0.0001) * max(pass.parameters[0].x, 0.0);
    return pass.parameters[1].z > 0.5 ? float4(indirect, 1.0)
                                      : float4(scene.rgb + indirect, scene.a);
}
