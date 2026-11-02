#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"

[[vk::binding(0, 0)]]
cbuffer Parameters : register(b0, space0)
{
    PostProcessParameterBlock pass;
};
[[vk::binding(1, 0)]]
Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]]
Sampler2D depthTexture : register(t2, space0);

#include "CloudNoise.slang"

float cloudSampleOffset(uint index)
{
    // A shared low-discrepancy sequence decorrelates successive strata without
    // introducing pixel-sized noise that cannot be resolved without history.
    return frac(0.5 + float(index) * 0.61803398875);
}
#include "VolumetricCloudDensity.slang"

float densityLocal(float3 worldPosition, float3 local)
{
    CloudDensityParameters cloud;
    cloud.coverage = pass.parameters[0].w;
    cloud.windOffset = pass.parameters[1].xyz;
    cloud.density = pass.parameters[1].w;
    cloud.baseScale = pass.parameters[4].w;
    cloud.detailScale = pass.parameters[5].x;
    cloud.erosion = pass.parameters[5].y;
    return cloudDensityLocal(worldPosition, local, cloud);
}

// Reference entry points retain per-sample transforms for image comparisons.
float densityAt(float3 worldPosition, float4x4 inverseVolume)
{
    return densityLocal(worldPosition, transformWorldToLocalPosition(worldPosition, inverseVolume, pass.flipY));
}

float2 intersectLocalVolume(float3 localOrigin, float3 localDirection)
{
    float nearDistance = -1e30;
    float farDistance = 1e30;
    for (int axis = 0; axis < 3; ++axis)
    {
        // Clamping local direction changes the ray at grazing angles and for
        // large volumes. Handle parallel slabs explicitly instead.
        if (localDirection[axis] == 0.0)
        {
            if (abs(localOrigin[axis]) > 0.5) return float2(1, 0);
            continue;
        }
        const float a = (-0.5 - localOrigin[axis]) / localDirection[axis];
        const float b = (0.5 - localOrigin[axis]) / localDirection[axis];
        nearDistance = max(nearDistance, min(a, b));
        farDistance = min(farDistance, max(a, b));
    }
    return float2(nearDistance, farDistance);
}

float2 intersectVolume(float3 origin, float3 direction, float4x4 inverseVolume)
{
    return intersectLocalVolume(transformWorldToLocalPosition(origin, inverseVolume, pass.flipY),
                                transformWorldToLocalDirection(direction, inverseVolume, pass.flipY));
}

float lightTransmittance(float3 position, float3 lightDirection, float4x4 inverseVolume, uint lightSteps,
                         float3 localPosition, float3 localLightDirection, float3 inverseLightDirection)
{
#ifdef PLUTO_CLOUD_REFERENCE_TRANSFORMS
    const float2 hit = intersectVolume(position + lightDirection * 0.01, lightDirection, inverseVolume);
    const float stepLength = max(hit.y, 0.0) / lightSteps;
#else
    // Density is positive only inside the volume. A sunlight ray therefore
    // needs just its exit distance, not a complete near/far slab intersection.
    // Parallel axes impose no exit constraint; never clamp grazing directions.
    float exitDistance = 1e30;
    [unroll] for (int axis = 0; axis < 3; ++axis)
        if (localLightDirection[axis] != 0.0)
            exitDistance = min(exitDistance,
                ((localLightDirection[axis] > 0.0 ? 0.5 : -0.5) - localPosition[axis]) * inverseLightDirection[axis]);
    const float stepLength = max(exitDistance - 0.01, 0.0) / lightSteps;
#endif
    float opticalDepth = 0.0;
    for (uint index = 0; index < 16u; ++index)
    {
        if (index >= lightSteps)
            break;
        const float distance = (float(index) + 0.5) * stepLength;
        const float3 samplePosition = position + lightDirection * distance;
#ifdef PLUTO_CLOUD_REFERENCE_TRANSFORMS
        opticalDepth += densityAt(samplePosition, inverseVolume) * pass.parameters[2].w * stepLength;
#else
        opticalDepth += densityLocal(samplePosition, localPosition + localLightDirection * distance) * pass.parameters[2].w * stepLength;
#endif
        if (opticalDepth >= 9.21034) // Remaining sunlight is below 0.0001.
            break;
    }
    return exp(-opticalDepth);
}

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id: SV_VertexID)
{
    return fullscreenVertex(id);
}

// RGB is integrated radiance, alpha is transmittance. Keep scene color out
// of the trace so reconstruction never blurs opaque geometry.
float4 integrateCloud(FullscreenVertexOutput input)
{
    const float2 pixel = floor(orientFullscreenUv(input.uv, pass.flipY) / pass.inverseResolution);
    const float2 uv = (pixel + 0.5) * pass.inverseResolution;
    const float4x4 inverseVolume = pass.worldToLocal;
    const float3 rayDirection = reconstructWorldRay(uv, pass.view, pass.projection, pass.flipY);
    const float3 localOrigin = transformWorldToLocalPosition(pass.cameraPosition.xyz, inverseVolume, pass.flipY);
    const float3 localDirection = transformWorldToLocalDirection(rayDirection, inverseVolume, pass.flipY);
    const float2 hit = intersectLocalVolume(localOrigin, localDirection);
    float end = hit.y;
    const float depth = depthTexture.Sample(uv).r;
    if (depth > 0.0)
    {
        // Water contributes reciprocal ray distance to the shared volumetric
        // depth buffer. Clip clouds at that surface, not at the seabed.
        if (pass.parameters[5].w > .5)
            end = min(end, max(1.0/depth-1.0, 0.0));
        else
        {
            const float3 opaquePosition = reconstructWorldPosition(uv, depth, pass.inverseViewProjection,
                                                                    pass.flipY, pass.zeroToOneDepth);
            end = min(end, max(dot(opaquePosition - pass.cameraPosition.xyz, rayDirection), 0.0));
        }
    }
    const float start = max(hit.x, 0.0);
    if (end <= start)
        return float4(0, 0, 0, 1);

    const uint primarySteps = clamp(pass.quality & 255u, 1u, 128u);
    const uint lightSteps = clamp((pass.quality >> 8u) & 255u, 1u, 16u);
    const float stepLength = (end - start) / primarySteps;
    // Vary offsets along the ray, but share them across pixels. Per-pixel
    // random primary and light samples produce screen-space grain, especially
    // at low render scales; this pass has no cloud history to accumulate it.
    float transmittance = 1.0;
    float3 radiance = 0.0;
    const float anisotropy = clamp(pass.parameters[4].y, -0.9, 0.9);
    const float3 lightDirection = normalize(pass.parameters[2].xyz);
    const float3 localLightDirection = transformWorldToLocalDirection(lightDirection, inverseVolume, pass.flipY);
    float3 inverseLightDirection;
    [unroll] for (int axis = 0; axis < 3; ++axis)
        inverseLightDirection[axis] = localLightDirection[axis] != 0.0 ? 1.0 / localLightDirection[axis] : 0.0;
    const float cosine = dot(rayDirection, lightDirection);
    const float phase = (1.0 - anisotropy * anisotropy) /
                        (12.56637061 * pow(max(1.0 + anisotropy * anisotropy - 2.0 * anisotropy * cosine, 0.0001), 1.5));
    // Fade the uniform daylight fill early in twilight. At night, the
    // directional moon term supplies illumination without an emissive floor.
    // Lane 5.z marks a non-solar light; the CPU then pre-scales the fill by
    // the sun's daylight, which this light direction cannot provide.
    const float daylight = pass.parameters[5].z > 0.5 ? 1.0 : smoothstep(-0.12, 0.035, lightDirection.y);
    const float ambientLight = pass.parameters[4].z * daylight * daylight;
    for (uint index = 0; index < 128u; ++index)
    {
        if (index >= primarySteps || transmittance < 0.01)
            break;
        const float distanceAlongRay = start + (float(index) + cloudSampleOffset(index)) * stepLength;
        const float3 samplePosition = pass.cameraPosition.xyz + rayDirection * distanceAlongRay;
        const float3 localPosition = localOrigin + localDirection * distanceAlongRay;
#ifdef PLUTO_CLOUD_REFERENCE_TRANSFORMS
        const float density = densityAt(samplePosition, inverseVolume);
#else
        const float density = densityLocal(samplePosition, localPosition);
#endif
        // An empty segment has exactly zero scattering and extinction.
        if (density <= 0.0)
            continue;
        const float segmentTransmittance = exp(-density * pass.parameters[2].w * stepLength);
        const float sunlight = pass.parameters[3].w > 0.0001
                                   ? lightTransmittance(samplePosition, lightDirection, inverseVolume, lightSteps,
                                                        localPosition, localLightDirection, inverseLightDirection)
                                   : 1.0;
        const float3 incident = ambientLight +
                                pass.parameters[3].rgb * pass.parameters[3].w * phase * sunlight;
        radiance += transmittance * pass.parameters[0].rgb * incident * pass.parameters[4].x * (1.0 - segmentTransmittance);
        transmittance *= segmentTransmittance;
    }
    return float4(radiance, transmittance);
}

[shader("fragment")]
float4 traceMain(FullscreenVertexOutput input) : SV_Target { return integrateCloud(input); }

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float4 scene = sceneTexture.Sample(orientFullscreenUv(input.uv, pass.flipY));
    const float4 volume = integrateCloud(input);
    return float4(scene.rgb * volume.a + volume.rgb, scene.a);
}
