#ifndef PLUTO_CLOUD_SHADOW_SAMPLING
#define PLUTO_CLOUD_SHADOW_SAMPLING
// Cloud shadows and overcast light for lit surfaces. Requires the frame
// lighting block (SceneLightingParameters.slang) and skyQuadratureTexture.
//
// Direct light reads the cloud shadow map (CloudShadow.slang), so gaps between
// clouds keep sharp sunlit shadows. Sky light uses the coarse deck estimate
// (CloudOcclusion.slang): the denser the coverage, the more of the clear sky
// the cloud base replaces, until an overcast deck lights the scene alone.
#include "../include/PlutoGE/render/SkyEnvironmentConfig.h"

// Fraction of overcast light a downward-facing receiver sees back from the ground.
static const float CloudOvercastGroundReflectance = 0.3;

bool cloudLightingActive() { return cloudOcclusion.volumes[0].w > 0.0; }

// Direct-light transmittance at position. coarse is the deck estimate used
// beyond the map, faded in over its outer edge so the boundary never shows.
float sampleCloudShadow(float3 position, float coarse)
{
    if (cloudShadowLight.w <= 0.5)
        return coarse;
    const float3 light = cloudShadowLight.xyz;
    const float2 planar = position.xz + light.xz * ((cloudShadowMapping.w - position.y) / light.y);
    const float2 texel = (planar - cloudShadowMapping.xy) * cloudShadowMapping.z;
    const float2 edge = abs(texel * (2.0 / PLUTO_SKY_CLOUD_SHADOW_SIZE) - 1.0);
    const float outside = smoothstep(0.8, 1.0, max(edge.x, edge.y));
    if (outside >= 1.0)
        return coarse;
    // Clamp to texel centres so bilinear taps never reach the sky rows.
    const float2 clamped = clamp(texel, 0.5, PLUTO_SKY_CLOUD_SHADOW_SIZE - 0.5);
    const float mapped = skyQuadratureTexture.SampleLevel(
        float2(clamped.x, clamped.y + PLUTO_SKY_CLOUD_SHADOW_Y) / float2(PLUTO_SKY_ATLAS_WIDTH, PLUTO_SKY_ATLAS_HEIGHT),
        0.0).r;
    return lerp(saturate(mapped), coarse, outside);
}

// Irradiance on normal from the cloud base's radiance over the upper
// hemisphere and its reflection from the ground below.
float3 cloudOvercastIrradiance(float3 overcast, float3 normal)
{
    const float up = 0.5 + 0.5 * normal.y;
    return PI * overcast * (up + CloudOvercastGroundReflectance * (1.0 - up));
}

// Clear-sky light (irradiance or radiance) with the part the clouds block
// replaced by the cloud base's.
float3 cloudySky(float3 clearSky, float3 overcast, CloudLighting cloud)
{
    return clearSky * cloud.sky + overcast * (1.0 - cloud.sky);
}
#endif
