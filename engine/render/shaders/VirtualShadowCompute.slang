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
[[vk::binding(10, 0)]] RWStructuredBuffer<uint4> membership : register(u10, space0);

[shader("compute")][numthreads(64, 1, 1)]
void resetMain(uint3 id : SV_DispatchThreadID)
{
    uint index = id.x;
    if (index < PLUTO_VSM_LEVELS * PLUTO_VSM_LEVEL_PAGES)
    {
        uint2 at = virtualPageCoordinate(index);
        const uint level = index / PLUTO_VSM_LEVEL_PAGES;
        const uint2 local = virtualPageCoordinate(index % PLUTO_VSM_LEVEL_PAGES);
        const bool root = ((level == PLUTO_VSM_ROOT_LEVEL && vsm.camera.w > 0.5) || (level >= PLUTO_VSM_SPOT_ROOT_BASE && vsm.origins[level].w != 0)) && all(local < virtualGrid(vsm, level));
        requestMask[at] = root ? 1u : 0u;
        pageTable[at] = 0;
        if (root)
            requests[PLUTO_VSM_LEVELS + level * PLUTO_VSM_LEVEL_PAGES + local.y * virtualGrid(vsm, level) + local.x] =
                local.y * PLUTO_VSM_GRID + local.x;
    }
    if (index < PLUTO_VSM_LEVELS) requests[index] = ((index == PLUTO_VSM_ROOT_LEVEL && vsm.camera.w > 0.5) || (index >= PLUTO_VSM_SPOT_ROOT_BASE && vsm.origins[index].w != 0)) ? virtualGrid(vsm, index) * virtualGrid(vsm, index) : 0;
    if (index < PLUTO_VSM_COUNTER_COUNT) stats[index] = 0;
    if (index < vsm.pool.x && vsm.settings.z > 0.5)
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
// Direct-mapped workgroup cache: collisions fall back to the global atomic,
// never discard coverage. Entries are immutable after the first insertion.
groupshared uint localRequests[128];
void requestFootprint(float3 world, int level)
{
    float2 uv = virtualCoordinate(vsm, level, world).xy;
    float guard = (clamp(vsm.settings.x, 0.0, 4.0) + 2.0) / virtualResolution(vsm, level);
    if (level >= PLUTO_VSM_DIRECTIONAL_LEVELS)
    {
        // Surface normal bias is applied before perspective projection. Narrow
        // cones magnify that shift; reserve its worst-case footprint as well.
        float clipW = mul(vsm.matrices[level], float4(world, 1)).w;
        float bias = max(0.00001, clipW * 0.0001);
        float axisScale = max(length(vsm.matrices[level][0].xyz), length(vsm.matrices[level][1].xyz));
        guard += 0.5 * bias * (axisScale + 1.0) / max(clipW - bias, 0.000001);
    }
    int2 lo = clamp(int2(floor((uv - guard) * virtualGrid(vsm, level))), 0, int(virtualGrid(vsm, level)) - 1);
    int2 hi = clamp(int2(floor((uv + guard) * virtualGrid(vsm, level))), 0, int(virtualGrid(vsm, level)) - 1);
    for (int y = lo.y; y <= hi.y; ++y)
        for (int x = lo.x; x <= hi.x; ++x)
        {
            uint key = 1u + uint(level) * PLUTO_VSM_LEVEL_PAGES + uint(y) * PLUTO_VSM_GRID + uint(x);
            uint cached;
            InterlockedCompareExchange(localRequests[(key * 2654435761u) >> 25], 0u, key, cached);
            if (cached == key) continue;
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
void requestMain(uint3 id : SV_DispatchThreadID, uint lane : SV_GroupIndex)
{
    localRequests[lane] = 0;
    localRequests[lane + 64] = 0;
    GroupMemoryBarrierWithGroupSync();
    if (any(id.xy >= vsm.viewport.xy)) return;
    float depth = receiverDepth.SampleLevel((float2(id.xy) + 0.5) / float2(vsm.viewport.xy), 0).r;
    if (depth <= 0.0) return;
    float3 world = reconstruct(id.xy, depth);
    float footprint = virtualReceiverFootprint(vsm, world);
    int level = selectVirtualLevel(vsm, world, footprint);
    if (vsm.camera.w > 0.5 && level >= 0) requestFootprint(world, level);
    for (uint spot = PLUTO_VSM_DIRECTIONAL_LEVELS; spot < PLUTO_VSM_SPOT_ROOT_BASE; ++spot)
    {
        if (vsm.origins[spot].w == 0) continue;
        float3 uv = virtualCoordinate(vsm, spot, world);
        if (all(uv >= 0) && all(uv <= 1)) requestFootprint(world, spot);
    }
}
// Only compact request/free lists occupy shared memory. Persistent page metadata
// stays in storage, keeping every pool tier within portable shared-memory limits.
#define VSM_PLANNING_LANES 128
groupshared uint planRequested[PLUTO_VSM_CAPACITY];
groupshared uint planFree[PLUTO_VSM_CAPACITY];
groupshared uint planCounts[PLUTO_VSM_LEVELS];
groupshared uint planQuotas[PLUTO_VSM_LEVELS];
[shader("compute")][numthreads(VSM_PLANNING_LANES, 1, 1)]
void allocateMain(uint lane : SV_GroupIndex)
{
    for (uint slot = lane; slot < vsm.pool.x; slot += VSM_PLANNING_LANES)
    {
        VirtualShadowPage p = pages[slot];
        p.state.w = 0;
        int level = p.address.z;
        int2 local = p.address.xy - vsm.origins[level].xy;
        bool retained = p.state.y != 0 && p.address.w == vsm.origins[level].z &&
            all(local >= 0) && all(local < int(virtualGrid(vsm, level)));
        planRequested[slot] = 0;
        if (retained) planRequested[slot] = requestMask[uint2(local.x, local.y + level * PLUTO_VSM_GRID)] != 0 ? 1u : 0u;
        pages[slot] = p;
    }
    AllMemoryBarrierWithGroupSync();
    if (lane != 0) return;
    uint reserved = 0, fineLevels = 0;
    for (uint level = 0; level < PLUTO_VSM_LEVELS; ++level)
    {
        if (virtualCoarseLevel(level)) reserved += requests[level];
        else if (requests[level] != 0) ++fineLevels;
    }
    uint fineCapacity = vsm.pool.x - min(vsm.pool.x, reserved);
    uint spare = vsm.pool.x;
    for (uint level = 0; level < PLUTO_VSM_LEVELS; ++level)
    {
        planCounts[level] = 0;
        planQuotas[level] = min(requests[level], virtualCoarseLevel(level) ? requests[level] : fineCapacity / max(1u, fineLevels));
        spare -= planQuotas[level];
    }
    for (uint round = 0; round < vsm.pool.x && spare > 0; ++round)
        for (uint level = 0; level < PLUTO_VSM_LEVELS && spare > 0; ++level)
            if (planQuotas[level] < requests[level]) { ++planQuotas[level]; --spare; }
    uint freeCount = 0;
    for (uint slot = 0; slot < vsm.pool.x; ++slot)
    {
        VirtualShadowPage p = pages[slot];
        uint level = p.address.z;
        if (planRequested[slot] != 0 && planCounts[level] < planQuotas[level])
        {
            int2 local = p.address.xy - vsm.origins[level].xy;
            p.state.x = local.y * PLUTO_VSM_GRID + local.x;
            p.state.y = vsm.limits.y;
            p.pending.w = max(p.pending.w, vsm.membershipEpochs[level / 4][level % 4]);
            requestMask[uint2(local.x, local.y + level * PLUTO_VSM_GRID)] = 2;
            ++planCounts[level];
        }
        // Leave unused contents eligible for reuse if they are requested again
        // before this physical slot is reassigned. Its old frame excludes it
        // from this frame's signature/render/publish passes.
        else planFree[freeCount++] = slot;
        pages[slot] = p;
    }
    uint nextFree = 0;
    for (uint level = 0; level < PLUTO_VSM_LEVELS; ++level)
        for (uint index = 0; index < requests[level] && planCounts[level] < planQuotas[level]; ++index)
        {
            uint key = requests[PLUTO_VSM_LEVELS + level * PLUTO_VSM_LEVEL_PAGES + index];
            uint2 local = virtualPageCoordinate(key);
            uint2 at = local + uint2(0, level * PLUTO_VSM_GRID);
            if (requestMask[at] != 1) continue;
            uint slot = planFree[nextFree++];
            VirtualShadowPage p = pages[slot];
            if (p.state.y != 0) ++stats[4];
            p.address = int4(vsm.origins[level].xy + int2(local), int(level), vsm.origins[level].z);
            p.state = uint4(key, vsm.limits.y, 0, 0);
            p.signature = 0;
            p.pending.w = vsm.limits.y;
            pages[slot] = p;
            requestMask[at] = 2;
            ++planCounts[level];
        }
    uint localResident = 0;
    for (uint level = 0; level < PLUTO_VSM_LEVELS; ++level)
    {
        stats[0] += requests[level];
        stats[1] += planCounts[level];
        if (level < PLUTO_VSM_FINE_LEVELS) { stats[11] += requests[level]; stats[12] += planCounts[level]; }
        else if (!virtualCoarseLevel(level)) { stats[14] += requests[level]; localResident += planCounts[level]; }
    }
    stats[5] = stats[0] - stats[1];
    stats[13] = fineCapacity - localResident;
    stats[15] = reserved;
}
bool intersects(VirtualShadowCaster caster, VirtualShadowPage page)
{
    if (caster.bounds.w < 0) return true;
    uint level = page.address.z;
    if (level == PLUTO_VSM_ROOT_LEVEL && caster.bounds.w < vsm.metrics[level].x * vsm.culling.x) return false;
    if (level >= PLUTO_VSM_DIRECTIONAL_LEVELS)
    {
        // Test the world sphere against homogeneous page-frustum planes. This
        // stays conservative even when the sphere crosses the light plane.
        float2 lo = float2(virtualPageCoordinate(page.state.x)) / virtualGrid(vsm, level) * 2.0 - 1.0;
        float2 hi = lo + 2.0 / virtualGrid(vsm, level);
        float4 planes[6] = {float4(1,0,0,-lo.x), float4(-1,0,0,hi.x),
            float4(0,1,0,-lo.y), float4(0,-1,0,hi.y), float4(0,0,1,0), float4(0,0,-1,1)};
        for (int i = 0; i < 6; ++i)
        {
            float4 plane = mul(planes[i], vsm.matrices[level]);
            if (dot(plane, float4(caster.bounds.xyz, 1)) < -caster.bounds.w * length(plane.xyz)) return false;
        }
        return true;
    }
    float3 uv = virtualCoordinate(vsm, level, caster.bounds.xyz);
    float2 radius = caster.bounds.w / vsm.metrics[level].w;
    float2 lo = float2(virtualPageCoordinate(page.state.x)) / virtualGrid(vsm, level);
    float2 hi = lo + 1.0 / virtualGrid(vsm, level);
    float depthRadius = caster.bounds.w / vsm.metrics[level].y;
    return all(uv.xy + radius >= lo) && all(uv.xy - radius <= hi) &&
        uv.z + depthRadius >= 0 && uv.z - depthRadius <= 1;
}
uint mixHash(uint hash, uint value) { return (hash ^ value) * 16777619u; }
groupshared uint4 signatureItems[64];
[shader("compute")][numthreads(64, 1, 1)]
void signatureMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    VirtualShadowPage p = pages[group.x];
    if (p.state.y != vsm.limits.y) return;
    uint2 hash = uint2(2166136261u, 3339675911u);
    uint cost = 0;
    for (uint first = 0; first < vsm.limits.x; first += 64)
    {
        uint index = first + lane;
        uint4 item = 0;
        if (index < vsm.limits.x)
        {
            VirtualShadowCaster caster = casters[index];
            uint at = index * vsm.pool.x + group.x;
            uint4 cached = membership[at];
            if (cached.x != caster.identity.z || cached.y != p.pending.w)
            {
                cached = uint4(caster.identity.z, p.pending.w, intersects(caster, p) ? 1u : 0u, 0);
                membership[at] = cached;
            }
            item = uint4(caster.identity.xy, (caster.draw.x / 3) * caster.draw.z, cached.z);
        }
        signatureItems[lane] = item;
        GroupMemoryBarrierWithGroupSync();
        // Retain the exact ordered hash and saturating cost; parallelising the
        // intersection/cache loads must not change cache identity or budgets.
        if (lane == 0) for (uint itemIndex = 0; itemIndex < min(64u, vsm.limits.x - first); ++itemIndex)
        {
            uint4 next = signatureItems[itemIndex];
            if (next.w == 0) continue;
            hash.x = mixHash(mixHash(hash.x, next.x), next.y);
            hash.y = mixHash(mixHash(hash.y, next.y), next.x);
            cost += min(next.z, 0xffffffffu - cost);
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0)
    {
        p.pending = uint4(hash, cost, p.pending.w);
        pages[group.x] = p;
    }
}
// Sort compact candidates cooperatively: overdue pages first, then new coarse
// coverage, dirty resident shadows, and new refinement. Age breaks ties, so a
// constantly changing caster does not reset its place in the queue.
groupshared uint4 budgetCandidates[PLUTO_VSM_CAPACITY];
bool precedes(uint4 a, uint4 b)
{
    return a.x != b.x ? a.x > b.x : a.y != b.y ? a.y > b.y : a.w < b.w;
}
[shader("compute")][numthreads(VSM_PLANNING_LANES, 1, 1)]
void budgetMain(uint lane : SV_GroupIndex)
{
    const uint sortCapacity = vsm.pool.x <= 256 ? 256u : 1024u;
    for (uint index = lane; index < sortCapacity; index += VSM_PLANNING_LANES)
    {
        uint4 candidate = 0;
        if (index < vsm.pool.x)
        {
            uint slot = (index + vsm.limits.y * 13) % vsm.pool.x;
            VirtualShadowPage p = pages[slot];
            if (p.state.y == vsm.limits.y)
            {
                if (p.state.z != 0 && all(p.signature.xy == p.pending.xy))
                {
                    InterlockedAdd(stats[3], 1);
                    p.signature.z = 0;
                }
                else
                {
                    if (p.signature.z == 0) p.signature.z = vsm.limits.y;
                    uint age = vsm.limits.y - p.signature.z;
                    uint priority = age >= vsm.scheduling.x ? 4u :
                        (p.state.z == 0 && virtualCoarseLevel(p.address.z) ? 3u : p.state.z != 0 ? 2u : 1u);
                    candidate = uint4(priority, age, p.pending.z, index);
                    InterlockedAdd(stats[2], 1);
                    InterlockedMax(stats[16], age);
                }
                pages[slot] = p;
            }
        }
        budgetCandidates[index] = candidate;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint width = 2; width <= sortCapacity; width *= 2)
        for (uint stride = width / 2; stride > 0; stride /= 2)
        {
            for (uint index = lane; index < sortCapacity; index += VSM_PLANNING_LANES)
            {
                uint other = index ^ stride;
                if (other > index)
                {
                    uint4 a = budgetCandidates[index], b = budgetCandidates[other];
                    bool descending = (index & width) == 0;
                    if (descending ? precedes(b, a) : precedes(a, b))
                    { budgetCandidates[index] = b; budgetCandidates[other] = a; }
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
    if (lane != 0) return;
    uint remaining = vsm.limits.w, updates = 0, triangles = 0;
    for (uint index = 0; index < vsm.pool.x; ++index)
    {
        uint4 candidate = budgetCandidates[index];
        if (candidate.x == 0) break;
        uint slot = (candidate.w + vsm.limits.y * 13) % vsm.pool.x;
        // Oversized work is admitted only as the first update after aging. It
        // consumes this frame's entire triangle allowance; counters show actual cost.
        bool oversized = candidate.z > vsm.limits.w && candidate.y >= vsm.scheduling.x &&
            vsm.scheduling.y != 0 && updates == 0;
        if (updates < vsm.limits.z && (candidate.z <= remaining || oversized))
        {
            pages[slot].state.w = 1;
            remaining = oversized ? 0 : remaining - candidate.z;
            triangles += min(candidate.z, 0xffffffffu - triangles);
            requests[PLUTO_VSM_UPDATE_LIST_OFFSET + updates++] = slot;
            if (oversized) ++stats[17];
        }
        else ++stats[8];
    }
    stats[7] = triangles;
    stats[9] = updates;
}
[shader("compute")][numthreads(64, 1, 1)]
void binMain(uint3 id : SV_DispatchThreadID)
{
    uint index = id.x;
    if (index >= vsm.limits.x) return;
    VirtualShadowCaster caster = casters[index];
    uint count = 0;
    for (uint update = 0; update < stats[9]; ++update)
    {
        uint slot = requests[PLUTO_VSM_UPDATE_LIST_OFFSET + update];
        if (membership[index * vsm.pool.x + slot].z != 0)
            pageLists[index * vsm.pool.x + count++] = slot;
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
    if (id.x >= vsm.pool.x) return;
    VirtualShadowPage p = pages[id.x];
    if (p.state.y != vsm.limits.y) return;
    if (p.state.w != 0) { p.signature = uint4(p.pending.xy, 0, vsm.limits.y); p.state.z = 1; }
    if (p.state.z != 0)
        pageTable[virtualPageCoordinate(p.state.x) + uint2(0, p.address.z * PLUTO_VSM_GRID)] = float(id.x + 1);
    pages[id.x] = p;
}
