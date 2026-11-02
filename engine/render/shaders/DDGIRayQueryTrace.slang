// DDGI ray source backed by hardware ray tracing (RayQueryDdgiRaySource): traces each probe ray against the scene's
// triangles (RayTracingScene's top-level acceleration structure) with an inline ray query. Same bindings and output
// as DDGIVoxelTrace.slang, which documents the contract:
//   rgb  radiance arriving along the ray
//   a    distance to the first surface, PLUTO_DDGI_MISS_DISTANCE for a ray that sees the sky, or minus the distance
//        to a back face when the probe starts inside geometry.
//
// A hit is shaded from its instance's record (RayTracingScene: diffuse albedo, emission): the directional light with
// a shadow ray, the local lights unshadowed, and with multiple bounces (ddgiState.w) the volume's own irradiance.
// Normals come from the hit triangle's positions (VK_KHR_ray_tracing_position_fetch).
//
// visibilityMain traces the probe occlusion volume exactly: a segment is visible when no triangle crosses it.
[[vk::binding(1, 0)]] StructuredBuffer<uint> probeList : register(t1, space0);
[[vk::binding(2, 0)]] [format("rgba16f")] RWTexture2D<float4> rayOutput : register(u2, space0);
[[vk::binding(3, 0)]] Sampler2D probeStateTexture : register(t3, space0);
[[vk::binding(14, 0)]] Sampler2D irradianceAtlas : register(t14, space0);
[[vk::binding(15, 0)]] Sampler2D distanceAtlas : register(t15, space0);
[[vk::binding(16, 0)]] RWStructuredBuffer<uint> ddgiOcclusion : register(u16, space0);

#define PLUTO_DDGI_SAMPLING
#include "DDGICommon.slang"
#include "PhysicalSkyEnvironment.slang"
#include "../include/PlutoGE/render/RayTracingConfig.h"

// Mirrors RayTracedLighting (RayQueryDdgiRaySource.cpp).
[[vk::binding(4, 0)]] cbuffer RayTracedLighting : register(b4, space0)
{
    float4 sunDirectionIntensity;                        // xyz towards the light, w illuminance
    float4 sunColor;                                     // rgb colour, w 1 when shadowed by rays
    uint4 localLightCount;                               // x count
    float4 localPositionRange[PLUTO_RT_MAX_LOCAL_LIGHTS];   // xyz position, w range
    float4 localColorIntensity[PLUTO_RT_MAX_LOCAL_LIGHTS];  // rgb colour, w intensity (candela)
    float4 localDirectionType[PLUTO_RT_MAX_LOCAL_LIGHTS];   // xyz direction, w 0 point, 1 spot, 2 one-sided rectangle
    float4 localCone[PLUTO_RT_MAX_LOCAL_LIGHTS];            // spot inner, outer cone cosines
};
[[vk::binding(5, 0)]] RaytracingAccelerationStructure scene : register(t5, space0);

// Mirrors RayTracedInstance (RayTracingScene.h), indexed by the instance's custom index.
struct RayTracedInstance
{
    float4 albedo;   // rgb diffuse reflectance
    float4 emission; // rgb emitted radiance
};
[[vk::binding(6, 0)]] StructuredBuffer<RayTracedInstance> instances : register(t6, space0);

static const float RayOffset = 0.001;

float3 missRadiance(float3 origin, float3 direction)
{
    // The directional light owns the sun disc, exactly as the geometry pass's sky irradiance.
    return ddgiCloudySky(ddgiSky.x > 0.5 ? samplePhysicalSkyEnvironment(direction, 0.0) * physicalSkySettings.z
                                         : float3(ddgiSky.y), origin, direction);
}

// As the voxel source: only rays leaving above the horizon reach the sky. Below it the scene ends in ground it does
// not hold, so such rays report the distance travelled.
float escapeDistance(float3 direction, float travelled)
{
    return direction.y >= 0.0 ? PLUTO_DDGI_MISS_DISTANCE : travelled;
}

bool occluded(float3 origin, float3 direction, float distance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = distance;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// Direct light reaching a surface at `position` with `normal`, per unit of diffuse reflectance (times 1 / pi).
float3 directLight(float3 position, float3 normal)
{
    float3 light = 0.0;
    const float3 towardsSun = sunDirectionIntensity.xyz;
    const float sunCosine = dot(normal, towardsSun);
    if (sunDirectionIntensity.w > 0.0 && sunCosine > 0.0 &&
        (sunColor.w < 0.5 || !occluded(position + normal * RayOffset, towardsSun, PLUTO_RT_MAX_DISTANCE)))
        light += sunColor.rgb * sunDirectionIntensity.w * sunCosine;
    for (uint index = 0; index < min(localLightCount.x, uint(PLUTO_RT_MAX_LOCAL_LIGHTS)); ++index)
    {
        const float3 offset = localPositionRange[index].xyz - position;
        const float distanceSquared = dot(offset, offset);
        const float range = localPositionRange[index].w;
        if (distanceSquared >= range * range) continue;
        const float distance = sqrt(distanceSquared);
        const float3 l = offset / max(distance, 0.0001);
        const float cosine = dot(normal, l);
        if (cosine <= 0.0) continue;
        float attenuation = (1.0 - smoothstep(range * 0.9, range, distance)) / max(distanceSquared, 0.0001);
        const float type = localDirectionType[index].w;
        if (type > 1.5)
            // A rectangle far from the hit: a cosine emitter at its centre.
            attenuation *= saturate(dot(-l, localDirectionType[index].xyz));
        else if (type > 0.5)
            attenuation *= smoothstep(localCone[index].x, max(localCone[index].y, localCone[index].x + 0.000001),
                                      dot(-l, localDirectionType[index].xyz));
        light += localColorIntensity[index].rgb * localColorIntensity[index].w * attenuation * cosine;
    }
    return light / PI;
}

// `reach`: how far the caller reads hits; fixed rays need only ddgiFixedRayReach and no radiance.
float4 traceRay(float3 origin, float3 direction, float reach)
{
    const bool fixedRay = reach < PLUTO_RT_MAX_DISTANCE;
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = reach;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed()) {}
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return float4(fixedRay ? float3(0.0) : missRadiance(origin, direction), escapeDistance(direction, reach));
    const float distance = query.CommittedRayT();
    // A probe inside geometry first meets a back face: report how far it must travel to leave (negative).
    if (!query.CommittedTriangleFrontFace())
        return float4(0.0, 0.0, 0.0, -distance);
    if (fixedRay)
        return float4(0.0, 0.0, 0.0, distance);
    const float3 corners[3] = query.CommittedGetIntersectionTriangleVertexPositions();
    const float3 objectNormal = cross(corners[1] - corners[0], corners[2] - corners[0]);
    const float3 worldNormal = mul(objectNormal, (float3x3)query.CommittedWorldToObject3x4());
    // A zero-area triangle has no normal; normalising it would write NaN into the probe's history for good.
    const float normalLength = length(worldNormal);
    float3 normal = normalLength > 1.0e-12 ? worldNormal / normalLength : -direction;
    if (dot(normal, direction) > 0.0) normal = -normal;
    const RayTracedInstance instance = instances[query.CommittedInstanceID()];
    const float3 position = origin + direction * distance;
    float3 radiance = instance.emission.rgb + instance.albedo.rgb * directLight(position, normal);
    if (ddgiState.w > 0.0)
    {
        float coverage;
        const float3 irradiance = ddgiIrradiance(position, normal, -direction, coverage);
        radiance += ddgiState.w * instance.albedo.rgb * irradiance * coverage;
    }
    // Bright, small sources make single rays dominate a probe update: limit the luminance a ray gathers from the
    // scene (the sky is never clamped), as the voxel source does.
    const float luminance = dot(radiance, float3(0.2126, 0.7152, 0.0722));
    if (luminance > ddgiBlend.y)
        radiance *= ddgiBlend.y / luminance;
    return float4(radiance, distance);
}

// The probe occlusion volume (DdgiVisibilityInputs): one group per (cell, listed probe), one thread per sub-cell.
[shader("compute")]
[numthreads(PLUTO_DDGI_VISIBILITY_GROUP, 1, 1)]
void visibilityMain(uint3 group : SV_GroupID, uint threadIndex : SV_GroupIndex)
{
    const uint slot = group.y;
    if (slot >= ddgiRays.z)
        return;
    const uint storage = probeList[slot];
    const float4 state = probeStateTexture.Load(int3(ddgiTile(storage), 0));
    if (ddgiHasFlag(state, PLUTO_DDGI_PROBE_INACTIVE))
        return;
    const uint cascade = ddgiCascadeOf(storage);
    const int3 gridCoordinate = ddgiGridFromStorage(cascade, ddgiStorageFromIndex(storage));
    const uint corner = group.x;
    const int3 base = gridCoordinate - int3(corner & 1u, (corner >> 1) & 1u, corner >> 2);
    if (any(base < 0) || any(base > ddgiCounts.xyz - 2))
        return;
    const int subdivision = PLUTO_DDGI_OCCLUSION_SUBDIVISION;
    const int3 subcell = int3(int(threadIndex) % subdivision, (int(threadIndex) / subdivision) % subdivision,
                              int(threadIndex) / (subdivision * subdivision));
    const float3 centre = ddgiCascadeOrigins[cascade].xyz +
                          (float3(base) + (float3(subcell) + 0.5) / float(subdivision)) * ddgiSpacing(cascade);
    const float3 probe = ddgiProbePosition(cascade, gridCoordinate, state);
    const float3 delta = probe - centre;
    const float segment = length(delta);
    const bool visible = segment < 0.0001 || !occluded(centre, delta / segment, segment);
    const uint index = ddgiOcclusionIndex(cascade, base, subcell);
    const uint shift = ddgiOcclusionShift(corner);
    InterlockedAnd(ddgiOcclusion[index], ~(15u << shift));
    InterlockedOr(ddgiOcclusion[index], (visible ? 15u : 0u) << shift);
}

[shader("compute")]
[numthreads(PLUTO_DDGI_TRACE_GROUP, 1, 1)]
void traceMain(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID)
{
    const uint ray = group.x * PLUTO_DDGI_TRACE_GROUP + local.x;
    const uint slot = group.y;
    if (slot >= ddgiRays.z || ray >= ddgiRays.w)
        return;
    const uint storage = probeList[slot];
    const float4 state = probeStateTexture.Load(int3(ddgiTile(storage), 0));
    if (ddgiHasFlag(state, PLUTO_DDGI_PROBE_INACTIVE) && ray >= ddgiRays.x)
        return;
    const float reach = ray < ddgiRays.x ? ddgiFixedRayReach(ddgiCascadeOf(storage)) : PLUTO_RT_MAX_DISTANCE;
    rayOutput[int2(ray, slot)] = traceRay(ddgiProbePositionOf(storage, state), ddgiRayDirection(ray), reach);
}
