// Builds the clustered local-light masks LightClusters.slang reads: one thread per cluster tests the cluster's
// view-space bounding box against every light's (inflated) bounding sphere.
#include "../include/PlutoGE/render/LightClusterConfig.h"

struct LightClusterCullingParameters
{
    column_major float4x4 inverseProjection;
    float4 depth;  // first-slice depth, slices per log-depth unit, farthest light reach, slice-zero near depth
    uint4 counts;  // local lights
    float4 lights[PLUTO_MAX_LOCAL_LIGHTS]; // view-space centre, radius
};

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { LightClusterCullingParameters parameters; };
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> clusterMasks : register(u1, space0);

// Edge tiles also hold surfaces that a raster offset (jitter, guard band) moves just past the NDC border.
static const float EdgeMargin = 0.25;

float sliceDepth(uint slice)
{
    return parameters.depth.x * exp(float(slice) / parameters.depth.y);
}

// The point of the view ray through `ndc` at view depth `depth`. Clip depths 0.5 and 1 are finite under every
// supported projection (reversed, infinite and orthographic), and both lie on that ray.
float3 viewPoint(float2 ndc, float depth)
{
    float4 a = mul(parameters.inverseProjection, float4(ndc, 0.5, 1.0));
    float4 b = mul(parameters.inverseProjection, float4(ndc, 1.0, 1.0));
    a.xyz /= a.w;
    b.xyz /= b.w;
    return lerp(a.xyz, b.xyz, (-depth - a.z) / (b.z - a.z));
}

[shader("compute")]
[numthreads(PLUTO_LIGHT_CLUSTER_GROUP_SIZE, 1, 1)]
void cullMain(uint3 id : SV_DispatchThreadID)
{
    const uint cluster = id.x;
    if (cluster >= PLUTO_LIGHT_CLUSTER_COUNT) return;
    const uint x = cluster % PLUTO_LIGHT_CLUSTER_X;
    const uint y = (cluster / PLUTO_LIGHT_CLUSTER_X) % PLUTO_LIGHT_CLUSTER_Y;
    const uint z = cluster / (PLUTO_LIGHT_CLUSTER_X * PLUTO_LIGHT_CLUSTER_Y);
    const float2 grid = float2(PLUTO_LIGHT_CLUSTER_X, PLUTO_LIGHT_CLUSTER_Y);
    float2 low = float2(x, y) / grid * 2.0 - 1.0;
    float2 high = float2(x + 1, y + 1) / grid * 2.0 - 1.0;
    if (x == 0) low.x -= EdgeMargin;
    if (y == 0) low.y -= EdgeMargin;
    if (x == PLUTO_LIGHT_CLUSTER_X - 1) high.x += EdgeMargin;
    if (y == PLUTO_LIGHT_CLUSTER_Y - 1) high.y += EdgeMargin;
    const float nearDepth = z == 0 ? parameters.depth.w : sliceDepth(z);
    const float farDepth = sliceDepth(z + 1);

    // The cluster is the convex hull of its eight corners.
    float3 minimum = 3.0e38, maximum = -3.0e38;
    bool bounded = true;
    for (uint corner = 0; corner < 8; ++corner)
    {
        const float2 ndc = float2((corner & 1) != 0 ? high.x : low.x, (corner & 2) != 0 ? high.y : low.y);
        const float3 position = viewPoint(ndc, (corner & 4) != 0 ? farDepth : nearDepth);
        bounded = bounded && all(isfinite(position));
        minimum = min(minimum, position);
        maximum = max(maximum, position);
    }

    const uint lightCount = min(parameters.counts.x, uint(PLUTO_MAX_LOCAL_LIGHTS));
    for (uint word = 0; word < PLUTO_LIGHT_CLUSTER_WORDS; ++word)
    {
        uint bits = 0;
        for (uint bit = 0; bit < 32; ++bit)
        {
            const uint light = word * 32 + bit;
            if (light >= lightCount) break;
            const float4 sphere = parameters.lights[light];
            const float3 offset = sphere.xyz - clamp(sphere.xyz, minimum, maximum);
            // Lights without reach (negative radius) light nothing. A degenerate projection keeps every other
            // light: culling must never drop a contribution.
            if (sphere.w >= 0.0 && (!bounded || dot(offset, offset) <= sphere.w * sphere.w))
                bits |= 1u << bit;
        }
        clusterMasks[cluster * PLUTO_LIGHT_CLUSTER_WORDS + word] = bits;
    }
}
