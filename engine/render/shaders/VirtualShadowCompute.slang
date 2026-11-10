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
// Caster/page membership records (membershipRecord): the frame the record was evaluated, and the mask of the caster's
// models reaching the page. A record stays valid until its caster's generation (VirtualShadowCaster::identity.z) or its
// page's membership epoch (VirtualShadowPage::pending.w) moves past that frame; both are frames, set when they change.
[[vk::binding(10, 0)]] RWStructuredBuffer<uint2> membership : register(u10, space0);
// Masked chunks' per-model world spheres, PLUTO_VSM_MASKED_INSTANCES per caster; negative radius = unknown.
[[vk::binding(11, 0)]] StructuredBuffer<float4> instanceBounds : register(t11, space0);

// Diagnostics must not wrap when very expensive pages share a level.
void addDirtyTriangleCost(uint level, uint cost)
{
    uint index = PLUTO_VSM_STATS_DIRTY_TRIANGLES_BASE + level;
    uint previous = stats[index];
    for (;;)
    {
        uint next = previous + min(cost, 0xffffffffu - previous);
        uint observed;
        InterlockedCompareExchange(stats[index], previous, next, observed);
        if (observed == previous) break;
        previous = observed;
    }
}

[shader("compute")][numthreads(64, 1, 1)]
void resetMain(uint3 id : SV_DispatchThreadID)
{
    uint index = id.x;
    if (index < PLUTO_VSM_LEVELS * PLUTO_VSM_LEVEL_PAGES)
    {
        uint2 at = virtualPageCoordinate(index);
        const uint level = index / PLUTO_VSM_LEVEL_PAGES;
        const uint2 local = virtualPageCoordinate(index % PLUTO_VSM_LEVEL_PAGES);
        const bool root = ((level == virtualRootLevel(vsm) && vsm.camera.w > 0.5) || (level >= PLUTO_VSM_SPOT_ROOT_BASE && vsm.origins[level].w != 0)) && all(local < virtualGrid(vsm, level));
        requestMask[at] = root ? 1u : 0u;
        pageTable[at] = 0;
        if (root)
            requests[PLUTO_VSM_LEVELS + level * PLUTO_VSM_LEVEL_PAGES + local.y * virtualGrid(vsm, level) + local.x] =
                local.y * PLUTO_VSM_GRID + local.x;
    }
    if (index < PLUTO_VSM_LEVELS) requests[index] = ((index == virtualRootLevel(vsm) && vsm.camera.w > 0.5) || (index >= PLUTO_VSM_SPOT_ROOT_BASE && vsm.origins[index].w != 0)) ? virtualGrid(vsm, index) * virtualGrid(vsm, index) : 0;
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
    if (vsm.camera.w > 0.5 && level >= 0)
    {
        requestFootprint(world, level);
        if (virtualLevelBlend(vsm, level, world, footprint) > 0.0)
            requestFootprint(world, level + 1);
    }
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
// A deterministic workgroup scan preserves request order and physical-slot
// preference without serial storage-buffer reads/writes on lane zero.
groupshared uint planScan[VSM_PLANNING_LANES];
uint inclusivePlanScan(uint value, uint lane)
{
    planScan[lane] = value;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 1; stride < VSM_PLANNING_LANES; stride *= 2)
    {
        uint previous = lane >= stride ? planScan[lane - stride] : 0;
        GroupMemoryBarrierWithGroupSync();
        planScan[lane] += previous;
        GroupMemoryBarrierWithGroupSync();
    }
    return planScan[lane];
}
// Whether a physical page still holds depth for its virtual page: drawn under the level's current projection epoch,
// and inside the level's current clipmap.
bool pageRetained(VirtualShadowPage p)
{
    int level = p.address.z;
    int2 local = p.address.xy - vsm.origins[level].xy;
    return p.state.y != 0 && p.address.w == vsm.origins[level].z && all(local >= 0) && all(local < int(virtualGrid(vsm, level)));
}
// Frames a page may go unrequested before it is preferred for reuse over recently requested pages.
#define VSM_STALE_PAGE_FRAMES 32u
// The order free slots are reused in (lowest first): those holding no usable depth, pages unrequested for at least
// VSM_STALE_PAGE_FRAMES, then the rest, which the request set is most likely to want back.
uint freeSlotPriority(VirtualShadowPage p)
{
    if (!pageRetained(p)) return 0u;
    return vsm.limits.y - p.state.y >= VSM_STALE_PAGE_FRAMES ? 1u : 2u;
}
[shader("compute")][numthreads(VSM_PLANNING_LANES, 1, 1)]
void allocateMain(uint lane : SV_GroupIndex)
{
    for (uint slot = lane; slot < vsm.pool.x; slot += VSM_PLANNING_LANES)
    {
        VirtualShadowPage p = pages[slot];
        p.state.w = 0;
        int level = p.address.z;
        int2 local = p.address.xy - vsm.origins[level].xy;
        bool retained = pageRetained(p);
        planRequested[slot] = 0;
        if (retained && requestMask[uint2(local.x, local.y + level * PLUTO_VSM_GRID)] != 0)
            planRequested[slot] = uint(level) + 1;
        planFree[slot] = 0;
        pages[slot] = p;
    }
    if (lane == 0)
    {
        uint requested[PLUTO_VSM_LEVELS];
        uint reserved = 0, fineLevels = 0;
        for (uint level = 0; level < PLUTO_VSM_LEVELS; ++level)
        {
            requested[level] = requests[level];
            if (virtualCoarseLevel(vsm, level)) reserved += requested[level];
            else if (requested[level] != 0) ++fineLevels;
        }
        uint fineCapacity = vsm.pool.x - min(vsm.pool.x, reserved);
        uint spare = vsm.pool.x;
        for (uint level = 0; level < PLUTO_VSM_LEVELS; ++level)
        {
            planQuotas[level] = min(requested[level], virtualCoarseLevel(vsm, level) ? requested[level] : fineCapacity / max(1u, fineLevels));
            spare -= planQuotas[level];
        }
        // Round-robin the spare pages; stop once a round grants nothing
        // (every level is satisfied) instead of spinning through the pool.
        for (uint round = 0; round < vsm.pool.x && spare > 0; ++round)
        {
            bool granted = false;
            for (uint level = 0; level < PLUTO_VSM_LEVELS && spare > 0; ++level)
                if (planQuotas[level] < requested[level]) { ++planQuotas[level]; --spare; granted = true; }
            if (!granted) break;
        }
        stats[13] = fineCapacity;
        stats[15] = reserved;
    }
    AllMemoryBarrierWithGroupSync();
    // Independent level ranks keep the same retained-slot preference as the
    // reference allocator. Only shared compact metadata is scanned here.
    for (uint level = lane; level < PLUTO_VSM_LEVELS; level += VSM_PLANNING_LANES)
    {
        uint count = 0;
        for (uint slot = 0; slot < vsm.pool.x; ++slot)
            if (planRequested[slot] == level + 1 && count < planQuotas[level])
            {
                planFree[slot] = 1;
                ++count;
            }
        planCounts[level] = count;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint slot = lane; slot < vsm.pool.x; slot += VSM_PLANNING_LANES)
    {
        VirtualShadowPage p = pages[slot];
        uint level = p.address.z;
        if (planFree[slot] != 0)
        {
            int2 local = p.address.xy - vsm.origins[level].xy;
            p.state.x = local.y * PLUTO_VSM_GRID + local.x;
            p.state.y = vsm.limits.y;
            p.pending.w = max(p.pending.w, vsm.membershipEpochs[level / 4][level % 4]);
            requestMask[uint2(local.x, local.y + level * PLUTO_VSM_GRID)] = 2;
        }
        else p.freshness.x = 0;
        pages[slot] = p;
    }
    AllMemoryBarrierWithGroupSync();
    // Reuse the request scratch as an ordered free-slot list after all readers
    // of its old contents have completed. Slots whose depth is no use go first, then pages unrequested for longest:
    // a page leaves the request set for a frame whenever jitter moves receivers across a level boundary, and taking
    // free slots in slot order overwrote such pages ahead of empty slots, to render them again when they returned.
    uint freeCount = 0;
    for (uint priority = 0; priority < 3; ++priority)
        for (uint first = 0; first < vsm.pool.x; first += VSM_PLANNING_LANES)
        {
            uint slot = first + lane;
            uint freeSlot = slot < vsm.pool.x && planFree[slot] == 0 && freeSlotPriority(pages[slot]) == priority ? 1u : 0u;
            uint rank = inclusivePlanScan(freeSlot, lane);
            if (freeSlot != 0) planRequested[freeCount + rank - 1] = slot;
            freeCount += planScan[VSM_PLANNING_LANES - 1];
            GroupMemoryBarrierWithGroupSync();
        }
    uint nextFree = 0;
    for (uint level = 0; level < PLUTO_VSM_LEVELS; ++level)
    {
        uint count = planCounts[level];
        for (uint first = 0; first < requests[level] && count < planQuotas[level]; first += VSM_PLANNING_LANES)
        {
            uint index = first + lane;
            uint key = 0;
            uint needed = 0;
            if (index < requests[level])
            {
                key = requests[PLUTO_VSM_LEVELS + level * PLUTO_VSM_LEVEL_PAGES + index];
                uint2 at = virtualPageCoordinate(key) + uint2(0, level * PLUTO_VSM_GRID);
                needed = requestMask[at] == 1 ? 1u : 0u;
            }
            uint rank = inclusivePlanScan(needed, lane);
            uint assigned = min(planScan[VSM_PLANNING_LANES - 1], planQuotas[level] - count);
            if (needed != 0 && rank <= assigned)
            {
                uint slot = planRequested[nextFree + rank - 1];
                VirtualShadowPage p = pages[slot];
                if (p.state.y != 0) InterlockedAdd(stats[4], 1u);
                p.address = int4(vsm.origins[level].xy + int2(virtualPageCoordinate(key)), int(level), vsm.origins[level].z);
                p.state = uint4(key, vsm.limits.y, 0, 0);
                p.signature = 0;
                p.freshness = 0;
                p.pending.w = vsm.limits.y;
                pages[slot] = p;
                requestMask[virtualPageCoordinate(key) + uint2(0, level * PLUTO_VSM_GRID)] = 2;
            }
            count += assigned;
            nextFree += assigned;
            AllMemoryBarrierWithGroupSync();
        }
        if (lane == 0) planCounts[level] = count;
    }
    GroupMemoryBarrierWithGroupSync();
    if (lane == 0)
    {
        uint localResident = 0;
        for (uint level = 0; level < PLUTO_VSM_LEVELS; ++level)
        {
            stats[0] += requests[level];
            stats[1] += planCounts[level];
            if (level < virtualRootLevel(vsm)) { stats[11] += requests[level]; stats[12] += planCounts[level]; }
            else if (!virtualCoarseLevel(vsm, level)) { stats[14] += requests[level]; localResident += planCounts[level]; }
        }
        stats[5] = stats[0] - stats[1];
        stats[13] -= localResident;
    }
}

// A page's six bounding planes in world space, and the root level's smallest caster: what intersects tests every caster
// against. A page's planes are the same for every caster, so planning builds them once per page (PageBounds) rather
// than transforming six planes by the level's matrix for every caster and model it tests.
struct PageBounds
{
    float4 planes[6];
    float minimumRadius; // casters smaller than this skip the page (the root level culls tiny casters); else -1
};
PageBounds pageBounds(VirtualShadowPage page)
{
    PageBounds bounds;
    const uint level = page.address.z;
    // Homogeneous page planes support both orthographic and perspective
    // projections. The AABB support function is tighter than a sphere for
    // architectural surfaces, while remaining conservative under clipping.
    const float2 lo = float2(virtualPageCoordinate(page.state.x)) / virtualGrid(vsm, level) * 2.0 - 1.0;
    const float2 hi = lo + 2.0 / virtualGrid(vsm, level);
    const float4 clipPlanes[6] = {float4(1,0,0,-lo.x), float4(-1,0,0,hi.x),
        float4(0,1,0,-lo.y), float4(0,-1,0,hi.y), float4(0,0,1,0), float4(0,0,-1,1)};
    for (int i = 0; i < 6; ++i)
        bounds.planes[i] = mul(clipPlanes[i], vsm.matrices[level]);
    bounds.minimumRadius = level == virtualRootLevel(vsm) ? vsm.metrics[level].x * vsm.culling.x : -1.0;
    return bounds;
}
// Conservative bounds/page test. `extents` is a world AABB around sphere.xyz, or negative to test the sphere alone.
bool intersects(float4 sphere, float4 extents, PageBounds page)
{
    if (sphere.w < 0) return true;
    if (sphere.w < page.minimumRadius) return false;
    for (int i = 0; i < 6; ++i)
    {
        const float4 plane = page.planes[i];
        float radius = extents.x >= 0 ? dot(abs(plane.xyz), extents.xyz) : sphere.w * length(plane.xyz);
        float distance = dot(plane, float4(sphere.xyz, 1));
        float tolerance = 1.e-5 * (1.0 + abs(distance) + radius);
        if (distance < -radius - tolerance) return false;
    }
    return true;
}
bool intersects(float4 sphere, float4 extents, VirtualShadowPage page)
{
    return intersects(sphere, extents, pageBounds(page));
}
// Whether a caster chunk can draw into the page at all: the page's level is one the chunk serves (a LOD variant serves
// only the levels whose texels its detail suits), and the chunk's whole bounds reach it.
bool casterReaches(VirtualShadowCaster caster, VirtualShadowPage page, PageBounds bounds)
{
    return virtualCasterServesLevel(caster, page.address.z) && intersects(caster.bounds, caster.extents, bounds);
}
// A caster/page pair's membership record. Records are page-major, so the consecutive casters a wave evaluates for one
// page (signatureMain), and for each updated page (binMain), read one contiguous block rather than a record per cache
// line: planning reads a record for every pair of every requested page, which made it bandwidth-bound.
uint membershipRecord(uint caster, uint slot) { return slot * virtualMembershipStride(vsm) + caster; }
// Which of a caster's models reach the page: a bit per chunk-local model for masked chunks of several models, otherwise
// 1 when the chunk does (its page entry then draws every model). Zero when none does.
uint intersectionMask(uint index, VirtualShadowCaster caster, VirtualShadowPage page, PageBounds bounds)
{
    if (!casterReaches(caster, page, bounds)) return 0;
    if (caster.draw.z <= 1 || !virtualMaskedCaster(caster)) return 1;
    uint mask = 0;
    for (uint model = 0; model < caster.draw.z; ++model)
        if (intersects(instanceBounds[index * PLUTO_VSM_MASKED_INSTANCES + model], float4(-1, -1, -1, 0), bounds))
            mask |= 1u << model;
    return mask;
}
// Whether a page animates swaying casters: within the shadow sway distance of the camera, so the pages redrawn every
// frame are bounded by the distance rather than by how far a tree's crown reaches. Coarse levels are low-resolution
// fallbacks, whose large, expensive pages always hold the rest pose.
bool pageSways(VirtualShadowPage page)
{
    if (virtualCoarseLevel(vsm, page.address.z)) return false;
    const float distance = vsm.culling.y;
    return distance <= 0 || intersects(float4(vsm.camera.xyz, distance), float4(-1, -1, -1, 0), page);
}
// The models of `mask` that cast their swaying pose on this page.
uint swayingMask(VirtualShadowCaster caster, VirtualShadowPage page, uint mask)
{
    return caster.still.z != 0 && pageSways(page) ? mask : 0;
}
// A strong 32-bit mix (murmur3's finaliser), for the order-independent page signatures below.
uint finalMix(uint value)
{
    value ^= value >> 16; value *= 0x85ebca6bu;
    value ^= value >> 13; value *= 0xc2b2ae35u;
    return value ^ (value >> 16);
}
// Adds `value` to the 64-bit sum (lo, hi), so a sum of 32-bit costs saturates exactly at the end (signatureCost).
void addWide(inout uint2 sum, uint value)
{
    const uint previous = sum.x;
    sum.x += value;
    if (sum.x < previous) ++sum.y;
}
uint signatureCost(uint2 sum) { return sum.y != 0 ? 0xffffffffu : sum.x; }

// A page's signature is the set of (caster, models, pose) items that reach it, each hashed on its own and summed, with
// the static casters' items summed apart; costs are summed exactly and saturate. Every lane evaluates its own casters
// and the group adds the sums once, so planning a page takes no serial step per caster: the previous ordered hash ran
// on one lane, item by item, behind two barriers per block of 64 casters, and dominated planning.
#define VSM_SIGNATURE_LANES 256
groupshared uint signatureSums[12]; // hash lo/hi, static hash lo/hi, static cost lo/hi, dynamic cost lo/hi
[shader("compute")][numthreads(VSM_SIGNATURE_LANES, 1, 1)]
void signatureMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    VirtualShadowPage p = pages[group.x];
    if (p.state.y != vsm.limits.y) return;
    if (lane < 12) signatureSums[lane] = 0;
    GroupMemoryBarrierWithGroupSync();
    const PageBounds bounds = pageBounds(p);
    const bool swaying = pageSways(p);
    uint2 hash = 0, staticHash = 0, staticCost = 0, dynamicCost = 0;
    for (uint index = lane; index < vsm.limits.x; index += VSM_SIGNATURE_LANES)
    {
        // Most casters miss most pages, and a valid cached record says so without the rest of the caster: read its
        // identity (levels served, membership generation) first, and the whole record only when the membership must
        // be evaluated or the caster reaches the page.
        const uint4 casterIdentity = casters[index].identity;
        // A chunk serving other levels (a LOD variant) never reaches the page, and its membership record for the page
        // is neither read nor written: binMain applies the same test before reading one.
        if (((casterIdentity.w >> p.address.z) & 1u) == 0) continue;
        const uint at = membershipRecord(index, group.x);
        uint2 cached = membership[at];
        const bool stale = cached.x == 0 || cached.x < casterIdentity.z || cached.x < p.pending.w;
        if (!stale && cached.y == 0) continue;
        const VirtualShadowCaster caster = casters[index];
        if (stale)
        {
            cached = uint2(vsm.limits.y, intersectionMask(index, caster, p, bounds));
            membership[at] = cached;
        }
        const uint mask = cached.y;
        if (mask == 0) continue;
        // The page's content is the rest pose unless it sways; the masks distinguish which models it holds.
        const uint sways = caster.still.z != 0 && swaying ? mask : 0u; // swayingMask, with the page's test hoisted
        const uint2 identity = sways != 0 ? caster.identity.xy : caster.still.xy;
        const uint models = virtualMaskedCaster(caster) && caster.draw.z > 1 ? countbits(mask) : caster.draw.z;
        // Only content takes part, not the chunk's index: chunks shift as casters enter and leave the list, and a page
        // whose casters did not change must keep its signature (and its cached depth).
        const uint2 item = uint2(finalMix(identity.x ^ finalMix(mask + 0x7f4a7c15u)),
                                 finalMix(identity.y ^ finalMix(sways + 0x9e3779b9u)));
        hash += item;
        const uint cost = (caster.draw.x / 3) * models;
        if (caster.still.w != 0)
            addWide(dynamicCost, cost);
        else
        {
            staticHash += item;
            addWide(staticCost, cost);
        }
    }
    InterlockedAdd(signatureSums[0], hash.x);
    InterlockedAdd(signatureSums[1], hash.y);
    InterlockedAdd(signatureSums[2], staticHash.x);
    InterlockedAdd(signatureSums[3], staticHash.y);
    // Wide sums: add the low words, carrying into the high words.
    uint previous;
    InterlockedAdd(signatureSums[4], staticCost.x, previous);
    InterlockedAdd(signatureSums[5], staticCost.y + (previous + staticCost.x < previous ? 1u : 0u));
    InterlockedAdd(signatureSums[6], dynamicCost.x, previous);
    InterlockedAdd(signatureSums[7], dynamicCost.y + (previous + dynamicCost.x < previous ? 1u : 0u));
    GroupMemoryBarrierWithGroupSync();
    if (lane == 0)
    {
        // Seeded so a page reaching no caster keeps the empty signature it always had.
        const uint2 pageHash = uint2(signatureSums[0], signatureSums[1]) + uint2(2166136261u, 3339675911u);
        const uint2 pageStaticHash = uint2(signatureSums[2], signatureSums[3]) + uint2(2166136261u, 3339675911u);
        const uint staticTotal = signatureCost(uint2(signatureSums[4], signatureSums[5]));
        const uint dynamicTotal = signatureCost(uint2(signatureSums[6], signatureSums[7]));
        // An update redraws the static layer when its casters changed, the page has no depth yet, or the layer was
        // (re)created since; otherwise it restores the layer and costs only the dynamic casters.
        const bool redrawStatic = !virtualStaticLayer(vsm) || p.state.z == 0 || p.layer.z != vsm.levels.y ||
            any(p.layer.xy != pageStaticHash);
        const uint cost = redrawStatic ? staticTotal + min(dynamicTotal, 0xffffffffu - staticTotal) : dynamicTotal;
        p.pending = uint4(pageHash, cost, p.pending.w);
        p.freshness.yzw = uint3(pageStaticHash, redrawStatic ? 1u : 0u);
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
                    p.freshness.x = 0;
                }
                else
                {
                    if (p.signature.z == 0) p.signature.z = vsm.limits.y;
                    if (p.freshness.x == 0) p.freshness.x = vsm.limits.y;
                    uint age = vsm.limits.y - p.freshness.x;
                    InterlockedMax(stats[PLUTO_VSM_STATS_HISTORICAL_AGE], vsm.limits.y - p.signature.z);
                    addDirtyTriangleCost(p.address.z, p.pending.z);
                    InterlockedMax(stats[PLUTO_VSM_STATS_MAX_DIRTY_TRIANGLES], p.pending.z);
                    uint priority = age >= vsm.scheduling.x ? 4u :
                        (p.state.z == 0 && virtualCoarseLevel(vsm, p.address.z) ? 3u : p.state.z != 0 ? 2u : 1u);
                    candidate = uint4(priority, age, p.pending.z, index);
                    InterlockedAdd(stats[2], 1);
                    InterlockedAdd(stats[p.state.z != 0 ? PLUTO_VSM_STATS_CHANGED_DIRTY : PLUTO_VSM_STATS_NEW_DIRTY], 1);
                    if (p.freshness.w != 0) InterlockedAdd(stats[PLUTO_VSM_STATS_STATIC_DIRTY], 1);
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
            stats[PLUTO_VSM_STATS_UPDATED_TRIANGLES_BASE + pages[slot].address.z] += candidate.z;
            requests[PLUTO_VSM_UPDATE_LIST_OFFSET + updates++] = slot;
            if (oversized) ++stats[17];
        }
        else
        {
            ++stats[8];
            if (updates >= vsm.limits.z) ++stats[PLUTO_VSM_STATS_PAGE_DEFERRED];
            else ++stats[PLUTO_VSM_STATS_TRIANGLE_DEFERRED];
        }
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
    const bool masked = virtualMaskedCaster(caster);
    // With a static layer, static casters draw into it only where it is redrawn; dynamic casters draw every update.
    const bool layered = virtualStaticLayer(vsm) && caster.still.w == 0;
    // Updates never exceed the page budget (limits.z), and a masked chunk lists each page at most once per model.
    const uint list = index * virtualListStride(vsm.limits);
    uint count = 0;
    for (uint update = 0; update < stats[9]; ++update)
    {
        uint slot = requests[PLUTO_VSM_UPDATE_LIST_OFFSET + update];
        if (layered && pages[slot].freshness.w == 0) continue;
        // Planning skipped (and so never validated) the records of levels the chunk does not serve.
        if (!virtualCasterServesLevel(caster, pages[slot].address.z)) continue;
        uint mask = membership[membershipRecord(index, slot)].y;
        if (mask == 0) continue;
        const uint sways = swayingMask(caster, pages[slot], mask);
        if (!masked) pageLists[list + count++] = virtualPageEntry(slot, 0, sways != 0);
        else for (; mask != 0; mask &= mask - 1)
        {
            const uint model = firstbitlow(mask);
            pageLists[list + count++] = virtualPageEntry(slot, model, ((sways >> model) & 1u) != 0);
        }
    }
    uint command = index * 5;
    indirectCommands[command] = caster.draw.x;
    // Masked entries name their model; other entries draw every model of the chunk.
    indirectCommands[command + 1] = masked ? count : count * caster.draw.z;
    indirectCommands[command + 2] = caster.draw.y;
    indirectCommands[command + 3] = 0;
    indirectCommands[command + 4] = vsm.scheduling.z != 0 ? index * PLUTO_VSM_INSTANCE_STRIDE : 0;
    InterlockedAdd(stats[6], count);
    if (count != 0) InterlockedAdd(stats[10], 1u);
}
[shader("compute")][numthreads(64, 1, 1)]
void publishMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= vsm.pool.x) return;
    VirtualShadowPage p = pages[id.x];
    if (p.state.y != vsm.limits.y) return;
    if (p.state.w != 0)
    {
        p.signature = uint4(p.pending.xy, 0, vsm.limits.y); p.state.z = 1; p.freshness.x = 0;
        if (virtualStaticLayer(vsm) && p.freshness.w != 0) p.layer = uint4(p.freshness.yz, vsm.levels.y, 0);
    }
    if (p.state.z != 0)
        pageTable[virtualPageCoordinate(p.state.x) + uint2(0, p.address.z * PLUTO_VSM_GRID)] = float(id.x + 1);
    pages[id.x] = p;
}
