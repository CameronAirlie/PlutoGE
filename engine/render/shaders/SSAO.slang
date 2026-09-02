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
    const float centerDepth = depthTexture.Sample(uv).r;
    if (centerDepth <= 0.000001)
        return float4(1.0, 1.0, 1.0, 1.0);
    const float centerLinearDepth = linearViewDepth(centerDepth,
                                                    pass.parameters[5].x,
                                                    pass.parameters[5].y);
    const float3 normal = decodeNormal(normalTexture.Sample(uv).xyz);
    const float3 centerPosition = reconstructWorldPosition(uv, centerDepth,
                                                           pass.inverseViewProjection,
                                                           pass.flipY, pass.zeroToOneDepth);
    const float noiseAngle = frac(52.9829189 * frac(dot(floor(input.position.xy),
                                                       float2(0.06711056, 0.00583715)))) * 6.2831853;
    const float radius = max(pass.parameters[0].x, 0.05);
    const float bias = max(pass.parameters[0].y, 0.0);
    const uint samples = clamp(pass.quality, 4u, 32u);
    // Project the world-space radius at the receiver depth. A minimum footprint
    // keeps distant AO stable, while the upper bound prevents a near-plane
    // receiver from scanning an excessive portion of the screen.
    const float2 projectedRadius = clamp(
        radius * abs(float2(pass.projection[0][0], pass.projection[1][1])) /
            max(centerLinearDepth * 2.0, 0.001),
        pass.inverseResolution * 2.0, float2(0.15, 0.15));
    float occlusion = 0.0;
    float sampleWeight = 0.0;
    for (uint i = 0; i < samples; ++i)
    {
        const float fraction = (float(i) + 0.5) / float(samples);
        const float angle = noiseAngle + float(i) * 2.39996323;
        const float radial = sqrt(fraction);
        const float2 sampleUv = uv + float2(cos(angle), sin(angle)) *
                                     projectedRadius * radial;
        if (any(sampleUv < 0.0) || any(sampleUv > 1.0)) continue;
        const float sceneDepth = depthTexture.Sample(sampleUv).r;
        if (sceneDepth <= 0.000001) continue;
        const float3 samplePosition = reconstructWorldPosition(sampleUv, sceneDepth,
                                                               pass.inverseViewProjection,
                                                               pass.flipY, pass.zeroToOneDepth);
        const float3 separation = samplePosition - centerPosition;
        const float distanceToSample = length(separation);
        const float rangeWeight = 1.0 - smoothstep(radius * 0.5, radius,
                                                   distanceToSample);
        if (rangeWeight <= 0.0) continue;
        // Camera-relative depth differences make a flat receding plane appear
        // occluded. Test against the receiver's tangent plane instead: only a
        // sample lying above the surface can block its ambient hemisphere.
        const float heightAboveSurface = dot(separation, normal);
        const float sampleOccluded = saturate(
            (heightAboveSurface - bias) / max(distanceToSample, 0.001));
        occlusion += sampleOccluded * rangeWeight;
        sampleWeight += rangeWeight;
    }
    float ao = 1.0 - occlusion / max(sampleWeight, 1.0) * pass.parameters[0].z;
    ao = pow(saturate(ao), max(pass.parameters[0].w, 0.25));
    return float4(ao.xxx, 1.0);
}
