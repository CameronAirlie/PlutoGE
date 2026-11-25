// DDGI ray source backed by the voxel scene (VoxelDdgiRaySource): marches each probe ray through the published VCT
// radiance cascades as a near-ray cone (voxelTrace.x) and lets the transmission that escapes see the sky.
//
// One thread per ray: dispatch (ceil(traced rays / group), probes listed). Output texel (ray, list slot):
//   rgb  radiance arriving along the ray
//   a    distance to the first solid surface (foliage does not count: see traceRay), PLUTO_DDGI_MISS_DISTANCE for a miss,
//        or minus the distance needed to leave geometry when the probe starts inside it (a backface hit). A fixed ray's
//        backface hit carries the open space beyond that exit in r (rays inside geometry are not blended).
// Inactive probes trace only the fixed rays, which classification and relocation read.
//
// visibilityMain traces the probe occlusion volume (DdgiVisibilityInputs): whether each listed probe can see the
// sub-cells of the cells around it, marching the same voxels.
//
// With multiple bounces (ddgiState.w), a hit also reflects the light the volume itself held there at its last update:
// the voxel's representative albedo times the probes' irradiance. Repeated over updates this carries sky light and
// every further bounce through the scene, which the voxel field (direct light and one bounce of it) does not hold.
[[vk::binding(1, 0)]] StructuredBuffer<uint> probeList : register(t1, space0);
[[vk::binding(2, 0)]] [format("rgba16f")] RWTexture2D<float4> rayOutput : register(u2, space0);
[[vk::binding(3, 0)]] Sampler2D probeStateTexture : register(t3, space0);
[[vk::binding(14, 0)]] Sampler2D irradianceAtlas : register(t14, space0);
[[vk::binding(15, 0)]] Sampler2D distanceAtlas : register(t15, space0);
// The probe occlusion volume: read by the multiple-bounce lookups, written by visibilityMain.
[[vk::binding(16, 0)]] RWStructuredBuffer<uint> ddgiOcclusion : register(u16, space0);

#define PLUTO_DDGI_SAMPLING
#include "DDGICommon.slang"
#include "PhysicalSkyEnvironment.slang"
#include "VCTVoxelOpacity.slang"
// The voxel scene (bindings 4 and 8-13), its sampling and segmentVisibility.
#include "VoxelSceneSampling.slang"

// Each cascade's representative surface per voxel (VCTVoxelize.slang): 5-bit normal and albedo, validity in bit 31.
// OpenGL guarantees eight image units, so these take 5-7 and the atlases follow.
[[vk::binding(5, 0)]] [format("r32ui")] RWTexture3D<uint> surfaceRecord0 : register(u5, space0);
[[vk::binding(6, 0)]] [format("r32ui")] RWTexture3D<uint> surfaceRecord1 : register(u6, space0);
[[vk::binding(7, 0)]] [format("r32ui")] RWTexture3D<uint> surfaceRecord2 : register(u7, space0);

// The representative surface of the finest cascade's voxel at `position`: diffuse reflectance and normal.
bool voxelSurface(float3 position, out float3 reflectance, out float3 normal, out bool hitIsFoliage)
{
    reflectance = 0.0;
    normal = float3(0, 1, 0);
    hitIsFoliage = false;
    for (uint cascade = 0; cascade < min(voxelCounts.x, 3u); ++cascade)
    {
        float3 coordinate;
        if (!cascadeCoordinate(cascade, position, coordinate))
            continue;
        const int3 cell = min(int3(coordinate * float(voxelCounts.z)), int3(int(voxelCounts.z) - 1));
        const uint packed = cascade == 0 ? surfaceRecord0[cell] : cascade == 1 ? surfaceRecord1[cell] : surfaceRecord2[cell];
        if ((packed & 0x80000000u) == 0u)
            return false;
        normal = normalize(float3(packed & 31u, (packed >> 5) & 31u, (packed >> 10) & 31u) / 31.0 * 2.0 - 1.0);
        reflectance = float3((packed >> 15) & 31u, (packed >> 20) & 31u, (packed >> 25) & 31u) / 31.0;
        hitIsFoliage = vctRecordIsFoliage(packed);
        return true;
    }
    return false;
}

// Light the hit surface reflects from the volume's own irradiance (multiple bounces).
float3 reflectedProbeLight(float3 hitPoint, float3 direction, float voxelSize)
{
    float3 reflectance, normal;
    bool hitIsFoliage;
    // The crossing lies on the surface's near side; its voxel is just beyond.
    if (!voxelSurface(hitPoint + direction * (voxelSize * 0.5), reflectance, normal, hitIsFoliage) &&
        !voxelSurface(hitPoint + direction * voxelSize, reflectance, normal, hitIsFoliage))
        return 0.0;
    // One normal stands for both sides of a thin surface: use the side the ray arrives at.
    if (dot(normal, direction) > 0.0)
        normal = -normal;
    float coverage;
    float3 irradiance = ddgiIrradiance(hitPoint, normal, -direction, coverage);
    irradiance *= coverage;
    // Foliage passes on half of what either side receives (VCTVoxelize.slang's record holds both sides' reflectance).
    if (hitIsFoliage)
    {
        float farCoverage;
        const float3 farIrradiance = ddgiIrradiance(hitPoint + direction * voxelSize, -normal, direction, farCoverage);
        irradiance = (irradiance + farIrradiance * farCoverage) * 0.5;
    }
    return reflectance * irradiance;
}

float3 missRadiance(float3 origin, float3 direction)
{
    // The directional light owns the sun disc, exactly as the geometry pass's sky irradiance.
    return ddgiCloudySky(ddgiSky.x > 0.5 ? samplePhysicalSkyEnvironment(direction, 0.0) * physicalSkySettings.z
                                         : float3(ddgiSky.y), origin, direction);
}

// The hit distance of a ray that left the voxel scene without reaching a surface. Only rays leaving above the horizon
// reach the sky (PLUTO_DDGI_MISS_DISTANCE). Below it the world ends in ground the voxel scene does not hold: rays that
// slipped through a floor thinner than a voxel with nothing beneath it, or left past the cascades' lower faces, report
// the distance travelled instead, so probes under floors do not count sky that downward-facing surfaces near them
// would then reflect. Their radiance still includes the sky term below the horizon.
float escapeDistance(float3 direction, float travelled)
{
    return direction.y >= 0.0 ? PLUTO_DDGI_MISS_DISTANCE : travelled;
}

// `reach`: how far the caller reads hits (fixed rays need only ddgiFixedRayReach). The march stops after the first
// sample beyond it, so a hit straddling it is placed as by a full march; radiance is only gathered for full rays.
float4 traceRay(float3 origin, float3 direction, float reach)
{
    const bool fixedRay = reach < voxelTrace.y;
    // Opacity below which a region counts as empty for skipping: a voxel this faint at four footprints would add at
    // most 0.06% opacity at the ordinary one. The finest voxel bounds the coarsest cell a sample can cover.
    const float EmptyOpacity = 0.00001;
    const float finestVoxel = voxelCascades[0].w / float(max(voxelCounts.z, 1u));
    float covered, voxelSize;
    const float4 start = sampleScene(origin, direction, 0.0, covered, voxelSize);
    if (covered <= 0.0)
        return float4(fixedRay ? float3(0.0) : missRadiance(origin, direction), escapeDistance(direction, voxelTrace.y));
    if (start.a > 0.5)
    {
        // Inside geometry: report how far this direction must travel to leave it.
        float exitDistance = voxelSize;
        bool left = false;
        for (uint step = 0; step < 16; ++step)
        {
            float exitCovered, exitVoxel;
            const float exitOpacity = sampleScene(origin + direction * exitDistance, direction, 0.0, exitCovered, exitVoxel).a;
            if (exitOpacity <= 0.5 || exitCovered <= 0.0)
            {
                left = true;
                break;
            }
            exitDistance += exitVoxel;
        }
        // Fixed rays also report the open space beyond the exit (r), up to their reach: relocation leaves by the side
        // with room for the probe, not by whichever way out its fixed rays list first (DDGIProbeUpdate.slang).
        float open = 0.0;
        if (fixedRay && left)
        {
            float beyond = exitDistance;
            for (uint step = 0; step < 48 && beyond - exitDistance < reach; ++step)
            {
                float openCovered, openVoxel;
                const float openOpacity = sampleScene(origin + direction * beyond, direction, 0.0, openCovered, openVoxel).a;
                if (openCovered <= 0.0)
                {
                    beyond = exitDistance + reach;
                    break;
                }
                if (openOpacity > 0.5)
                    break;
                beyond += openVoxel;
            }
            open = min(beyond - exitDistance, reach);
        }
        return float4(open, 0.0, 0.0, -exitDistance);
    }
    float distance = voxelSize, previousDistance = 0.0;
    float transmission = 1.0;
    // Foliage voxels (VCTVoxelOpacity.slang, below one half at the finest footprint) dim the ray's radiance but are not
    // surfaces to probes: counted as hits, they would pull a probe's visibility in to the nearest leaves and move it out
    // of the canopy, so everything below the canopy lost the probes above it. Reported distances follow solid surfaces.
    float solidTransmission = 1.0;
    float solidDistance = -1.0;
    float hitDistance = -1.0, hitVoxel = voxelSize;
    float3 radiance = 0.0;
    for (uint step = 0; step < 96 && solidTransmission > 0.02 && distance < voxelTrace.y && previousDistance < reach; ++step)
    {
        const float diameter = max(voxelSize, distance * voxelTrace.x);
        float sampleCovered, sampleVoxel;
        float4 sample = sampleScene(origin + direction * distance, direction, diameter, sampleCovered, sampleVoxel);
        if (sampleCovered <= 0.0)
            break;
        // A wall thinner than a voxel fills one layer, and steps of a voxel sample it filtered between two positions:
        // one reads at least half opaque, but both together still pass a quarter of the light behind it (sky into
        // rooms, strongest along edges and corners). Doubling opacity while the footprint is a single voxel makes
        // that sample opaque; wider footprints are filtered mips whose partial coverage is real.
        // Below one half nothing is doubled: that is foliage's range, and a wall always has a sample above it.
        const float narrow = saturate(2.0 - diameter / sampleVoxel);
        const float rawOpacity = sample.a;
        const float solidOpacity = lerp(rawOpacity, rawOpacity >= 0.5 ? 1.0 : rawOpacity, narrow);
        sample.rgb *= sample.a > 0.0001 ? solidOpacity / sample.a : 1.0;
        sample.a = solidOpacity;
        radiance += transmission * sample.rgb;
        const float next = transmission * (1.0 - saturate(sample.a));
        // Wider footprints are mips that cannot tell foliage from part of a wall, and count as they read.
        const float solidNext = solidTransmission * (1.0 - lerp(rawOpacity, rawOpacity >= 0.5 ? 1.0 : 0.0, narrow));
        if (solidDistance < 0.0 && solidNext < 0.5)
            solidDistance = lerp(previousDistance, distance,
                                 saturate((solidTransmission - 0.5) / max(solidTransmission - solidNext, 0.0001)));
        solidTransmission = solidNext;
        // Place the surface where transmission crosses one half between the two samples, not at the step: visibility
        // compares these distances with sub-voxel offsets.
        if (hitDistance < 0.0 && next < 0.5)
        {
            hitDistance = lerp(previousDistance, distance, saturate((transmission - 0.5) / max(transmission - next, 0.0001)));
            hitVoxel = sampleVoxel;
        }
        transmission = next;
        voxelSize = sampleVoxel;
        previousDistance = distance;
        const float advance = max(sampleVoxel, diameter * 0.5);
        distance += advance;
        // Empty-space skipping. Escaping rays otherwise spend most of their steps crossing empty outer cascades. Where
        // this sample is empty and so is the volume four footprints coarse around the next stretch (its trilinear
        // sample at that size covers the whole stretch), leap over it: empty voxels add no radiance or opacity, so
        // only where later samples fall changes. The sample before the landing point is taken as one ordinary step
        // back, where transmission is still what it is now, so hits beyond the leap are placed as before.
        if (sample.a <= EmptyOpacity)
        {
            const float leap = min(max(sampleVoxel, diameter) * 4.0, finestVoxel * exp2(float(voxelCounts.w)));
            if (leap > advance * 2.0)
            {
                float leapCovered, leapVoxel;
                const float4 coarse = sampleScene(origin + direction * (previousDistance + leap * 0.5), direction, leap,
                                                  leapCovered, leapVoxel);
                if (leapCovered >= 1.0 && coarse.a <= EmptyOpacity)
                {
                    distance = previousDistance + leap;
                    previousDistance = distance - advance;
                }
            }
        }
    }
    if (fixedRay)
        return float4(0.0, 0.0, 0.0, solidDistance >= 0.0 ? solidDistance : escapeDistance(direction, distance));
    if (ddgiState.w > 0.0 && hitDistance >= 0.0)
        radiance += (1.0 - transmission) * ddgiState.w * reflectedProbeLight(origin + direction * hitDistance, direction, hitVoxel);
    // Bright, small sources make single rays dominate a probe update: limit the luminance a ray gathers from the
    // scene. The sky is smooth and is never clamped, so open areas match the geometry pass's sky lighting.
    const float luminance = dot(radiance, float3(0.2126, 0.7152, 0.0722));
    if (luminance > ddgiBlend.y)
        radiance *= ddgiBlend.y / luminance;
    radiance += transmission * missRadiance(origin, direction);
    // Foliage hid the sky from this ray though nothing solid stopped it: far away, but not sky to reflections.
    const float reported = solidDistance >= 0.0 ? solidDistance
        : hitDistance >= 0.0 ? distance : escapeDistance(direction, distance);
    return float4(radiance, reported);
}

// One group per (cell, listed probe), one thread per sub-cell of the cell (DdgiVisibilityInputs).
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
    // The probe is corner `corner` of the cell whose base lies that far below it.
    const uint corner = group.x;
    const int3 base = gridCoordinate - int3(corner & 1u, (corner >> 1) & 1u, corner >> 2);
    if (any(base < 0) || any(base > ddgiCounts.xyz - 2))
        return;
    const int subdivision = PLUTO_DDGI_OCCLUSION_SUBDIVISION;
    const int3 subcell = int3(int(threadIndex) % subdivision, (int(threadIndex) / subdivision) % subdivision,
                              int(threadIndex) / (subdivision * subdivision));
    const float3 centre = ddgiCascadeOrigins[cascade].xyz +
                          (float3(base) + (float3(subcell) + 0.5) / float(subdivision)) * ddgiSpacing(cascade);
    const float visibility = segmentVisibility(centre, ddgiProbePosition(cascade, gridCoordinate, state));
    // Other probes write the other nibbles of the same sub-cell concurrently.
    const uint index = ddgiOcclusionIndex(cascade, base, subcell);
    const uint shift = ddgiOcclusionShift(corner);
    InterlockedAnd(ddgiOcclusion[index], ~(15u << shift));
    InterlockedOr(ddgiOcclusion[index], uint(round(visibility * 15.0)) << shift);
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
    const float reach = ray < ddgiRays.x ? ddgiFixedRayReach(ddgiCascadeOf(storage)) : voxelTrace.y;
    rayOutput[int2(ray, slot)] = traceRay(ddgiProbePositionOf(storage, state), ddgiRayDirection(ray), reach);
}
