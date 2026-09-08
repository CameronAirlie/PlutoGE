#include "../include/PlutoGE/render/VirtualShadowConfig.h"
struct VirtualShadowParameters
{
    column_major float4x4 matrices[PLUTO_VSM_LEVELS];
    column_major float4x4 inverseViewProjection;
    column_major float4x4 viewProjection;
    int4 origins[PLUTO_VSM_LEVELS]; // absolute page XY, projection epoch, unused
    float4 metrics[PLUTO_VSM_LEVELS]; // world texel, depth range, world page, span
    uint4 viewport; // width, height, flipped framebuffer Y, zero-to-one clip depth
    uint4 limits; // caster chunks, frame, page budget, triangle budget
    float4 settings; // softness, enabled, reset cache, unused
    float4 camera;
};
struct VirtualShadowPage
{
    int4 address; // absolute page XY, level, projection epoch
    uint4 state; // current virtual key, selected frame, valid depth, scheduled dirty
    uint4 signature; // cached hash low/high, unused
    uint4 pending; // candidate hash low/high, triangle cost, unused
};
struct VirtualShadowCaster
{
    float4 bounds;
    uint4 draw; // index count, first index, model count, original draw index
    uint4 identity;
};
uint2 virtualPageCoordinate(uint key) { return uint2(key % PLUTO_VSM_GRID, key / PLUTO_VSM_GRID); }
uint virtualGrid(uint level) { return PLUTO_VSM_LEVEL_GRID(level); }
uint virtualResolution(uint level) { return virtualGrid(level) * PLUTO_VSM_PAGE_SIZE; }
float3 virtualCoordinate(VirtualShadowParameters p, uint level, float3 world)
{
    float3 clip = mul(p.matrices[level], float4(world, 1)).xyz;
    return float3(clip.xy * 0.5 + 0.5, clip.z);
}
int selectVirtualLevel(VirtualShadowParameters p, float3 world, float footprint)
{
    for (int level = 0; level < PLUTO_VSM_LEVELS; ++level)
    {
        float3 uv = virtualCoordinate(p, level, world);
        float guard = 7.0 / virtualResolution(level);
        if (all(uv.xy > guard) && all(uv.xy < 1.0 - guard) && uv.z >= 0 && uv.z <= 1 &&
            (p.metrics[level].x * 2.0 >= footprint || level >= PLUTO_VSM_FINE_LEVELS - 1)) return level;
    }
    return -1;
}
