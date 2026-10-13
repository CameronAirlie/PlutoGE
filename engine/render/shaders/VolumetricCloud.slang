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

uint cloudHash(uint value)
{
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
float hash31(float3 p)
{
    // Lattice corners must agree across cells and compiler contraction modes.
    const int3 cell = int3(p);
    const uint seed = uint(cell.x) * 0x8da6b343u ^ uint(cell.y) * 0xd8163841u ^ uint(cell.z) * 0xcb1ab31fu;
    return float(cloudHash(seed) >> 8) * (1.0 / 16777216.0);
}
float cloudSampleOffset(uint index)
{
    // A shared low-discrepancy sequence decorrelates successive strata without
    // introducing pixel-sized noise that cannot be resolved without history.
    return frac(0.5 + float(index) * 0.61803398875);
}
float valueNoise(float3 p)
{
    const float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(lerp(hash31(i), hash31(i + float3(1, 0, 0)), f.x),
                     lerp(hash31(i + float3(0, 1, 0)), hash31(i + float3(1, 1, 0)), f.x), f.y),
                lerp(lerp(hash31(i + float3(0, 0, 1)), hash31(i + float3(1, 0, 1)), f.x),
                     lerp(hash31(i + float3(0, 1, 1)), hash31(i + 1.0), f.x), f.y),
                f.z);
}
float fbm(float3 p)
{
    return valueNoise(p) * 0.55 + valueNoise(p * 2.03 + float3(17.1, 9.2, 13.7)) * 0.275 + valueNoise(p * 4.12 + 9.7) * 0.1375;
}

float baseCloudDensity(float3 p, float coverage)
{
    const float threshold = 1.0 - coverage;
#ifdef PLUTO_CLOUD_REFERENCE_TRANSFORMS
    return saturate((fbm(p) - threshold) / coverage);
#else
    // Each value-noise octave is in [0,1]. Reject only samples whose maximum
    // possible remaining contribution cannot reach the coverage threshold.
    // A small conservative margin keeps rounding at the bound from rejecting
    // visible density. Surviving samples retain the original octave order.
    float noise = valueNoise(p) * 0.55;
    if (noise + (0.275 + 0.1375) < threshold - 0.000001) return 0.0;
    noise += valueNoise(p * 2.03 + float3(17.1, 9.2, 13.7)) * 0.275;
    if (noise + 0.1375 < threshold - 0.000001) return 0.0;
    noise += valueNoise(p * 4.12 + 9.7) * 0.1375;
    return saturate((noise - threshold) / coverage);
#endif
}

float densityLocal(float3 worldPosition, float3 local)
{
    if (any(abs(local) > 0.5))
        return 0.0;
    const float height = local.y + 0.5;
    const float2 edge = 0.5 - abs(local.xz);
    const float shape = smoothstep(0.0, 0.14, height) * (1.0 - smoothstep(0.62, 1.0, height)) *
                        smoothstep(0.0, 0.07, min(edge.x, edge.y));
    if (shape <= 0.0 || pass.parameters[1].w <= 0.0)
        return 0.0;
    const float coverage = max(pass.parameters[0].w, 0.001);
    float density = baseCloudDensity((worldPosition + pass.parameters[1].xyz) * pass.parameters[4].w, coverage);
    if (density > 0.001 && pass.parameters[5].y > 0.001)
        density = saturate(density - (1.0 - valueNoise((worldPosition + pass.parameters[1].xyz) *
                                                           pass.parameters[5].x +
                                                       float3(31.4, 7.8, 19.2))) *
                                         pass.parameters[5].y * (1.0 - density));
    return density * shape * pass.parameters[1].w;
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
    const float sunHeight = lightDirection.y * (pass.parameters[5].z > 0.5 ? -1.0 : 1.0);
    const float daylight = smoothstep(-0.12, 0.035, sunHeight);
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
