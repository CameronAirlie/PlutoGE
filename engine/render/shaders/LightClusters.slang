// Clustered local lights for surface shading. LightClusterCulling.slang writes one light mask per cluster;
// a surface point finds its cluster from its own view-space position through the projection the culling pass
// used, so raster offsets (jitter, upscaling, viewports) cannot move it into another cluster.
// Requires SceneLightingParameters.slang (view, pointParameters, lightCluster*).
#include "../include/PlutoGE/render/LightClusterConfig.h"

[[vk::binding(4, 0)]] StructuredBuffer<uint> lightClusterMasks : register(t4, space0);

static const uint LocalLightsAll = 0;
static const uint LocalLightsNone = 1;
static const uint LocalLightsCluster = 2;

// Visits local light indices in ascending order, the order of the unculled loop, so sums are unchanged.
struct LocalLightIterator
{
    uint mode;
    uint firstWord;
    uint lightCount;
    uint word;
    uint bits;

    uint wordBits(uint index)
    {
        const uint first = index * 32u;
        if (mode == LocalLightsNone || lightCount <= first) return 0u;
        const uint inRange = lightCount >= first + 32u ? 0xffffffffu : (1u << (lightCount - first)) - 1u;
        return mode == LocalLightsAll ? inRange : lightClusterMasks[firstWord + index] & inRange;
    }

    [mutating] bool next(out uint light)
    {
        while (bits == 0u)
        {
            if (++word >= PLUTO_LIGHT_CLUSTER_WORDS)
            {
                light = 0u;
                return false;
            }
            bits = wordBits(word);
        }
        light = word * 32u + firstbitlow(bits);
        bits &= bits - 1u;
        return true;
    }
};

// lightClusterDepth: first-slice depth, slices per log-depth unit, farthest light reach, enabled.
LocalLightIterator localLights(float3 worldPosition)
{
    LocalLightIterator lights;
    lights.mode = LocalLightsAll;
    lights.firstWord = 0u;
    lights.lightCount = uint(pointParameters.x);
    lights.word = 0u;
    if (lightClusterDepth.w > 0.5)
    {
        const float4 viewPosition = mul(view, float4(worldPosition, 1.0));
        const float depth = -viewPosition.z;
        const float4 clip = mul(lightClusterProjection, viewPosition);
        const float2 ndc = clip.xy / clip.w;
        if (depth >= lightClusterDepth.z)
            lights.mode = LocalLightsNone;
        else if (clip.w > 0.0 && all(isfinite(ndc)))
        {
            const float2 grid = float2(PLUTO_LIGHT_CLUSTER_X, PLUTO_LIGHT_CLUSTER_Y);
            const uint2 tile = uint2(clamp(floor((ndc * 0.5 + 0.5) * grid), float2(0.0), grid - 1.0));
            const uint slice = depth <= lightClusterDepth.x ? 0u
                : min(uint(log(depth / lightClusterDepth.x) * lightClusterDepth.y), uint(PLUTO_LIGHT_CLUSTER_Z - 1));
            lights.mode = LocalLightsCluster;
            lights.firstWord = ((slice * PLUTO_LIGHT_CLUSTER_Y + tile.y) * PLUTO_LIGHT_CLUSTER_X + tile.x) *
                               PLUTO_LIGHT_CLUSTER_WORDS;
        }
    }
    lights.bits = lights.wordBits(0u);
    return lights;
}
