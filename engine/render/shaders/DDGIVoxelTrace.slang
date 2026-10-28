// DDGI ray source backed by the voxel scene (VoxelDdgiRaySource): marches each probe ray through the published VCT
// radiance cascades as a near-ray cone (voxelTrace.x) and lets the transmission that escapes see the sky.
//
// One thread per ray: dispatch (ceil(traced rays / group), probes listed). Output texel (ray, list slot):
//   rgb  radiance arriving along the ray
//   a    distance to the first surface (accumulated opacity reaches one half), PLUTO_DDGI_MISS_DISTANCE for a miss,
//        or minus the distance needed to leave geometry when the probe starts inside it (a backface hit).
// Inactive probes trace only the fixed rays, which classification and relocation read.
//
// With multiple bounces (ddgiState.w), a hit also reflects the light the volume itself held there at its last update:
// the voxel's representative albedo times the probes' irradiance. Repeated over updates this carries sky light and
// every further bounce through the scene, which the voxel field (direct light and one bounce of it) does not hold.
[[vk::binding(1, 0)]] StructuredBuffer<uint> probeList : register(t1, space0);
[[vk::binding(2, 0)]] [format("rgba16f")] RWTexture2D<float4> rayOutput : register(u2, space0);
[[vk::binding(3, 0)]] Sampler2D probeStateTexture : register(t3, space0);
[[vk::binding(14, 0)]] Sampler2D irradianceAtlas : register(t14, space0);
[[vk::binding(15, 0)]] Sampler2D distanceAtlas : register(t15, space0);

#define PLUTO_DDGI_SAMPLING
#include "DDGICommon.slang"
#include "PhysicalSkyEnvironment.slang"

// The voxel scene: up to three nested cascades sharing six directional radiance atlases (VCT's layout: premultiplied
// radiance in rgb, opacity in a, cascades stacked along z, directional mips).
[[vk::binding(4, 0)]] cbuffer VoxelSceneParameters : register(b4, space0)
{
    float4 voxelCascades[3]; // xyz origin, w size
    uint4 voxelCounts;       // x valid cascades, y cascades in the atlas, z resolution, w maximum mip
    float4 voxelTrace;       // x cone spread (footprint diameter per unit distance), y maximum distance
};
// Each cascade's representative surface per voxel (VCTVoxelize.slang): 5-bit normal and albedo, validity in bit 31.
// OpenGL guarantees eight image units, so these take 5-7 and the atlases follow.
[[vk::binding(5, 0)]] [format("r32ui")] RWTexture3D<uint> surfaceRecord0 : register(u5, space0);
[[vk::binding(6, 0)]] [format("r32ui")] RWTexture3D<uint> surfaceRecord1 : register(u6, space0);
[[vk::binding(7, 0)]] [format("r32ui")] RWTexture3D<uint> surfaceRecord2 : register(u7, space0);
[[vk::binding(8, 0)]] Sampler3D voxelPositiveX : register(t8, space0);
[[vk::binding(9, 0)]] Sampler3D voxelNegativeX : register(t9, space0);
[[vk::binding(10, 0)]] Sampler3D voxelPositiveY : register(t10, space0);
[[vk::binding(11, 0)]] Sampler3D voxelNegativeY : register(t11, space0);
[[vk::binding(12, 0)]] Sampler3D voxelPositiveZ : register(t12, space0);
[[vk::binding(13, 0)]] Sampler3D voxelNegativeZ : register(t13, space0);

bool cascadeCoordinate(uint cascade, float3 position, out float3 coordinate)
{
    coordinate = (position - voxelCascades[cascade].xyz) / voxelCascades[cascade].w;
    return all(coordinate >= 0.0) && all(coordinate < 1.0);
}

float4 directionalSample(uint cascade, float3 coordinate, float3 direction, float mip)
{
    const float size = float(max(voxelCounts.z >> uint(ceil(mip)), 1u));
    const float halfTexel = 0.5 / size;
    coordinate = clamp(coordinate, halfTexel, 1.0 - halfTexel);
    const float3 atlas = float3(coordinate.xy, (float(cascade) + coordinate.z) / float(max(voxelCounts.y, 1u)));
    float3 weight = abs(direction);
    weight /= max(weight.x + weight.y + weight.z, 0.0001);
    const float4 x = direction.x >= 0.0 ? voxelPositiveX.SampleLevel(atlas, mip) : voxelNegativeX.SampleLevel(atlas, mip);
    const float4 y = direction.y >= 0.0 ? voxelPositiveY.SampleLevel(atlas, mip) : voxelNegativeY.SampleLevel(atlas, mip);
    const float4 z = direction.z >= 0.0 ? voxelPositiveZ.SampleLevel(atlas, mip) : voxelNegativeZ.SampleLevel(atlas, mip);
    return x * weight.x + y * weight.y + z * weight.z;
}

// Fades a cascade out towards its faces, over a band that grows with the footprint (as VCTConeTrace).
float cascadeWeight(uint cascade, float3 coordinate, float diameter)
{
    const float size = voxelCascades[cascade].w;
    const float3 edge = min(coordinate, 1.0 - coordinate) * size;
    const float band = min(size * 0.5, max(size * 0.15, diameter));
    return smoothstep(0.0, band, min(min(edge.x, edge.y), edge.z));
}

// The voxel scene around `position` for a footprint of `diameter`, blending from the finest cascade outwards.
// `covered` is the weight of cascades that contain the point (zero outside the scene); `voxelSize` the blended
// size of their voxels.
float4 sampleScene(float3 position, float3 direction, float diameter, out float covered, out float voxelSize)
{
    const uint count = min(voxelCounts.x, 3u);
    const float resolution = float(voxelCounts.z);
    float4 result = 0.0;
    float remaining = 1.0;
    voxelSize = voxelCascades[max(count, 1u) - 1].w / resolution;
    float sizeSum = 0.0;
    for (uint cascade = 0; cascade < count && remaining > 0.0; ++cascade)
    {
        float3 coordinate;
        if (!cascadeCoordinate(cascade, position, coordinate))
            continue;
        const float localVoxel = voxelCascades[cascade].w / resolution;
        const float weight = remaining * cascadeWeight(cascade, coordinate, diameter);
        if (weight <= 0.0)
            continue;
        const float mip = clamp(log2(max(diameter, localVoxel) / localVoxel), 0.0, float(voxelCounts.w));
        result += directionalSample(cascade, coordinate, direction, mip) * weight;
        sizeSum += localVoxel * weight;
        remaining -= weight;
    }
    covered = 1.0 - remaining;
    if (covered > 0.0)
        voxelSize = sizeSum / covered;
    return result;
}

// The representative surface of the finest cascade's voxel at `position`: diffuse reflectance and normal.
bool voxelSurface(float3 position, out float3 reflectance, out float3 normal)
{
    reflectance = 0.0;
    normal = float3(0, 1, 0);
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
        return true;
    }
    return false;
}

// Light the hit surface reflects from the volume's own irradiance (multiple bounces).
float3 reflectedProbeLight(float3 hitPoint, float3 direction, float voxelSize)
{
    float3 reflectance, normal;
    // The crossing lies on the surface's near side; its voxel is just beyond.
    if (!voxelSurface(hitPoint + direction * (voxelSize * 0.5), reflectance, normal) &&
        !voxelSurface(hitPoint + direction * voxelSize, reflectance, normal))
        return 0.0;
    // One normal stands for both sides of a thin surface: use the side the ray arrives at.
    if (dot(normal, direction) > 0.0)
        normal = -normal;
    float coverage;
    const float3 irradiance = ddgiIrradiance(hitPoint, normal, -direction, coverage);
    return reflectance * irradiance * coverage;
}

float3 missRadiance(float3 direction)
{
    // The directional light owns the sun disc, exactly as the geometry pass's sky irradiance.
    return ddgiSky.x > 0.5 ? samplePhysicalSkyEnvironment(direction, 0.0) * physicalSkySettings.z
                           : float3(ddgiSky.y);
}

float4 traceRay(float3 origin, float3 direction)
{
    float covered, voxelSize;
    const float4 start = sampleScene(origin, direction, 0.0, covered, voxelSize);
    if (covered <= 0.0)
        return float4(missRadiance(direction), PLUTO_DDGI_MISS_DISTANCE);
    if (start.a > 0.5)
    {
        // Inside geometry: report how far this direction must travel to leave it.
        float exitDistance = voxelSize;
        for (uint step = 0; step < 16; ++step)
        {
            float exitCovered, exitVoxel;
            if (sampleScene(origin + direction * exitDistance, direction, 0.0, exitCovered, exitVoxel).a <= 0.5 ||
                exitCovered <= 0.0)
                break;
            exitDistance += exitVoxel;
        }
        return float4(0.0, 0.0, 0.0, -exitDistance);
    }
    float distance = voxelSize, previousDistance = 0.0;
    float transmission = 1.0;
    float hitDistance = -1.0, hitVoxel = voxelSize;
    float3 radiance = 0.0;
    for (uint step = 0; step < 96 && transmission > 0.02 && distance < voxelTrace.y; ++step)
    {
        const float diameter = max(voxelSize, distance * voxelTrace.x);
        float sampleCovered, sampleVoxel;
        const float4 sample = sampleScene(origin + direction * distance, direction, diameter, sampleCovered, sampleVoxel);
        if (sampleCovered <= 0.0)
            break;
        radiance += transmission * sample.rgb;
        const float next = transmission * (1.0 - saturate(sample.a));
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
        distance += max(sampleVoxel, diameter * 0.5);
    }
    if (ddgiState.w > 0.0 && hitDistance >= 0.0)
        radiance += (1.0 - transmission) * ddgiState.w * reflectedProbeLight(origin + direction * hitDistance, direction, hitVoxel);
    // Bright, small sources make single rays dominate a probe update: limit the luminance a ray gathers from the
    // scene. The sky is smooth and is never clamped, so open areas match the geometry pass's sky lighting.
    const float luminance = dot(radiance, float3(0.2126, 0.7152, 0.0722));
    if (luminance > ddgiBlend.y)
        radiance *= ddgiBlend.y / luminance;
    radiance += transmission * missRadiance(direction);
    return float4(radiance, hitDistance >= 0.0 ? hitDistance : PLUTO_DDGI_MISS_DISTANCE);
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
    rayOutput[int2(ray, slot)] = traceRay(ddgiProbePositionOf(storage, state), ddgiRayDirection(ray));
}
