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
    const float3 centerPosition = reconstructWorldPosition(uv, centerDepth, pass.inverseViewProjection);
    const float radius = max(pass.parameters[0].x, 0.05);
    const float bias = max(pass.parameters[0].y, 0.0);
    const uint samples = clamp(pass.quality, 4u, 32u);
    const float2 onePixelUv = saturate(uv + float2(pass.inverseResolution.x, 0.0));
    const float3 onePixelPosition = reconstructWorldPosition(onePixelUv, centerDepth,
                                                             pass.inverseViewProjection);
    const float worldUnitsPerPixel = max(length(onePixelPosition - centerPosition), 0.00001);
    const float radiusPixels = clamp(radius / worldUnitsPerPixel, 1.5, 32.0);
    float occlusion = 0.0;
    for (uint i = 0; i < samples; ++i)
    {
        const float angle = 6.2831853 * float(i) / float(samples) + pass.time * 0.17;
        const float fraction = (float(i) + 0.5) / float(samples);
        const float2 sampleUv = saturate(uv + float2(cos(angle), sin(angle)) *
                                        sqrt(fraction) * radiusPixels * pass.inverseResolution);
        const float sampleDepth = depthTexture.Sample(sampleUv).r;
        if (sampleDepth <= 0.000001) continue;
        const float3 samplePosition = reconstructWorldPosition(sampleUv, sampleDepth,
                                                               pass.inverseViewProjection);
        const float3 delta = samplePosition - centerPosition;
        const float distanceToSample = length(delta);
        const float rangeWeight = saturate(1.0 - distanceToSample / radius);
        const float normalDistance = dot(delta, normal);
        const float horizon = saturate((normalDistance - bias) /
                                       max(distanceToSample, 0.0001));
        occlusion += horizon * rangeWeight;
    }
    const float ao = pow(saturate(1.0 - occlusion / float(samples) * pass.parameters[0].z),
                         max(pass.parameters[0].w, 0.25));
    return pass.parameters[1].y > 0.5 ? float4(ao.xxx, 1.0)
                                      : float4(scene.rgb * ao, scene.a);
}
