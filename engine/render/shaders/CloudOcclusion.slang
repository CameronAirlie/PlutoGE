#ifndef PLUTO_CLOUD_OCCLUSION
#define PLUTO_CLOUD_OCCLUSION
#include "../include/PlutoGE/render/CloudOcclusionLayout.h"
#include "../include/PlutoGE/render/SkyEnvironmentConfig.h"

// Cloud lighting for everything outside the cloud pass (docs/CLOUD_SHADOWS.md).
//
// Each volume is the box the cloud pass traces, with its noise replaced by the
// noise's mean extinction (CloudOcclusion.cpp): media and surfaces under a deck
// dim and take the deck's colour without tracing cloud noise per sample. That
// estimate is uniform across a deck, so shaders that can read the cloud shadow
// map (CloudShadow.slang) use it for direct light instead: define
// PLUTO_CLOUD_SHADOW_ATLAS as the sky lighting atlas sampler before including
// this file. Gaps between clouds then keep sunlight and cast light shafts.
struct CloudOcclusionParameters
{
    // Named apart from other worldToLocal members: OpenGL links uniform blocks by member name.
    column_major float4x4 volumeWorldToLocal[PLUTO_CLOUD_OCCLUSION_VOLUMES];
    // rgb: radiance scattered down from the cloud base. w: mean extinction
    // per world unit; zero marks an unused volume.
    float4 volumes[PLUTO_CLOUD_OCCLUSION_VOLUMES];
    // Cloud shadow map (CloudShadowMap::SurfaceMapping): origin x/z, texels per
    // world unit, plane height; and the direction it was projected along, w 1
    // when the map is present this frame.
    float4 shadowMapping;
    float4 shadowLight;
};

struct CloudLighting
{
    float sun;       // Direct-light transmittance through the clouds.
    float sky;       // Fraction of diffuse sky light that passes the clouds.
    float3 overcast; // Radiance the clouds scatter in place of the blocked sky.
};

// Light unaffected by clouds.
CloudLighting clearCloudLighting()
{
    CloudLighting result;
    result.sun = 1.0;
    result.sky = 1.0;
    result.overcast = float3(0.0, 0.0, 0.0);
    return result;
}

// Volumes pack from index zero, so an empty first slot means no clouds.
bool cloudLightingActive(CloudOcclusionParameters clouds) { return clouds.volumes[0].w > 0.0; }

// World-space length of the ray from position along direction inside the
// volume's unit box. The local direction is not normalized, so slab distances
// stay in world units.
float cloudOcclusionChord(float4x4 worldToLocal, float3 position, float3 direction)
{
    const float3 origin = mul(worldToLocal, float4(position, 1.0)).xyz;
    const float3 localDirection = mul(worldToLocal, float4(direction, 0.0)).xyz;
    float nearDistance = 0.0;
    float farDistance = 1e30;
    [unroll] for (int axis = 0; axis < 3; ++axis)
    {
        if (localDirection[axis] == 0.0)
        {
            if (abs(origin[axis]) > 0.5) return 0.0;
            continue;
        }
        const float a = (-0.5 - origin[axis]) / localDirection[axis];
        const float b = (0.5 - origin[axis]) / localDirection[axis];
        nearDistance = max(nearDistance, min(a, b));
        farDistance = min(farDistance, max(a, b));
    }
    return max(farDistance - nearDistance, 0.0);
}

#ifdef PLUTO_CLOUD_SHADOW_ATLAS
// Direct-light transmittance at position from the cloud shadow map. coarse is
// the deck estimate used beyond the map, faded in over its outer edge so the
// boundary never shows.
float sampleCloudShadow(CloudOcclusionParameters clouds, float3 position, float coarse)
{
    if (clouds.shadowLight.w <= 0.5)
        return coarse;
    const float3 light = clouds.shadowLight.xyz;
    const float2 planar = position.xz + light.xz * ((clouds.shadowMapping.w - position.y) / light.y);
    const float2 texel = (planar - clouds.shadowMapping.xy) * clouds.shadowMapping.z;
    const float2 edge = abs(texel * (2.0 / PLUTO_SKY_CLOUD_SHADOW_SIZE) - 1.0);
    const float outside = smoothstep(0.8, 1.0, max(edge.x, edge.y));
    if (outside >= 1.0)
        return coarse;
    // Clamp to texel centres so bilinear taps never reach the sky rows.
    const float2 clamped = clamp(texel, 0.5, PLUTO_SKY_CLOUD_SHADOW_SIZE - 0.5);
    const float mapped = PLUTO_CLOUD_SHADOW_ATLAS.SampleLevel(
        float2(clamped.x, clamped.y + PLUTO_SKY_CLOUD_SHADOW_Y) / float2(PLUTO_SKY_ATLAS_WIDTH, PLUTO_SKY_ATLAS_HEIGHT),
        0.0).r;
    return lerp(saturate(mapped), coarse, outside);
}
#endif

// sunDirection is the directional light's; the cloud shadow map is traced along it.
CloudLighting evaluateCloudLighting(CloudOcclusionParameters clouds, float3 position, float3 sunDirection)
{
    if (!cloudLightingActive(clouds))
        return clearCloudLighting();
    // Diffuse light crosses a slab along slanted paths. The two-stream
    // diffusivity factor converts the zenith depth into that mean depth.
    const float diffusivity = 1.66;
    float sunDepth = 0.0;
    float skyDepth = 0.0;
    float3 overcast = 0.0;
    float overcastWeight = 0.0;
    [unroll] for (int index = 0; index < PLUTO_CLOUD_OCCLUSION_VOLUMES; ++index)
    {
        const float extinction = clouds.volumes[index].w;
        if (extinction <= 0.0) continue;
        sunDepth += extinction * cloudOcclusionChord(clouds.volumeWorldToLocal[index], position, sunDirection);
        const float zenithDepth = diffusivity * extinction *
                                  cloudOcclusionChord(clouds.volumeWorldToLocal[index], position, float3(0.0, 1.0, 0.0));
        skyDepth += zenithDepth;
        const float blocked = 1.0 - exp(-zenithDepth);
        overcast += clouds.volumes[index].rgb * blocked;
        overcastWeight += blocked;
    }
    CloudLighting result;
    result.sun = exp(-sunDepth);
#ifdef PLUTO_CLOUD_SHADOW_ATLAS
    result.sun = sampleCloudShadow(clouds, position, result.sun);
#endif
    result.sky = exp(-skyDepth);
    result.overcast = overcastWeight > 0.0 ? overcast / overcastWeight : float3(0.0, 0.0, 0.0);
    return result;
}

// Fraction of overcast light a downward-facing receiver sees back from the ground.
static const float CloudOvercastGroundReflectance = 0.3;

// Cosine-weighted mean radiance on normal (irradiance / pi) from the cloud
// base over the upper hemisphere and its reflection from the ground below.
float3 cloudOvercastRadiance(float3 overcast, float3 normal)
{
    const float up = 0.5 + 0.5 * normal.y;
    return overcast * (up + CloudOvercastGroundReflectance * (1.0 - up));
}

// Clear-sky light (irradiance or radiance) with the part the clouds block
// replaced by the cloud base's.
float3 cloudySky(float3 clearSky, float3 overcast, CloudLighting cloud)
{
    return clearSky * cloud.sky + overcast * (1.0 - cloud.sky);
}
#endif
