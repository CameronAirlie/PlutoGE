// The light visibility mask with world tests through the voxel scene (VoxelLightOcclusionSource): each traced slot
// marches the segment from the receiver to its light through the VCT cascades DDGI keeps (VoxelSceneSampling.slang).
// Every backend runs it. Its detail is the voxel's (tens of centimetres): it stops light crossing walls, floors and
// terrain, while small occluders are left to contact shadows.
#include "LightVisibilityCommon.slang"
#include "VoxelSceneSampling.slang"

// Most samples a segment takes; longer segments step further.
static const uint VoxelVisibilitySteps = 48;

float worldVisibility(float3 position, float3 normal, float viewDepth, uint slot, float2 noise)
{
    float covered, voxel;
    const float4 receiver = sampleScene(position, normal, 0.0, covered, voxel);
    // Outside the voxel scene nothing is known: leave the light as it is.
    if (covered <= 0.0)
        return 1.0;
    // The receiver's own surface fills the voxels it lies in, and the light's fixture (or the wall it hangs on) the
    // ones around the light: start a voxel off the surface and stop a voxel short of the emitter.
    const float3 from = position + normal * max(visibilityTrace.z, voxel);
    const float3 target = visibilityEmitterPoint(slot, position, noise);
    const float3 delta = target - from;
    const float segment = length(delta);
    const float start = max(visibilityTrace.x + visibilityTrace.y * viewDepth, 0.5 * voxel);
    const float end = segment - max(slotSettings[slot].z, voxel);
    if (end <= start)
        return 1.0;
    const float3 direction = delta / segment;
    float transmission = 1.0;
    float travelled = start + noise.x * 0.5 * voxel;
    for (uint step = 0; step < VoxelVisibilitySteps && travelled < end; ++step)
    {
        float sampleCovered, sampleVoxel;
        const float opacity = sampleScene(from + direction * travelled, direction, 0.0, sampleCovered, sampleVoxel).a;
        if (sampleCovered <= 0.0)
            break;
        // As segmentVisibility: only opacity above one half counts, so a segment running beside a wall within half a
        // voxel of it is not blocked by filtering.
        transmission *= 1.0 - saturate(opacity * 2.0 - 1.0);
        if (transmission < 0.02)
            return 0.0;
        travelled += max(0.5 * sampleVoxel, (end - start) / float(VoxelVisibilitySteps));
    }
    return transmission;
}

#include "LightVisibilityPass.slang"
