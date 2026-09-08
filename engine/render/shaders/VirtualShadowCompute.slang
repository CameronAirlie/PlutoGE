#include "VirtualShadowCommon.slang"
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { VirtualShadowParameters vsm; };
[[vk::binding(1, 0)]] Sampler2D receiverDepth : register(t1, space0);
[[vk::binding(2, 0)]] RWStructuredBuffer<VirtualShadowPage> pages : register(u2, space0);
[[vk::binding(3, 0)]] StructuredBuffer<VirtualShadowCaster> casters : register(t3, space0);
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> pageLists : register(u4, space0);
[[vk::binding(5, 0)]] RWStructuredBuffer<uint> indirectCommands : register(u5, space0);
[[vk::binding(6, 0)]] RWStructuredBuffer<uint> requests : register(u6, space0);
[[vk::binding(7, 0)]] RWStructuredBuffer<uint> stats : register(u7, space0);
[[vk::binding(8, 0)]] RWTexture2D<uint> requestMask : register(u8, space0);
[[vk::binding(9, 0)]] RWTexture2D<float> pageTable : register(u9, space0);

[shader("compute")][numthreads(64, 1, 1)]
void resetMain(uint3 id : SV_DispatchThreadID)
{
    uint index = id.x;
    if (index < PLUTO_VSM_LEVELS * PLUTO_VSM_LEVEL_PAGES)
    {
        uint2 at = virtualPageCoordinate(index);
        const uint level = index / PLUTO_VSM_LEVEL_PAGES;
        const uint2 local = virtualPageCoordinate(index % PLUTO_VSM_LEVEL_PAGES);
        const bool root = level == PLUTO_VSM_ROOT_LEVEL && all(local < PLUTO_VSM_ROOT_GRID);
        requestMask[at] = root ? 1u : 0u;
        pageTable[at] = 0;
        if (root)
            requests[PLUTO_VSM_LEVELS + level * PLUTO_VSM_LEVEL_PAGES + local.y * PLUTO_VSM_ROOT_GRID + local.x] =
                local.y * PLUTO_VSM_GRID + local.x;
    }
    if (index < PLUTO_VSM_LEVELS) requests[index] = index == PLUTO_VSM_ROOT_LEVEL ? PLUTO_VSM_ROOT_PAGES : 0;
    if (index < 16) stats[index] = 0;
    if (index < PLUTO_VSM_CAPACITY && vsm.settings.z > 0.5)
        pages[index] = (VirtualShadowPage)0;
}
float3 reconstruct(uint2 pixel, float depth)
{
    float2 uv = (float2(pixel) + 0.5) / float2(vsm.viewport.xy);
    if (vsm.viewport.z != 0) uv.y = 1.0 - uv.y;
    float z = vsm.viewport.w != 0 ? depth : depth * 2.0 - 1.0;
    float4 world = mul(vsm.inverseViewProjection, float4(uv * 2.0 - 1.0, z, 1));
    return world.xyz / world.w;
}
void requestFootprint(float3 world, int level)
{
    float2 uv = virtualCoordinate(vsm, level, world).xy;
    float guard = (clamp(vsm.settings.x, 0.0, 4.0) + 2.0) / virtualResolution(level);
    int2 lo = clamp(int2(floor((uv - guard) * virtualGrid(level))), 0, int(virtualGrid(level)) - 1);
    int2 hi = clamp(int2(floor((uv + guard) * virtualGrid(level))), 0, int(virtualGrid(level)) - 1);
    for (int y = lo.y; y <= hi.y; ++y)
        for (int x = lo.x; x <= hi.x; ++x)
        {
            uint previous;
            InterlockedOr(requestMask[uint2(x, y + level * PLUTO_VSM_GRID)], 1u, previous);
            if (previous == 0)
            {
                uint offset;
                InterlockedAdd(requests[level], 1u, offset);
                requests[PLUTO_VSM_LEVELS + level * PLUTO_VSM_LEVEL_PAGES + offset] = y * PLUTO_VSM_GRID + x;
            }
        }
}
[shader("compute")][numthreads(8, 8, 1)]
void requestMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= vsm.viewport.xy)) return;
    float depth = receiverDepth.SampleLevel((float2(id.xy) + 0.5) / float2(vsm.viewport.xy), 0).r;
    if (depth <= 0.0) return;
    float3 world = reconstruct(id.xy, depth);
    float footprint = virtualReceiverFootprint(vsm, world);
    int level = selectVirtualLevel(vsm, world, footprint);
    if (level >= 0) requestFootprint(world, level);
}
[shader("compute")][numthreads(1, 1, 1)]
void allocateMain(uint3 id : SV_DispatchThreadID)
{
    uint counts[PLUTO_VSM_LEVELS];
    uint quotas[PLUTO_VSM_LEVELS];
    uint spare = PLUTO_VSM_CAPACITY;
    for (uint level = 0; level < PLUTO_VSM_LEVELS; ++level)
    {
        counts[level] = 0;
        const uint share = level == PLUTO_VSM_ROOT_LEVEL ? PLUTO_VSM_ROOT_PAGES :
            (PLUTO_VSM_CAPACITY - PLUTO_VSM_ROOT_PAGES) / PLUTO_VSM_FINE_LEVELS;
        quotas[level] = min(requests[level], share);
        spare -= quotas[level];
    }
    for (uint round = 0; round < PLUTO_VSM_CAPACITY && spare > 0; ++round)
        for (uint level = 0; level < PLUTO_VSM_LEVELS && spare > 0; ++level)
            if (quotas[level] < requests[level]) { ++quotas[level]; --spare; }
    uint available = PLUTO_VSM_CAPACITY;
    // Pin still-requested residents before assigning new pages. Atomic request
    // order must never evict useful cached depth from one stationary frame to another.
    for (uint slot = 0; slot < PLUTO_VSM_CAPACITY; ++slot)
    {
        VirtualShadowPage p = pages[slot];
        p.state.w = 0;
        int level = p.address.z;
        int2 local = p.address.xy - vsm.origins[level].xy;
        bool retained = p.state.y != 0 && p.address.w == vsm.origins[level].z &&
            all(local >= 0) && all(local < int(virtualGrid(level)));
        if (retained && counts[level] < quotas[level] && requestMask[uint2(local.x, local.y + level * PLUTO_VSM_GRID)] != 0)
        {
            p.state.x = local.y * PLUTO_VSM_GRID + local.x;
            p.state.y = vsm.limits.y;
            requestMask[uint2(local.x, local.y + level * PLUTO_VSM_GRID)] = 2;
            ++counts[level]; --available;
        }
        pages[slot] = p;
    }
    // Borrow unused shares, but guarantee coverage to newly visible levels
    // rather than allowing an old near-level working set to starve them.
        for (uint level = 0; level < PLUTO_VSM_LEVELS && available > 0; ++level)
            for (uint index = 0; index < requests[level] && available > 0; ++index)
            {
                if (counts[level] >= quotas[level]) break;
                uint key = requests[PLUTO_VSM_LEVELS + level * PLUTO_VSM_LEVEL_PAGES + index];
                uint2 local = virtualPageCoordinate(key);
                uint2 at = local + uint2(0, level * PLUTO_VSM_GRID);
                if (requestMask[at] != 1) continue;
                uint slot = 0;
                while (pages[slot].state.y == vsm.limits.y) ++slot;
                VirtualShadowPage p = pages[slot];
                if (p.state.y != 0) ++stats[4];
                p.address = int4(vsm.origins[level].xy + int2(local), int(level), vsm.origins[level].z);
                p.state = uint4(key, vsm.limits.y, 0, 0);
                pages[slot] = p;
                requestMask[at] = 2;
                ++counts[level]; --available;
            }
    for (uint level = 0; level < PLUTO_VSM_LEVELS; ++level) stats[0] += requests[level];
    stats[1] = PLUTO_VSM_CAPACITY - available;
    stats[5] = stats[0] - stats[1];
}
bool intersects(VirtualShadowCaster caster, VirtualShadowPage page)
{
    if (caster.bounds.w < 0) return true;
    uint level = page.address.z;
    float3 uv = virtualCoordinate(vsm, level, caster.bounds.xyz);
    float2 radius = caster.bounds.w / vsm.metrics[level].w;
    float2 lo = float2(virtualPageCoordinate(page.state.x)) / virtualGrid(level);
    float2 hi = lo + 1.0 / virtualGrid(level);
    float depthRadius = caster.bounds.w / vsm.metrics[level].y;
    return all(uv.xy + radius >= lo) && all(uv.xy - radius <= hi) &&
        uv.z + depthRadius >= 0 && uv.z - depthRadius <= 1;
}
uint mixHash(uint hash, uint value) { return (hash ^ value) * 16777619u; }
[shader("compute")][numthreads(64, 1, 1)]
void signatureMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= PLUTO_VSM_CAPACITY) return;
    VirtualShadowPage p = pages[id.x];
    if (p.state.y != vsm.limits.y) return;
    uint2 hash = uint2(2166136261u, 3339675911u);
    uint cost = 0;
    for (uint index = 0; index < vsm.limits.x; ++index)
    {
        VirtualShadowCaster caster = casters[index];
        if (!intersects(caster, p)) continue;
        hash.x = mixHash(mixHash(hash.x, caster.identity.x), caster.identity.y);
        hash.y = mixHash(mixHash(hash.y, caster.identity.y), caster.identity.x);
        // Saturate at the budget plus one: huge meshes must not wrap to a cheap page.
        cost = min(vsm.limits.w + 1, cost + min(vsm.limits.w + 1, min(vsm.limits.w + 1, caster.draw.x / 3) * caster.draw.z));
    }
    p.pending = uint4(hash, cost, 0);
    pages[id.x] = p;
}
[shader("compute")][numthreads(1, 1, 1)]
void budgetMain(uint3 id : SV_DispatchThreadID)
{
    uint remaining = vsm.limits.w;
    uint updates = 0;
    // Establish coarse coverage before refining newly visible pages. Dirty
    // resident pages retain their previous depth until replacement is recorded,
    // so budget pressure delays shadow motion rather than flashing fully lit.
    for (uint priority = 0; priority < 3; ++priority)
        for (uint index = 0; index < PLUTO_VSM_CAPACITY; ++index)
        {
            uint slot = (index + vsm.limits.y * 13) % PLUTO_VSM_CAPACITY;
            VirtualShadowPage p = pages[slot];
            if (p.state.y != vsm.limits.y) continue;
            if (p.state.z != 0 && all(p.signature.xy == p.pending.xy))
            {
                if (priority == 2) ++stats[3];
                continue;
            }
            const uint pagePriority = p.state.z != 0 ? 2u :
                (p.address.z == PLUTO_VSM_ROOT_LEVEL ? 0u : 1u);
            if (pagePriority != priority) continue;
            ++stats[2];
            if (updates < vsm.limits.z && p.pending.z <= remaining)
            {
                p.state.w = 1;
                remaining -= p.pending.z;
                ++updates;
            }
            else ++stats[8];
            pages[slot] = p;
        }
    stats[7] = vsm.limits.w - remaining;
    stats[9] = updates;
}
[shader("compute")][numthreads(64, 1, 1)]
void binMain(uint3 id : SV_DispatchThreadID)
{
    uint index = id.x;
    if (index >= vsm.limits.x) return;
    VirtualShadowCaster caster = casters[index];
    uint count = 0;
    for (uint slot = 0; slot < PLUTO_VSM_CAPACITY; ++slot)
    {
        VirtualShadowPage p = pages[slot];
        if (p.state.y == vsm.limits.y && p.state.w != 0 && intersects(caster, p))
            pageLists[index * PLUTO_VSM_CAPACITY + count++] = slot;
    }
    uint command = index * 5;
    indirectCommands[command] = caster.draw.x;
    indirectCommands[command + 1] = count * caster.draw.z;
    indirectCommands[command + 2] = caster.draw.y;
    indirectCommands[command + 3] = 0;
    indirectCommands[command + 4] = 0;
    InterlockedAdd(stats[6], count);
    if (count != 0) InterlockedAdd(stats[10], 1u);
}
[shader("compute")][numthreads(64, 1, 1)]
void publishMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= PLUTO_VSM_CAPACITY) return;
    VirtualShadowPage p = pages[id.x];
    if (p.state.y != vsm.limits.y) return;
    if (p.state.w != 0) { p.signature = p.pending; p.state.z = 1; }
    if (p.state.z != 0)
        pageTable[virtualPageCoordinate(p.state.x) + uint2(0, p.address.z * PLUTO_VSM_GRID)] = float(id.x + 1);
    pages[id.x] = p;
}
