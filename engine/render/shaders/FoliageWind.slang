// Foliage wind: the vertex deformation every pass that draws wind-animated geometry applies (BasicLit forward and
// depth, DirectionalShadow, VirtualShadowRaster, VCTVoxelize). Deformation is a pure function of the vertex's mesh
// position, the instance transform, the packed parameters and time, so every pass agrees and shadows follow the
// plants. FoliageWind.cpp packs the parameters and mirrors these functions on the CPU; keep them in step.
#ifndef PLUTO_FOLIAGE_WIND_SLANG
#define PLUTO_FOLIAGE_WIND_SLANG
#include "../include/PlutoGE/render/FoliageWindLayout.h"

static const float foliageWindTau = 6.28318530718;

// FoliageWindParameters in FoliageWind.h. All zero for draws that do not move.
struct FoliageWindParameters
{
    float4 field;    // wind direction XZ, normalized strength, gustiness
    float4 timing;   // time, previous frame's time, 1 / gust size, gust travel speed (m/s)
    float4 response; // sway, sway frequency (Hz), flutter amplitude, flutter frequency (Hz)
    float4 shape;    // pivot height, 1 / bend height, bend height, 1 when active
};

bool foliageWindActive(FoliageWindParameters wind)
{
    return wind.shape.w > 0.0;
}

// Gust level in [0, 1] at a root position: SceneWindGust's waveform.
float foliageWindGust(FoliageWindParameters wind, float2 root, float time)
{
    const float2 direction = wind.field.xy;
    const float2 across = float2(-direction.y, direction.x);
    const float wave = (dot(root, direction) - time * wind.timing.w) * wind.timing.z;
    return 0.5 + PLUTO_WIND_GUST_PRIMARY * sin(foliageWindTau * wave) +
           PLUTO_WIND_GUST_SECONDARY * sin(foliageWindTau * (wave * PLUTO_WIND_GUST_SECONDARY_SCALE +
                                                              dot(root, across) * wind.timing.z * PLUTO_WIND_GUST_CROSS) +
                                           PLUTO_WIND_GUST_SECONDARY_PHASE);
}

// A plant's phase in [0, 1) from an integer hash of its root cell; integer math gives every pass the same value.
float foliageWindSeed(float2 root)
{
    const int2 cell = int2(floor(root * PLUTO_WIND_SEED_CELLS_PER_METRE));
    uint hash = uint(cell.x) * 0x8da6b343u ^ uint(cell.y) * 0xd8163841u;
    hash ^= hash >> 16u;
    hash *= 0x7feb352du;
    hash ^= hash >> 15u;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16u;
    return float(hash & 0xffffu) / 65536.0;
}

// World-space displacement of the vertex at localPosition (mesh space) of an instance drawn with model.
//  - Sway: the plant leans downwind with the gust passing its root, and oscillates about that lean with a phase of
//    its own. The lean grows with the square of the height above the pivot, so roots stay planted.
//  - Flutter: a small, fast motion that varies per vertex, for leaves and blade tips.
//  - Leaning geometry drops so stems keep roughly their length.
float3 foliageWindOffset(FoliageWindParameters wind, float4x4 model, float3 localPosition, float time)
{
    const float3 origin = mul(model, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    const float upScale = length(mul((float3x3)model, float3(0.0, 1.0, 0.0)));
    const float above = localPosition.y - wind.shape.x;
    const float relative = clamp(above * wind.shape.y, 0.0, PLUTO_WIND_MAX_RELATIVE_HEIGHT);
    const float bendWorld = wind.shape.z * upScale;
    const float2 direction = wind.field.xy;
    const float strength = wind.field.z;
    const float2 root = origin.xz;

    const float gust = 1.0 + wind.field.w * (2.0 * foliageWindGust(wind, root, time) - 1.0);
    const float seed = foliageWindSeed(root);
    const float swayPhase = time * wind.response.y;
    const float oscillation = 0.7 * sin(foliageWindTau * (swayPhase + seed)) +
                              0.3 * sin(foliageWindTau * (swayPhase * 2.13 + seed * 1.93));
    const float lean = wind.response.x * strength *
                       (gust * PLUTO_WIND_LEAN_SHARE + PLUTO_WIND_OSCILLATION_SHARE * oscillation);
    float3 offset = float3(direction.x, 0.0, direction.y) * (lean * relative * relative * bendWorld);

    const float vertexPhase = dot(localPosition, float3(3.17, 1.73, 2.41)) + seed;
    const float flutterPhase = time * wind.response.w;
    const float3 flutterDirection = float3(sin(foliageWindTau * (flutterPhase + vertexPhase)),
                                           0.5 * sin(foliageWindTau * (flutterPhase * 1.31 + vertexPhase * 1.7)),
                                           cos(foliageWindTau * (flutterPhase * 0.87 + vertexPhase * 1.3)));
    offset += flutterDirection * (wind.response.z * strength * gust * relative * bendWorld);

    const float height = max(above * upScale, 0.001);
    const float horizontal = length(offset.xz);
    offset.y -= min(horizontal * horizontal / (2.0 * height), horizontal);
    return offset;
}

// The world position of a vertex with wind applied; draws without wind keep the rigid position exactly.
float3 foliageWindPosition(FoliageWindParameters wind, float4x4 model, float3 localPosition, float3 world, float time)
{
    if (foliageWindActive(wind))
        world += foliageWindOffset(wind, model, localPosition, time);
    return world;
}
#endif
