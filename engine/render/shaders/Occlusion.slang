struct Candidate { float4 bounds; float4 extents; uint4 draw; };
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0)
{
    column_major float4x4 viewProjection;
    uint4 source; // width, height, offset, sample depth texture
    uint4 destination; // width, height, offset
    uint4 config; // candidate count, flags (bit 0: ZO depth, bit 1: Vulkan Y), cull, level count
    uint4 levels[32]; // width, height, offset
};
[[vk::binding(1, 0)]] Sampler2D depthTexture : register(t1);
[[vk::binding(2, 0)]] RWStructuredBuffer<float> hierarchy : register(u2);
[[vk::binding(3, 0)]] StructuredBuffer<Candidate> candidates : register(t3);
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> indirect : register(u4);
[[vk::binding(5, 0)]] RWStructuredBuffer<uint> counters : register(u5);

[shader("compute")]
[numthreads(8, 8, 1)]
void reduceMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= destination.xy)) return;
    // Reverse Z: the farthest sample is the conservative occlusion depth.
    // Missing texels at odd image edges are uncovered, never occluders.
    float value = 1.0;
    for (uint y = 0; y < 2; ++y)
        for (uint x = 0; x < 2; ++x)
        {
            uint2 p = id.xy * 2 + uint2(x, y);
            float sampleDepth = 0;
            if (all(p < source.xy))
                sampleDepth = source.w != 0 ? depthTexture.Load(int3(p, 0)).r
                    : hierarchy[source.z + p.y * source.x + p.x];
            value = min(value, sampleDepth);
        }
    hierarchy[destination.z + id.y * destination.x + id.x] = value;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void testMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= config.x) return;
    Candidate candidate = candidates[id.x];
    bool validBounds = candidate.bounds.w >= 0 && all(isfinite(candidate.bounds)) &&
        all(isfinite(candidate.extents)) && all(candidate.extents.xyz >= 0);
    bool valid = candidate.draw.z != 0 && validBounds;
    if (candidate.draw.z == 0) InterlockedAdd(counters[3], 1);
    else if (!validBounds) InterlockedAdd(counters[4], 1);
    if (valid && candidate.draw.w != 0) InterlockedAdd(counters[9], 1);
    float2 low = float2(1, 1), high = float2(-1, -1);
    float nearest = 0;
    for (uint corner = 0; corner < 8 && valid; ++corner)
    {
        float3 sign = float3((corner & 1) ? 1.0 : -1.0, (corner & 2) ? 1.0 : -1.0, (corner & 4) ? 1.0 : -1.0);
        float4 clip = mul(viewProjection, float4(candidate.bounds.xyz + sign * candidate.extents.xyz, 1));
        // Near-plane/camera intersections and invalid projections fail open.
        if (!all(isfinite(clip)) || clip.w <= 0.00001 || clip.z >= clip.w ||
            clip.z < ((config.y & 1) != 0 ? 0 : -clip.w)) { valid = false; break; }
        float3 ndc = clip.xyz / clip.w;
        if ((config.y & 2) != 0) ndc.y = -ndc.y;
        low = min(low, ndc.xy); high = max(high, ndc.xy);
        nearest = max(nearest, (config.y & 1) != 0 ? ndc.z : ndc.z * 0.5 + 0.5);
    }
    bool hidden = false;
    if (valid && all(high >= -1) && all(low <= 1))
    {
        // Expand by a pixel for rasterization precision and bounds rounding.
        float2 first = max(floor((low * 0.5 + 0.5) * source.xy) - 1, 0);
        float2 last = min(ceil((high * 0.5 + 0.5) * source.xy) + 1, float2(source.xy) - 1);
        int level = 0;
        float divisor = 2;
        while (level + 1 < int(config.w) && any((last - first) / divisor > 2)) { ++level; divisor *= 2; }
        bool refined = false;
        uint remaining = 256;
        // Coarse cells can contain uncovered pixels outside the object's
        // rectangle. Retry finer levels, including original depth texels,
        // rather than treating that inconclusive result as visibility proof.
        // Bound the total reads so large/exposed objects cannot monopolize GPU time.
        for (;;)
        {
            uint2 a = uint2(first / divisor), b = uint2(last / divisor);
            uint required = (b.x - a.x + 1) * (b.y - a.y + 1);
            if (required > remaining) { InterlockedAdd(counters[8], 1); break; }
            remaining -= required;
            float farthest = 1;
            for (uint y = a.y; y <= b.y && nearest + 0.00001 < farthest; ++y)
                for (uint x = a.x; x <= b.x; ++x)
                {
                    float depth;
                    if (level >= 0)
                        depth = hierarchy[levels[level].z + y * levels[level].x + x];
                    else
                        depth = depthTexture.Load(int3(x, y, 0)).r;
                    farthest = min(farthest, depth);
                    if (nearest + 0.00001 >= farthest) break;
                }
            hidden = nearest + 0.00001 < farthest;
            if (hidden || level < 0) break;
            if (!refined) { InterlockedAdd(counters[6], 1); refined = true; }
            --level;
            divisor *= 0.5;
        }
        if (hidden && refined) InterlockedAdd(counters[7], 1);
        InterlockedAdd(counters[0], 1);
    }
    else if (candidate.draw.z != 0 && validBounds)
        InterlockedAdd(counters[5], 1);
    if (hidden)
    {
        InterlockedAdd(counters[1], 1);
        InterlockedAdd(counters[2], candidate.draw.x / 3);
    }
    uint offset = id.x * 5;
    indirect[offset] = candidate.draw.x;
    indirect[offset + 1] = hidden && config.z != 0 ? 0 : 1;
    indirect[offset + 2] = candidate.draw.y;
    indirect[offset + 3] = indirect[offset + 4] = 0;
}
