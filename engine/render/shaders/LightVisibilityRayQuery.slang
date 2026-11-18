// The light visibility mask with world tests against the ray-traced scene (RayQueryLightOcclusionSource): one inline
// ray query per traced slot and pixel, from the receiver to a point on its light's emitter, against the triangles
// DDGI's hardware ray source keeps (RayTracedScene.slang). Leaves stop a ray with their instance's chance. Vulkan with
// ray queries only.
#include "LightVisibilityCommon.slang"
#include "RayTracedScene.slang"

// The emitter's own fixture (a lamp's shade, a torch's head) sits within this of the light and must not block it.
static const float FixtureClearance = 0.05;

float worldVisibility(float3 position, float3 normal, float viewDepth, uint slot, float2 noise)
{
    const float3 origin = position + normal * visibilityTrace.z;
    const float3 target = visibilityEmitterPoint(slot, position, noise);
    const float3 delta = target - origin;
    const float distance = length(delta);
    // The traced geometry is a draw's coarsest LOD and the receiver is reconstructed from depth, so the first stretch
    // could meet the receiver's own surface: contact shadows cover it instead.
    const float start = visibilityTrace.x + visibilityTrace.y * viewDepth;
    const float end = distance - max(slotSettings[slot].z, FixtureClearance);
    if (end <= start)
        return 1.0;
    return segmentOccludedThroughFoliage(origin, delta / distance, start, end) ? 0.0 : 1.0;
}

#include "LightVisibilityPass.slang"
