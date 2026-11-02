// DDGI probe update: everything after the ray source has traced this frame's listed probes (DdgiVolume::Update).
//
//   resetMain            marks probes without history: every probe on a full reset, or those that entered the volume
//                        by scrolling (listed after this frame's traced probes). Their offset returns to zero.
//   stateMain            per listed probe, from its fixed rays: relocation away from nearby geometry (or out of it) and
//                        classification (inactive when inside geometry or with no surface close enough to sample it).
//   blendIrradianceMain  one group per listed probe: cosine-weighted mean radiance per octahedral texel, blended into
//                        history more slowly while the change is within the rays' own noise. Border texels compute
//                        the interior texel they mirror, so bilinear lookups stay within the tile without a second pass.
//   blendDistanceMain    the same for the visibility moments (mean and mean square hit distance) and the share of rays
//                        that reached the sky; it also clears the probe's fresh flag once both atlases hold data and
//                        counts the updates blended since (PLUTO_DDGI_HISTORY_SHIFT).
//
// Probes skip both blends while inactive, and for the update that reactivates them (PLUTO_DDGI_PROBE_PARTIAL_RAYS).
#include "DDGICommon.slang"

[[vk::binding(1, 0)]] StructuredBuffer<uint> probeList : register(t1, space0);
[[vk::binding(2, 0)]] [format("rgba16f")] RWTexture2D<float4> probeState : register(u2, space0);
[[vk::binding(3, 0)]] Sampler2D rayTexture : register(t3, space0);
[[vk::binding(5, 0)]] [format("rgba16f")] RWTexture2D<float4> irradianceAtlas : register(u5, space0);
[[vk::binding(6, 0)]] [format("rgba16f")] RWTexture2D<float4> distanceAtlas : register(u6, space0);

// ddgiApply.w: probes to reset after the listed ones; a negative value resets every probe.
[shader("compute")]
[numthreads(PLUTO_DDGI_STATE_GROUP, 1, 1)]
void resetMain(uint3 id : SV_DispatchThreadID)
{
    uint storage;
    if (ddgiApply.w < 0.0)
    {
        if (id.x >= ddgiProbeCount()) return;
        storage = id.x;
    }
    else
    {
        if (id.x >= uint(ddgiApply.w)) return;
        storage = probeList[ddgiRays.z + id.x];
    }
    probeState[ddgiTile(storage)] = float4(0.0, 0.0, 0.0, float(PLUTO_DDGI_PROBE_FRESH));
}

[shader("compute")]
[numthreads(PLUTO_DDGI_STATE_GROUP, 1, 1)]
void stateMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= ddgiRays.z) return;
    const uint probe = probeList[id.x];
    const int2 tile = ddgiTile(probe);
    const float4 state = probeState[tile];
    const uint flags = ddgiFlags(state);
    const float spacing = ddgiSpacing(ddgiCascadeOf(probe));
    uint backfaces = 0;
    float closestBackface = 1e30, closestFrontface = 1e30;
    float3 backfaceDirection = 0.0, frontfaceDirection = 0.0;
    for (uint ray = 0; ray < ddgiRays.x; ++ray)
    {
        const float distance = rayTexture.Load(int3(int(ray), int(id.x), 0)).a;
        const float3 direction = ddgiRayDirection(ray);
        if (distance < 0.0)
        {
            ++backfaces;
            if (-distance < closestBackface) { closestBackface = -distance; backfaceDirection = direction; }
        }
        else if (distance < closestFrontface)
        {
            closestFrontface = distance;
            frontfaceDirection = direction;
        }
    }
    const bool inside = float(backfaces) > 0.25 * float(ddgiRays.x);

    float3 offset = 0.0;
    uint moves = (flags >> PLUTO_DDGI_RELOCATION_SHIFT) & PLUTO_DDGI_MAX_RELOCATIONS;
    if (ddgiState.x > 0.5)
    {
        // Leave geometry by the shortest way out, or keep a minimum distance from the nearest surface. A probe whose way
        // out is within the ray source's surface tolerance (ddgiBias.z) sits in a surface thinner than that, such as
        // a roof thinner than a voxel: either side is as near, and probes moved out through it lit the join of the
        // surfaces beneath with noisy light. It stays inside, and inactive, as nothing can separate its sides, unless
        // the volume relocates from thin surfaces too (ddgiState.x 2): a probe layer in a floor otherwise leaves the
        // floor without its nearest probes.
        const float minimumFrontface = ddgiState.z * spacing;
        const float thinSurface = ddgiState.x > 1.5 ? 0.0 : ddgiBias.z;
        offset = state.xyz;
        float3 move = 0.0;
        if (inside)
        {
            if (closestBackface > thinSurface)
                move = backfaceDirection * (closestBackface + minimumFrontface * 0.5) / spacing;
        }
        else if (closestFrontface < minimumFrontface)
            move = -frontfaceDirection * (minimumFrontface - closestFrontface) / spacing;
        // A probe makes at most PLUTO_DDGI_MAX_RELOCATIONS moves until it is reset, then stays where it is. Settling
        // can take a few moves that reverse each other (out through the far side of a thin floor, then back up through
        // it), but near some thin surfaces the moves never settled: the probe moved and snapped back at every update.
        if (any(move != 0.0) && moves < PLUTO_DDGI_MAX_RELOCATIONS)
        {
            offset = clamp(offset + move, -0.45, 0.45);
            ++moves;
        }
    }

    bool inactive = false;
    if (ddgiState.y > 0.5)
    {
        // A surface only samples the probes at the corners of its cell: with none within reach (or the probe still
        // inside geometry) the probe cannot contribute, so it traces just its fixed rays until that changes.
        inactive = inside || closestFrontface > spacing * PLUTO_DDGI_CLASSIFICATION_REACH;
    }
    uint next = (flags & PLUTO_DDGI_PROBE_FRESH) | (inactive ? PLUTO_DDGI_PROBE_INACTIVE : 0u) |
                (moves << PLUTO_DDGI_RELOCATION_SHIFT);
    next |= flags & (PLUTO_DDGI_MAX_HISTORY << PLUTO_DDGI_HISTORY_SHIFT);
    // The trace saw this probe inactive and traced only its fixed rays.
    if ((flags & PLUTO_DDGI_PROBE_INACTIVE) != 0)
        next |= PLUTO_DDGI_PROBE_PARTIAL_RAYS;
    // A reactivated probe's history stopped updating while it was inactive.
    if ((flags & PLUTO_DDGI_PROBE_INACTIVE) != 0 && !inactive)
        next |= PLUTO_DDGI_PROBE_FRESH;
    probeState[tile] = float4(offset, float(next));
}

// This group's rays: radiance (rgb), hit distance (a) and direction.
groupshared float4 sharedRays[PLUTO_DDGI_MAX_RAYS];
groupshared float3 sharedDirections[PLUTO_DDGI_MAX_RAYS];

void loadRays(uint slot, uint threadIndex, uint threadCount)
{
    for (uint ray = threadIndex; ray < ddgiRays.w; ray += threadCount)
    {
        sharedRays[ray] = rayTexture.Load(int3(int(ray), int(slot), 0));
        sharedDirections[ray] = ddgiRayDirection(ray);
    }
    GroupMemoryBarrierWithGroupSync();
}

// The tile texel whose value border texel `texel` mirrors (octahedral edge wrap); interior texels map to themselves.
int2 borderSource(int2 texel, int tileSize)
{
    const int last = tileSize - 1;
    const bool row = texel.y == 0 || texel.y == last;
    const bool column = texel.x == 0 || texel.x == last;
    if (row && column)
        return int2(texel.x > 0 ? 1 : last - 1, texel.y > 0 ? 1 : last - 1);
    if (row)
        return int2(last - texel.x, texel.y > 0 ? last - 1 : 1);
    if (column)
        return int2(texel.x > 0 ? last - 1 : 1, last - texel.y);
    return texel;
}

[shader("compute")]
[numthreads(PLUTO_DDGI_IRRADIANCE_TILE, PLUTO_DDGI_IRRADIANCE_TILE, 1)]
void blendIrradianceMain(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID, uint threadIndex : SV_GroupIndex)
{
    const uint slot = group.x;
    if (slot >= ddgiRays.z) return;
    const int2 tile = ddgiTile(probeList[slot]);
    const float4 state = probeState[tile];
    if (ddgiHasFlag(state, PLUTO_DDGI_PROBE_INACTIVE | PLUTO_DDGI_PROBE_PARTIAL_RAYS)) return;
    loadRays(slot, threadIndex, PLUTO_DDGI_IRRADIANCE_TILE * PLUTO_DDGI_IRRADIANCE_TILE);

    const int2 source = borderSource(int2(local.xy), PLUTO_DDGI_IRRADIANCE_TILE);
    const float3 direction = ddgiTexelDirection(source - 1, PLUTO_DDGI_IRRADIANCE_INTERIOR);
    float3 radiance = 0.0;
    // Weighted sums of the rays' luminance squared and of the squared weights, for the estimate's standard error.
    float luminanceSquares = 0.0, weightSquares = 0.0;
    float weightSum = 0.0;
    for (uint ray = ddgiRays.x; ray < ddgiRays.w; ++ray)
    {
        const float4 sample = sharedRays[ray];
        // Rays starting inside geometry see no light; they would only darken the probe.
        if (sample.a < 0.0) continue;
        const float weight = max(dot(direction, sharedDirections[ray]), 0.0);
        const float luminance = dot(sample.rgb, float3(0.2126, 0.7152, 0.0722));
        radiance += sample.rgb * weight;
        luminanceSquares += luminance * luminance * weight;
        weightSquares += weight * weight;
        weightSum += weight;
    }
    const int2 texel = tile * PLUTO_DDGI_IRRADIANCE_TILE + int2(local.xy);
    // A fresh probe's texels hold no data (possibly not even numbers): replace them without reading.
    if (ddgiHasFlag(state, PLUTO_DDGI_PROBE_FRESH))
    {
        irradianceAtlas[texel] = float4(weightSum > 0.0001 ? radiance / weightSum : 0.0, 1.0);
        return;
    }
    if (weightSum <= 0.0001) return;
    const float3 current = radiance / weightSum;
    const float3 previous = irradianceAtlas[texel].rgb;
    const float luminanceCurrent = dot(current, float3(0.2126, 0.7152, 0.0722));
    const float luminancePrevious = dot(previous, float3(0.2126, 0.7152, 0.0722));
    const float luminanceChange = abs(luminanceCurrent - luminancePrevious);
    // Each update's rays are a fresh random sample, so its estimate differs from the last by noise even in a static
    // scene. The rays measure that noise themselves: the standard error of their weighted mean luminance is the
    // spread of their luminance over the square root of their effective count. A change within it is most likely
    // noise and keeps more history (ddgiTemporal.x); one well beyond it is a real change and converges at the volume's
    // hysteresis, and one that doubles or halves the brightness (a light switched on or off) at half of that.
    const float spread = max(luminanceSquares / weightSum - luminanceCurrent * luminanceCurrent, 0.0);
    const float effectiveRays = weightSum * weightSum / max(weightSquares, 1e-8);
    const float standardError = sqrt(spread / max(effectiveRays, 1.0));
    const float noise = 1.0 - smoothstep(ddgiTemporal.y, ddgiTemporal.z, luminanceChange / max(standardError, 1e-6));
    float hysteresis = lerp(ddgiBlend.x, ddgiTemporal.x, noise);
    if (luminanceChange > max(luminancePrevious, 0.1))
        hysteresis = ddgiBlend.x * 0.5;
    hysteresis = min(hysteresis, ddgiHistoryLimit(state));
    irradianceAtlas[texel] = float4(lerp(current, previous, hysteresis), 1.0);
}

[shader("compute")]
[numthreads(PLUTO_DDGI_DISTANCE_TILE, PLUTO_DDGI_DISTANCE_TILE, 1)]
void blendDistanceMain(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID, uint threadIndex : SV_GroupIndex)
{
    const uint slot = group.x;
    if (slot >= ddgiRays.z) return;
    const uint probe = probeList[slot];
    const int2 tile = ddgiTile(probe);
    const float4 state = probeState[tile];
    if (ddgiHasFlag(state, PLUTO_DDGI_PROBE_INACTIVE | PLUTO_DDGI_PROBE_PARTIAL_RAYS)) return;
    loadRays(slot, threadIndex, PLUTO_DDGI_DISTANCE_TILE * PLUTO_DDGI_DISTANCE_TILE);
    const float range = ddgiBlend.w * ddgiSpacing(ddgiCascadeOf(probe));

    const int2 source = borderSource(int2(local.xy), PLUTO_DDGI_DISTANCE_TILE);
    const float3 direction = ddgiTexelDirection(source - 1, PLUTO_DDGI_DISTANCE_INTERIOR);
    float2 moments = 0.0;
    float weightSum = 0.0;
    // Sky visibility for reflection occlusion (DDGIReflection.slang) uses a wider lobe than the moments: a texel's
    // sharp lobe holds about one ray per update, too few to estimate a share from.
    float sky = 0.0, skyWeightSum = 0.0;
    for (uint ray = ddgiRays.x; ray < ddgiRays.w; ++ray)
    {
        const float cosine = max(dot(direction, sharedDirections[ray]), 0.0);
        const float weight = pow(cosine, ddgiBlend.z);
        const float distance = min(abs(sharedRays[ray].a), range);
        moments += float2(distance, distance * distance) * weight;
        weightSum += weight;
        const float skyWeight = pow(cosine, ddgiReflection.z);
        // Rays starting inside geometry see no sky.
        sky += ddgiIsMiss(sharedRays[ray].a) ? skyWeight : 0.0;
        skyWeightSum += skyWeight;
    }
    const int2 texel = tile * PLUTO_DDGI_DISTANCE_TILE + int2(local.xy);
    const float2 currentMoments = weightSum > 1e-12 ? moments / weightSum : float2(range, range * range);
    // Without rays in the lobe nothing is known to occlude the sky.
    const float3 current = float3(currentMoments, skyWeightSum > 1e-12 ? sky / skyWeightSum : 1.0);
    distanceAtlas[texel] = float4(ddgiHasFlag(state, PLUTO_DDGI_PROBE_FRESH)
        ? current : lerp(current, distanceAtlas[texel].rgb, min(ddgiBlend.x, ddgiHistoryLimit(state))), 1.0);
    // Both atlases now hold this probe's data (the irradiance blend ran before this dispatch). Every thread read the
    // state before loadRays' barrier, so updating the flags here cannot race with those reads.
    if (threadIndex == 0)
    {
        const uint history = ddgiHasFlag(state, PLUTO_DDGI_PROBE_FRESH)
            ? 1u : min(ddgiHistory(state) + 1u, uint(PLUTO_DDGI_MAX_HISTORY));
        const uint kept = ddgiFlags(state) & ~uint(PLUTO_DDGI_PROBE_FRESH | (PLUTO_DDGI_MAX_HISTORY << PLUTO_DDGI_HISTORY_SHIFT));
        probeState[tile] = float4(state.xyz, float(kept | (history << PLUTO_DDGI_HISTORY_SHIFT)));
    }
}
