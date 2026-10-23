#ifndef PLUTO_CLOUD_OCCLUSION
#define PLUTO_CLOUD_OCCLUSION
#include "../include/PlutoGE/render/CloudOcclusionLayout.h"

// Coarse cloud occlusion for lighting outside the cloud pass. Each volume is
// the box the cloud pass traces, with its noise replaced by the noise's mean
// extinction (CloudOcclusion.cpp). Media under a cloud deck therefore dim and
// take the deck's colour without tracing cloud noise per sample. Gaps between
// individual clouds do not cast light shafts.
struct CloudOcclusionParameters
{
    column_major float4x4 worldToLocal[PLUTO_CLOUD_OCCLUSION_VOLUMES];
    // rgb: radiance scattered down from the cloud base. w: mean extinction
    // per world unit; zero marks an unused volume.
    float4 volumes[PLUTO_CLOUD_OCCLUSION_VOLUMES];
};

struct CloudLighting
{
    float sun;       // Direct-light transmittance through the clouds.
    float sky;       // Fraction of diffuse sky light that passes the clouds.
    float3 overcast; // Radiance the clouds scatter in place of the blocked sky.
};

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

CloudLighting evaluateCloudLighting(CloudOcclusionParameters clouds, float3 position, float3 sunDirection)
{
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
        sunDepth += extinction * cloudOcclusionChord(clouds.worldToLocal[index], position, sunDirection);
        const float zenithDepth = diffusivity * extinction *
                                  cloudOcclusionChord(clouds.worldToLocal[index], position, float3(0.0, 1.0, 0.0));
        skyDepth += zenithDepth;
        const float blocked = 1.0 - exp(-zenithDepth);
        overcast += clouds.volumes[index].rgb * blocked;
        overcastWeight += blocked;
    }
    CloudLighting result;
    result.sun = exp(-sunDepth);
    result.sky = exp(-skyDepth);
    result.overcast = overcastWeight > 0.0 ? overcast / overcastWeight : float3(0.0, 0.0, 0.0);
    return result;
}
#endif
