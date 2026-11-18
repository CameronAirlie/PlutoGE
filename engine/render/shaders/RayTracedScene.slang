// The scene hardware ray queries trace (RayTracingScene): its top-level acceleration structure and a record per traced
// draw. Shared by DDGI's hardware ray source (DDGIRayQueryTrace.slang) and the light visibility mask's ray-traced
// occlusion (LightVisibilityRayQuery.slang).
//
// Bindings: set 0 binding 5 the top-level structure, 6 the instance records.
#ifndef PLUTO_RAY_TRACED_SCENE
#define PLUTO_RAY_TRACED_SCENE

#include "../include/PlutoGE/render/RayTracingConfig.h"

[[vk::binding(5, 0)]] RaytracingAccelerationStructure scene : register(t5, space0);

// Mirrors RayTracedInstance (RayTracingScene.h), indexed by the instance's custom index.
struct RayTracedInstance
{
    float4 albedo;   // rgb diffuse reflectance
    float4 emission; // rgb emitted radiance
    float4 foliage;  // rgb diffuse transmittance, a chance a ray stops at a triangle (1 for solid instances)
};
[[vk::binding(6, 0)]] StructuredBuffer<RayTracedInstance> instances : register(t6, space0);

bool occluded(float3 origin, float3 direction, float distance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = distance;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, PLUTO_RT_MASK_SOLID, ray);
    query.Proceed();
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// A uniform value in [0, 1) for a ray meeting a triangle at `t`. A probe's rays turn at every update, so this varies
// over updates and the blend averages it.
float foliageRandom(float3 origin, float3 direction, float t)
{
    uint state = asuint(direction.x) ^ (asuint(direction.y) * 3u) ^ (asuint(direction.z) * 5u) ^ (asuint(origin.x + origin.z) * 7u) ^
                 (asuint(t) * 11u);
    state = state * 747796405u + 2891336453u;
    state = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    state = (state >> 22u) ^ state;
    return float(state >> 8u) * (1.0 / 16777216.0);
}

// Whether anything blocks the ray between `tMin` and `tMax`: solid surfaces, and leaves with their instance's chance.
bool segmentOccludedThroughFoliage(float3 origin, float3 direction, float tMin, float tMax)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = tMin;
    ray.TMax = tMax;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed())
    {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            foliageRandom(origin, direction, query.CandidateTriangleRayT()) < instances[query.CandidateInstanceID()].foliage.a)
            query.CommitNonOpaqueTriangleHit();
    }
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// Whether a ray reaches `distance`, stopped by solid surfaces and by leaves with their chance.
bool occludedThroughFoliage(float3 origin, float3 direction, float distance)
{
    return segmentOccludedThroughFoliage(origin, direction, 0.0, distance);
}

// The nearest solid surface along a ray, or a negative value.
float solidSurfaceDistance(float3 origin, float3 direction, float reach)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = reach;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, PLUTO_RT_MASK_SOLID, ray);
    while (query.Proceed()) {}
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? query.CommittedRayT() : -1.0;
}

#endif
