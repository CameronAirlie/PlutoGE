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
        const bool root = ((level == PLUTO_VSM_ROOT_LEVEL && vsm.camera.w > 0.5) || vsm.origins[level].w != 0) && all(local < virtualGrid(level));
        requestMask[at] = root ? 1u : 0u;
        pageTable[at] = 0;
        if (root)
            requests[PLUTO_VSM_LEVELS + level * PLUTO_VSM_LEVEL_PAGES + local.y * virtualGrid(level) + local.x] =
                local.y * PLUTO_VSM_GRID + local.x;
    }
    if (index < PLUTO_VSM_LEVELS) requests[index] = ((index == PLUTO_VSM_ROOT_LEVEL && vsm.camera.w > 0.5) || vsm.origins[index].w != 0) ? virtualGrid(index) * virtualGrid(index) : 0;
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
// Direct-mapped workgroup cache: collisions fall back to the global atomic,
// never discard coverage. Entries are immutable after the first insertion.
groupshared uint localRequests[128];
void requestFootprint(float3 world, int level)
{
    float2 uv = virtualCoordinate(vsm, level, world).xy;
    float guard = (clamp(vsm.settings.x, 0.0, 4.0) + 2.0) / virtualResolution(level);
    int2 lo = clamp(int2(floor((uv - guard) * virtualGrid(level))), 0, int(virtualGrid(level)) - 1);
    int2 hi = clamp(int2(floor((uv + guard) * virtualGrid(level))), 0, int(virtualGrid(level)) - 1);
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
}
// Cooperative loads/stores keep serial policy decisions in workgroup memory.
// Page retention, request order, quotas and greedy budgets remain unchanged.
// Keep allocation below 16 KiB of shared storage and use 128-lane groups.
// Signatures/costs remain in persistent memory until the signature pass.
#define VSM_PLANNING_LANES 128
struct AllocationPage { int4 address; uint4 state; uint epoch; };
groupshared AllocationPage allocationPages[PLUTO_VSM_CAPACITY];
groupshared uint planRequested[PLUTO_VSM_CAPACITY];
groupshared uint planFree[PLUTO_VSM_CAPACITY];
groupshared uint planCounts[PLUTO_VSM_LEVELS];
groupshared uint planQuotas[PLUTO_VSM_LEVELS];
groupshared uint planAvailable;
groupshared uint planNextFree;
[shader("compute")][numthreads(VSM_PLANNING_LANES, 1, 1)]
void allocateMain(uint lane : SV_GroupIndex)
{
    for (uint slot = lane; slot < PLUTO_VSM_CAPACITY; slot += VSM_PLANNING_LANES)
    {
        VirtualShadowPage previous = pages[slot];
        AllocationPage candidate;
        candidate.address = previous.address;
        candidate.state = previous.state;
        candidate.epoch = previous.pending.w;
        candidate.state.w = 0;
        int candidateLevel = candidate.address.z;
        int2 candidateLocal = candidate.address.xy - vsm.origins[candidateLevel].xy;
        bool retained = candidate.state.y != 0 && candidate.address.w == vsm.origins[candidateLevel].z &&
            all(candidateLocal >= 0) && all(candidateLocal < int(virtualGrid(candidateLevel)));
        planRequested[slot] = 0;
        if (retained)
            planRequested[slot] = requestMask[uint2(candidateLocal.x, candidateLocal.y + candidateLevel * PLUTO_VSM_GRID)] != 0 ? 1u : 0u;
        allocationPages[slot] = candidate;
    }
    GroupMemoryBarrierWithGroupSync();
    if (lane == 0)
    {
        uint reserved = PLUTO_VSM_ROOT_PAGES;
        for (uint level = PLUTO_VSM_DIRECTIONAL_LEVELS; level < PLUTO_VSM_LEVELS; ++level) reserved += requests[level];
        uint spare = PLUTO_VSM_CAPACITY;
        for (uint level = 0; level < PLUTO_VSM_LEVELS; ++level)
        {
            planCounts[level] = 0;
            const uint share = level == PLUTO_VSM_ROOT_LEVEL ? PLUTO_VSM_ROOT_PAGES :
                level >= PLUTO_VSM_DIRECTIONAL_LEVELS ? requests[level] :
                (PLUTO_VSM_CAPACITY - reserved) / PLUTO_VSM_FINE_LEVELS;
            planQuotas[level] = min(requests[level], share);
            spare -= planQuotas[level];
        }
        for (uint round = 0; round < PLUTO_VSM_CAPACITY && spare > 0; ++round)
            for (uint level = 0; level < PLUTO_VSM_LEVELS && spare > 0; ++level)
                if (planQuotas[level] < requests[level]) { ++planQuotas[level]; --spare; }
        planAvailable = 0;
        planNextFree = 0;
        // Pin still-requested residents before assigning new pages. Atomic request
        // order must never evict useful cached depth from one stationary frame to another.
        for (uint slot = 0; slot < PLUTO_VSM_CAPACITY; ++slot)
        {
            AllocationPage p = allocationPages[slot];
            int level = p.address.z;
            int2 local = p.address.xy - vsm.origins[level].xy;
            if (planRequested[slot] != 0 && planCounts[level] < planQuotas[level])
            {
                p.state.x = local.y * PLUTO_VSM_GRID + local.x;
                p.state.y = vsm.limits.y;
                requestMask[uint2(local.x, local.y + level * PLUTO_VSM_GRID)] = 2;
                ++planCounts[level];
                p.epoch = max(p.epoch, vsm.membershipEpochs[level / 4][level % 4]);
            }
            else planFree[planAvailable++] = slot;
            allocationPages[slot] = p;
        }
    }
    AllMemoryBarrierWithGroupSync();
    for (uint level = 0; level < PLUTO_VSM_LEVELS; ++level)
        for (uint first = 0; first < requests[level] && planNextFree < planAvailable && planCounts[level] < planQuotas[level]; first += PLUTO_VSM_CAPACITY)
        {
            for (uint item = lane; item < PLUTO_VSM_CAPACITY; item += VSM_PLANNING_LANES)
            {
                uint index = first + item;
                uint key = index < requests[level] ? requests[PLUTO_VSM_LEVELS + level * PLUTO_VSM_LEVEL_PAGES + index] : 0;
                uint2 at = virtualPageCoordinate(key) + uint2(0, level * PLUTO_VSM_GRID);
                planRequested[item] = index < requests[level] && requestMask[at] == 1 ? key : 0xffffffffu;
            }
            GroupMemoryBarrierWithGroupSync();
            if (lane == 0) for (uint item = 0; item < PLUTO_VSM_CAPACITY && planCounts[level] < planQuotas[level] && planNextFree < planAvailable; ++item)
            {
                uint key = planRequested[item];
                if (key == 0xffffffffu) continue;
                uint2 local = virtualPageCoordinate(key);
                uint2 at = local + uint2(0, level * PLUTO_VSM_GRID);
                uint slot = planFree[planNextFree++];
                AllocationPage p = allocationPages[slot];
                if (p.state.y != 0) ++stats[4];
                p.address = int4(vsm.origins[level].xy + int2(local), int(level), vsm.origins[level].z);
                p.state = uint4(key, vsm.limits.y, 0, 0);
                p.epoch = vsm.limits.y;
                allocationPages[slot] = p;
                requestMask[at] = 2;
                ++planCounts[level];
            }
            AllMemoryBarrierWithGroupSync();
        }
    for (uint slot = lane; slot < PLUTO_VSM_CAPACITY; slot += VSM_PLANNING_LANES)
    {
        VirtualShadowPage p = pages[slot];
        p.address = allocationPages[slot].address;
        p.state = allocationPages[slot].state;
        p.pending.w = allocationPages[slot].epoch;
        pages[slot] = p;
    }
    if (lane == 0)
    {
        for (uint level = 0; level < PLUTO_VSM_LEVELS; ++level) stats[0] += requests[level];
        stats[1] = PLUTO_VSM_CAPACITY - planAvailable + planNextFree;
        stats[5] = stats[0] - stats[1];
    }
}
bool intersects(VirtualShadowCaster caster, VirtualShadowPage page)
{
    if (caster.bounds.w < 0) return true;
    uint level = page.address.z;
    if (level >= PLUTO_VSM_DIRECTIONAL_LEVELS)
    {
        // Test the world sphere against homogeneous page-frustum planes. This
        // stays conservative even when the sphere crosses the light plane.
        float2 lo = float2(virtualPageCoordinate(page.state.x)) / virtualGrid(level) * 2.0 - 1.0;
        float2 hi = lo + 2.0 / virtualGrid(level);
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
    float2 lo = float2(virtualPageCoordinate(page.state.x)) / virtualGrid(level);
    float2 hi = lo + 1.0 / virtualGrid(level);
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
            uint at = index * PLUTO_VSM_CAPACITY + group.x;
            uint4 cached = membership[at];
            if (cached.x != caster.identity.z || cached.y != p.pending.w)
            {
                cached = uint4(caster.identity.z, p.pending.w, intersects(caster, p) ? 1u : 0u, 0);
                membership[at] = cached;
            }
            item = uint4(caster.identity.xy, min(vsm.limits.w + 1, min(vsm.limits.w + 1, caster.draw.x / 3) * caster.draw.z), cached.z);
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
            cost = min(vsm.limits.w + 1, cost + next.z);
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0)
    {
        p.pending = uint4(hash, cost, p.pending.w);
        pages[group.x] = p;
    }
}
groupshared VirtualShadowPage planPages[PLUTO_VSM_CAPACITY];
[shader("compute")][numthreads(VSM_PLANNING_LANES, 1, 1)]
void budgetMain(uint lane : SV_GroupIndex)
{
    for (uint slot = lane; slot < PLUTO_VSM_CAPACITY; slot += VSM_PLANNING_LANES)
        planPages[slot] = pages[slot];
    GroupMemoryBarrierWithGroupSync();
    if (lane == 0)
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
                VirtualShadowPage p = planPages[slot];
                if (p.state.y != vsm.limits.y) continue;
                if (p.state.z != 0 && all(p.signature.xy == p.pending.xy))
                {
                    if (priority == 2) ++stats[3];
                    continue;
                }
                const uint pagePriority = p.state.z != 0 ? 2u :
                    (p.address.z >= PLUTO_VSM_ROOT_LEVEL ? 0u : 1u);
                if (pagePriority != priority) continue;
                ++stats[2];
                if (updates < vsm.limits.z && p.pending.z <= remaining)
                {
                    p.state.w = 1;
                    remaining -= p.pending.z;
                    requests[PLUTO_VSM_UPDATE_LIST_OFFSET + updates++] = slot;
                }
                else ++stats[8];
                planPages[slot] = p;
            }
        stats[7] = vsm.limits.w - remaining;
        stats[9] = updates;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint slot = lane; slot < PLUTO_VSM_CAPACITY; slot += VSM_PLANNING_LANES)
        pages[slot] = planPages[slot];
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
        if (membership[index * PLUTO_VSM_CAPACITY + slot].z != 0)
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
