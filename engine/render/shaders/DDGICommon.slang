// Dynamic diffuse global illumination: the probe volume's parameters, addressing, ray directions and irradiance
// lookup, shared by every DDGI stage (DDGIVoxelTrace, DDGIProbeUpdate, DDGIApply). After Majercik et al., "Dynamic
// Diffuse Global Illumination with Ray-Traced Irradiance Fields" (JCGT 2019) and NVIDIA RTXGI.
//
// Conventions match the rest of the renderer's ambient lighting: irradiance texels hold the cosine-weighted mean
// radiance (irradiance / pi, as samplePhysicalSkyIrradiance and the baked probe grid), and a receiver's diffuse
// light is that value times albedo (1 - F)(1 - metallic) / pi.
//
// The volume is a set of nested, camera-centred cascades (DdgiProbeGrid.h), each three times the spacing of the one
// inside it. Grid coordinate g (0..counts-1) of cascade c is that cascade's probe g in its current placement; its data
// lives at storage coordinate (originCell[c] + g) mod counts, so scrolling never moves data. Its global index is
// c * probesPerCascade + storage index, which owns tile (index mod tilesPerRow, index / tilesPerRow) of the atlases
// and one texel of the probe state texture.
#ifndef PLUTO_DDGI_COMMON
#define PLUTO_DDGI_COMMON

#include "../include/PlutoGE/render/DdgiConfig.h"
#include "../include/PlutoGE/render/PhysicalSkyLayout.h"

#ifndef PI
#define PI 3.14159265358979
#endif

// Mirrors DdgiGpuParameters (DdgiVolume.cpp). The physical-sky lanes keep the names PhysicalSkyEnvironment.slang
// expects, so ray misses evaluate exactly the sky the geometry pass lit with.
[[vk::binding(0, 0)]] cbuffer DdgiParameters : register(b0, space0)
{
    float4 ddgiCascadeOrigins[PLUTO_DDGI_MAX_CASCADES]; // xyz position of grid coordinate 0, w probe spacing
    int4 ddgiCascadeCells[PLUTO_DDGI_MAX_CASCADES];     // xyz absolute cell of grid coordinate 0
    int4 ddgiCounts;          // xyz probes per axis, w atlas tiles per row
    uint4 ddgiVolume;         // x cascades, y probes per cascade, z probes in all cascades
    uint4 ddgiRays;           // x fixed rays, y blended rays, z probes listed this frame, w traced rays per probe
    float4 ddgiRotation[3];   // rows of this frame's rotation of the blended ray directions
    float4 ddgiBlend;         // x hysteresis, y brightness clamp, z visibility sharpness exponent,
                              // w visibility range (spacing units)
    float4 ddgiBias;          // x normal bias, y view bias (spacing units), z minimum normal bias (world units)
    float4 ddgiState;         // x relocation, y classification, z min frontface distance (spacing units),
                              // w multiple-bounce gain (hits reflect the volume's irradiance)
    float4 ddgiApply;         // x intensity, y debug view, z debug probe radius (spacing units), w probes to reset
                              // after the listed ones (negative: every probe)
    float4 ddgiReflection;    // x reflection occlusion strength (0: off), y minimum sine of the visibility lookup's
                              // elevation above the receiver's tangent plane, z sky visibility lobe exponent
    uint4 ddgiAtlas;         // xy irradiance atlas size, zw distance atlas size
    float4 ddgiSky;           // x physical sky enabled, y flat ambient radiance
    float4 physicalSkyParameters[PLUTO_SKY_LANE_COUNT];
    float4 physicalSkySettings;
};

uint ddgiCascades() { return ddgiVolume.x; }
float ddgiSpacing(uint cascade) { return ddgiCascadeOrigins[cascade].w; }
// Probes in all cascades.
uint ddgiProbeCount() { return ddgiVolume.z; }
uint ddgiCascadeOf(uint index) { return index / ddgiVolume.y; }

// GLSL leaves % undefined for negative operands, and grid cells left of the world origin are negative: floor division
// is exact for every cell index a float can address.
int3 ddgiPositiveModulo(int3 value, int3 divisor)
{
    return value - divisor * int3(floor(float3(value) / float3(divisor)));
}

int3 ddgiStorageCoordinate(uint cascade, int3 gridCoordinate)
{
    return ddgiPositiveModulo(ddgiCascadeCells[cascade].xyz + gridCoordinate, ddgiCounts.xyz);
}

// Global index of a cascade's storage coordinate.
uint ddgiStorageIndex(uint cascade, int3 storage)
{
    return cascade * ddgiVolume.y + uint(storage.x + ddgiCounts.x * (storage.y + ddgiCounts.y * storage.z));
}

// Storage coordinate of a global index within its cascade.
int3 ddgiStorageFromIndex(uint index)
{
    const uint local = index % ddgiVolume.y;
    const uint layer = uint(ddgiCounts.x * ddgiCounts.y);
    return int3(int(local % uint(ddgiCounts.x)), int((local % layer) / uint(ddgiCounts.x)), int(local / layer));
}

int3 ddgiGridFromStorage(uint cascade, int3 storage)
{
    return ddgiPositiveModulo(storage - ddgiCascadeCells[cascade].xyz, ddgiCounts.xyz);
}

int2 ddgiTile(uint storageIndex)
{
    const uint perRow = uint(ddgiCounts.w);
    return int2(int(storageIndex % perRow), int(storageIndex / perRow));
}

// A traced ray that left the scene (IDdgiRaySource reports PLUTO_DDGI_MISS_DISTANCE; half of it is still far beyond
// any distance a ray source traces, and survives the ray buffer's half precision).
bool ddgiIsMiss(float hitDistance) { return hitDistance >= PLUTO_DDGI_MISS_DISTANCE * 0.5; }

// Probe state: xyz relocation offset in spacing units, w PLUTO_DDGI_PROBE_* flags.
uint ddgiFlags(float4 state) { return uint(state.w + 0.5); }
bool ddgiHasFlag(float4 state, uint flag) { return (ddgiFlags(state) & flag) != 0; }

float3 ddgiProbePosition(uint cascade, int3 gridCoordinate, float4 state)
{
    return ddgiCascadeOrigins[cascade].xyz + (float3(gridCoordinate) + state.xyz) * ddgiSpacing(cascade);
}

// The world position of the probe with global index `index`.
float3 ddgiProbePositionOf(uint index, float4 state)
{
    const uint cascade = ddgiCascadeOf(index);
    return ddgiProbePosition(cascade, ddgiGridFromStorage(cascade, ddgiStorageFromIndex(index)), state);
}

float2 ddgiSignNotZero(float2 value) { return float2(value.x >= 0.0 ? 1.0 : -1.0, value.y >= 0.0 ? 1.0 : -1.0); }

// Octahedral mapping of a unit direction to [-1, 1]^2 and back.
float2 ddgiOctEncode(float3 direction)
{
    const float3 n = direction / (abs(direction.x) + abs(direction.y) + abs(direction.z));
    return n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * ddgiSignNotZero(n.xy);
}

float3 ddgiOctDecode(float2 coordinate)
{
    float3 direction = float3(coordinate, 1.0 - abs(coordinate.x) - abs(coordinate.y));
    if (direction.z < 0.0)
        direction.xy = (1.0 - abs(direction.yx)) * ddgiSignNotZero(direction.xy);
    return normalize(direction);
}

// The direction an interior texel of a tile represents.
float3 ddgiTexelDirection(int2 interiorTexel, int interior)
{
    return ddgiOctDecode((float2(interiorTexel) + 0.5) / float(interior) * 2.0 - 1.0);
}

// Atlas UV of `direction` in a probe's tile, inside its interior so bilinear filtering stays within the tile.
float2 ddgiAtlasUv(uint storageIndex, float3 direction, int interior, int tile, float2 atlasSize)
{
    const float2 interiorUv = ddgiOctEncode(direction) * 0.5 + 0.5;
    const float2 texel = float2(ddgiTile(storageIndex) * tile) + 1.0 + interiorUv * float(interior);
    return texel / atlasSize;
}

float3 ddgiSphericalFibonacci(float index, float count)
{
    const float goldenFraction = 0.61803398875;
    const float phi = 2.0 * PI * frac(index * goldenFraction);
    const float cosTheta = 1.0 - (2.0 * index + 1.0) / count;
    const float sinTheta = sqrt(saturate(1.0 - cosTheta * cosTheta));
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

// Ray r of a probe update: the fixed rays come first, unrotated, so classification and relocation compare the same
// directions every frame; the blended rays follow, rotated by this frame's rotation.
float3 ddgiRayDirection(uint ray)
{
    if (ray < ddgiRays.x)
        return ddgiSphericalFibonacci(float(ray), float(ddgiRays.x));
    const float3 direction = ddgiSphericalFibonacci(float(ray - ddgiRays.x), float(ddgiRays.y));
    return float3(dot(ddgiRotation[0].xyz, direction), dot(ddgiRotation[1].xyz, direction),
                  dot(ddgiRotation[2].xyz, direction));
}

#ifdef PLUTO_DDGI_SAMPLING
// The cell of a cascade's probe grid that holds a surface lookup, offset off the surface by the biases.
struct DdgiCell
{
    float3 biasedPosition;
    // The biased position in grid coordinates, and the grid coordinate of the cell's lowest corner.
    float3 local;
    int3 base;
};

// False when the biased lookup lies outside the cascade.
bool ddgiCell(uint cascade, float3 position, float3 normal, float3 viewDirection, out DdgiCell cell)
{
    const float spacing = ddgiSpacing(cascade);
    cell.biasedPosition = position + normal * max(ddgiBias.x * spacing, ddgiBias.z) +
                          viewDirection * (ddgiBias.y * spacing);
    cell.local = (cell.biasedPosition - ddgiCascadeOrigins[cascade].xyz) / spacing;
    cell.base = clamp(int3(floor(cell.local)), int3(0), ddgiCounts.xyz - 2);
    return all(cell.local >= 0.0) && all(cell.local <= float3(ddgiCounts.xyz - 1));
}

// Falls from one to zero over the cascade's outermost cell, so its edge does not show as a seam.
float ddgiCellCoverage(DdgiCell cell)
{
    const float3 edge = min(cell.local, float3(ddgiCounts.xyz - 1) - cell.local);
    return saturate(min(min(edge.x, edge.y), edge.z));
}

// The weight of corner probe `corner` (0-7) of `cell` for a surface at `position` facing `normal`: trilinear, a
// smooth backface term and Chebyshev visibility from the probe's distance moments. Zero for probes without data;
// `storage` returns the probe's global index.
float ddgiCornerWeight(uint cascade, DdgiCell cell, int corner, float3 position, float3 normal, out uint storage)
{
    const int3 offset = int3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
    const int3 gridCoordinate = cell.base + offset;
    storage = ddgiStorageIndex(cascade, ddgiStorageCoordinate(cascade, gridCoordinate));
    const float4 state = probeStateTexture.Load(int3(ddgiTile(storage), 0));
    if (ddgiHasFlag(state, PLUTO_DDGI_PROBE_INACTIVE | PLUTO_DDGI_PROBE_FRESH))
        return 0.0;
    const float3 probePosition = ddgiProbePosition(cascade, gridCoordinate, state);
    const float3 alpha = saturate(cell.local - float3(cell.base));
    const float3 trilinear3 = lerp(1.0 - alpha, alpha, float3(offset));
    const float trilinear = max(0.001, trilinear3.x * trilinear3.y * trilinear3.z);
    // Smooth backface: probes behind the surface contribute little but never exactly zero.
    const float3 toProbe = normalize(probePosition - position);
    const float wrap = (dot(toProbe, normal) + 1.0) * 0.5;
    float weight = wrap * wrap + 0.2;
    // Chebyshev visibility from the biased point.
    const float3 probeToPoint = cell.biasedPosition - probePosition;
    const float distance = length(probeToPoint);
    const float2 moments = distanceAtlas.SampleLevel(
        ddgiAtlasUv(storage, probeToPoint / max(distance, 0.0001), PLUTO_DDGI_DISTANCE_INTERIOR,
                    PLUTO_DDGI_DISTANCE_TILE, float2(ddgiAtlas.zw)), 0.0).rg;
    if (distance > moments.x)
    {
        const float variance = abs(moments.y - moments.x * moments.x);
        const float excess = distance - moments.x;
        float chebyshev = variance / (variance + excess * excess);
        chebyshev = chebyshev * chebyshev * chebyshev;
        weight *= max(chebyshev, 0.05);
    }
    weight = max(weight, 0.000001);
    // Crush tiny weights so a probe seen only through leaks cannot tint the result.
    const float crush = 0.2;
    if (weight < crush)
        weight *= weight * weight / (crush * crush);
    return weight * trilinear;
}

// A probe's cosine-weighted mean radiance arriving from around `direction`.
float3 ddgiProbeIrradiance(uint storage, float3 direction)
{
    return irradianceAtlas.SampleLevel(ddgiAtlasUv(storage, direction, PLUTO_DDGI_IRRADIANCE_INTERIOR,
                                                   PLUTO_DDGI_IRRADIANCE_TILE, float2(ddgiAtlas.xy)), 0.0).rgb;
}

// The share of a probe's rays around `direction` that reached the sky.
float ddgiProbeSkyVisibility(uint storage, float3 direction)
{
    return distanceAtlas.SampleLevel(ddgiAtlasUv(storage, direction, PLUTO_DDGI_DISTANCE_INTERIOR,
                                                 PLUTO_DDGI_DISTANCE_TILE, float2(ddgiAtlas.zw)), 0.0).b;
}

// Irradiance at a surface (cosine-weighted mean radiance) from one cascade's eight surrounding probes. `coverage`
// falls from one to zero over the cascade's outermost cell and is zero where no surrounding probe has data.
float3 ddgiCascadeIrradiance(uint cascade, float3 position, float3 normal, float3 viewDirection, out float coverage)
{
    coverage = 0.0;
    DdgiCell cell;
    if (!ddgiCell(cascade, position, normal, viewDirection, cell))
        return 0.0;
    float3 irradiance = 0.0;
    float weightSum = 0.0;
    [ForceUnroll] for (int corner = 0; corner < 8; ++corner)
    {
        uint storage;
        const float weight = ddgiCornerWeight(cascade, cell, corner, position, normal, storage);
        if (weight <= 0.0)
            continue;
        irradiance += weight * ddgiProbeIrradiance(storage, normal);
        weightSum += weight;
    }
    if (weightSum <= 0.0)
        return 0.0;
    coverage = ddgiCellCoverage(cell);
    return irradiance / weightSum;
}

// Irradiance from the finest cascade that covers the surface, blending into the next one out where the finer fades
// over its outermost cell. `coverage` is how much of the result the cascades supply (zero beyond the widest).
float3 ddgiIrradiance(float3 position, float3 normal, float3 viewDirection, out float coverage)
{
    float3 irradiance = 0.0;
    float remaining = 1.0;
    for (uint cascade = 0; cascade < ddgiCascades() && remaining > 0.001; ++cascade)
    {
        float cascadeCoverage;
        const float3 cascadeIrradiance = ddgiCascadeIrradiance(cascade, position, normal, viewDirection, cascadeCoverage);
        irradiance += cascadeIrradiance * cascadeCoverage * remaining;
        remaining *= 1.0 - cascadeCoverage;
    }
    coverage = 1.0 - remaining;
    return coverage > 0.0 ? irradiance / coverage : 0.0;
}

// Irradiance plus what reflection occlusion (DDGIReflection.slang) reads along a second direction, gathered with the
// same probe weights. Diffuse-only lookups (multiple-bounce hits) use ddgiIrradiance and pay nothing for it.
struct DdgiSample
{
    // Cosine-weighted mean radiance arriving at the surface along its normal.
    float3 irradiance;
    // The same quantity along the reflection lookup direction: the low-frequency light of the surroundings there.
    float3 radiance;
    // Share of the probes' rays along the reflection lookup direction that reached the sky.
    float skyVisibility;
};

DdgiSample ddgiZeroSample()
{
    DdgiSample result;
    result.irradiance = 0.0;
    result.radiance = 0.0;
    result.skyVisibility = 0.0;
    return result;
}

// A sample with `irradiance` and nothing known along the reflection: no light, nothing occluding the sky.
DdgiSample ddgiIrradianceOnly(float3 irradiance)
{
    DdgiSample result = ddgiZeroSample();
    result.irradiance = irradiance;
    result.skyVisibility = 1.0;
    return result;
}

// accumulated + value * weight, field by field.
DdgiSample ddgiAccumulate(DdgiSample accumulated, DdgiSample value, float weight)
{
    accumulated.irradiance += value.irradiance * weight;
    accumulated.radiance += value.radiance * weight;
    accumulated.skyVisibility += value.skyVisibility * weight;
    return accumulated;
}

// ddgiCascadeIrradiance, also reading each probe along `reflection`.
DdgiSample ddgiCascadeSample(uint cascade, float3 position, float3 normal, float3 viewDirection, float3 reflection,
                             out float coverage)
{
    coverage = 0.0;
    DdgiCell cell;
    if (!ddgiCell(cascade, position, normal, viewDirection, cell))
        return ddgiZeroSample();
    DdgiSample result = ddgiZeroSample();
    float weightSum = 0.0;
    [ForceUnroll] for (int corner = 0; corner < 8; ++corner)
    {
        uint storage;
        const float weight = ddgiCornerWeight(cascade, cell, corner, position, normal, storage);
        if (weight <= 0.0)
            continue;
        DdgiSample probe;
        probe.irradiance = ddgiProbeIrradiance(storage, normal);
        probe.radiance = ddgiProbeIrradiance(storage, reflection);
        probe.skyVisibility = ddgiProbeSkyVisibility(storage, reflection);
        result = ddgiAccumulate(result, probe, weight);
        weightSum += weight;
    }
    if (weightSum <= 0.0)
        return ddgiZeroSample();
    coverage = ddgiCellCoverage(cell);
    return ddgiAccumulate(ddgiZeroSample(), result, 1.0 / weightSum);
}

// ddgiIrradiance, also reading the probes along `reflection`.
DdgiSample ddgiSample(float3 position, float3 normal, float3 viewDirection, float3 reflection, out float coverage)
{
    DdgiSample result = ddgiZeroSample();
    float remaining = 1.0;
    for (uint cascade = 0; cascade < ddgiCascades() && remaining > 0.001; ++cascade)
    {
        float cascadeCoverage;
        const DdgiSample cascadeSample = ddgiCascadeSample(cascade, position, normal, viewDirection, reflection,
                                                           cascadeCoverage);
        result = ddgiAccumulate(result, cascadeSample, cascadeCoverage * remaining);
        remaining *= 1.0 - cascadeCoverage;
    }
    coverage = 1.0 - remaining;
    return coverage > 0.0 ? ddgiAccumulate(ddgiZeroSample(), result, 1.0 / coverage) : ddgiZeroSample();
}
#endif

#endif
