// Cloud shadow map (CloudShadowMap.h): sun transmittance through the cloud
// volumes, written into its region of the sky lighting atlas. Each texel
// traces the cloud pass's own density along the light from its point on the
// map plane, so ground shadows follow the clouds drawn above them.
#include "Fullscreen.slang"
#include "../include/PlutoGE/render/CloudOcclusionLayout.h"
#include "../include/PlutoGE/render/SkyEnvironmentConfig.h"

struct CloudShadowVolume
{
    column_major float4x4 worldToLocal;
    float4 wind;   // xyz wind offset, w density
    float4 shape;  // coverage, shadow extinction per world unit, base scale, detail scale
    float4 detail; // detail erosion
};

[[vk::binding(0, 0)]] cbuffer CloudShadowParameters : register(b0, space0)
{
    float4 mapping; // origin x/z, world units per texel, plane height
    float4 light;   // xyz toward the light, w volume count
    CloudShadowVolume volumes[PLUTO_CLOUD_OCCLUSION_VOLUMES];
};

#define PLUTO_CLOUD_NOISE_VOLUME 1
#include "CloudNoise.slang"
#include "VolumetricCloudDensity.slang"

// Enough steps to resolve the base noise across a deck at the lowest projected
// elevation; the map is bilinearly filtered and has no history.
static const uint CloudShadowSteps = 24;

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target0
{
    // Pixel coordinates address the atlas the same way its readers do on
    // every backend; the sky rows above keep their contents.
    const int2 pixel = int2(input.position.xy);
    const int2 texel = pixel - int2(0, PLUTO_SKY_CLOUD_SHADOW_Y);
    if (any(texel < 0) || any(texel >= PLUTO_SKY_CLOUD_SHADOW_SIZE))
        discard;
    const float3 origin = float3(mapping.x + (float(texel.x) + 0.5) * mapping.z, mapping.w,
                                 mapping.y + (float(texel.y) + 0.5) * mapping.z);
    float opticalDepth = 0.0;
    const uint count = min(uint(light.w), uint(PLUTO_CLOUD_OCCLUSION_VOLUMES));
    for (uint index = 0; index < count; ++index)
    {
        const CloudShadowVolume volume = volumes[index];
        const float3 localOrigin = mul(volume.worldToLocal, float4(origin, 1.0)).xyz;
        const float3 localDirection = mul(volume.worldToLocal, float4(light.xyz, 0.0)).xyz;
        // Slab intersection in world distance: the local direction is not normalized.
        float nearDistance = 0.0, farDistance = 1e30;
        [unroll] for (int axis = 0; axis < 3; ++axis)
        {
            if (localDirection[axis] == 0.0)
            {
                if (abs(localOrigin[axis]) > 0.5) farDistance = -1.0;
                continue;
            }
            const float a = (-0.5 - localOrigin[axis]) / localDirection[axis];
            const float b = (0.5 - localOrigin[axis]) / localDirection[axis];
            nearDistance = max(nearDistance, min(a, b));
            farDistance = min(farDistance, max(a, b));
        }
        if (farDistance <= nearDistance)
            continue;
        CloudDensityParameters cloud;
        cloud.coverage = volume.shape.x;
        cloud.windOffset = volume.wind.xyz;
        cloud.density = volume.wind.w;
        cloud.baseScale = volume.shape.z;
        cloud.detailScale = volume.shape.w;
        cloud.erosion = volume.detail.x;
        const float stepLength = (farDistance - nearDistance) / CloudShadowSteps;
        for (uint step = 0; step < CloudShadowSteps; ++step)
        {
            // The cloud pass's shared stratified offsets: smooth across texels.
            const float distance = nearDistance + (float(step) + frac(0.5 + float(step) * 0.61803398875)) * stepLength;
            opticalDepth += cloudDensityLocal(origin + light.xyz * distance, localOrigin + localDirection * distance,
                                              cloud) * volume.shape.y * stepLength;
        }
        if (opticalDepth >= 9.21034) // Remaining sunlight is below 0.0001.
            break;
    }
    return float4(exp(-opticalDepth), opticalDepth, 0.0, 1.0);
}
