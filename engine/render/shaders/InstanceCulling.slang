// Per-instance culling and LOD binning of instanced draws on the GPU (GpuInstanceCulling.h), the compute counterpart
// of the CPU binning in BasicRenderer::Render. LOD selection and crossfades follow BasicInstanceLodChain::SelectBlend
// over the switches the CPU uploads, so an instance near a switch bins into both LODs with complementary dither codes.
// Three passes per phase:
//   countMain   per instance: cull (side planes, draw distance, a depth hierarchy), pick the LODs, count them into
//               their slots (one per draw and LOD);
//   scanMain    one group: prefix sums of the slot counts give each bin's offset, written to the bin buffer's header
//               and into each slot's indirect draw arguments (the CPU uploads their index ranges);
//   scatterMain per instance again: the same choice, then its record index (with its dither code) goes into its bins.
// Two-phase occlusion: phase 0 tests the previous frame's hierarchy and marks the instances it rejected only for
// occlusion; after the phase-0 instances' depth is drawn, phase 1 re-tests just those against a hierarchy of that
// depth, into slots and bins of its own. What phase 0 hid wrongly is drawn in the same frame, and both phases' bins
// together are exactly the unoccluded instances.
// count and scatter run once per instance record page, which the dispatch binds as `records`.

static const uint MaxLods = 8;
static const uint MaxSwitches = MaxLods + 1;
static const float TransitionWidth = 0.15;   // BasicInstanceLodChain::TransitionWidth
static const uint DitherSteps = 127u;        // BasicInstanceLodChain::DitherSteps
static const uint DitherCoarser = 0x80u;     // BasicInstanceLodChain::DitherCoarser
static const uint DitherShift = 24u;         // BasicInstanceLodChain::DitherShift

[[vk::binding(0, 0)]] cbuffer CullParameters : register(b0)
{
    float4 cullPlanes[4];                       // left, right, bottom, top: normal xyz, distance w
    float4 cameraLod;                           // camera position, pixels per unit radius over distance
    column_major float4x4 hierarchyViewProjection; // the view the hierarchy's depth was drawn from
    uint4 hierarchySource;                      // depth width, height, level count, flags
    uint4 cullDispatch;                         // first draw, draw count, first item, item end
    uint4 cullTotals;                           // slots per phase, first instance in arguments (1) or bin header (0),
                                                // phase, first bin entry of this phase
    uint4 hierarchyLevels[16];                  // width, height, offset in `hierarchy`
};

// The instance records' matrices as columns: independent of how either backend lays out a matrix.
struct CullRecord
{
    float4 modelColumns[4];
    float4 previousColumns[4];
};
struct CullDraw
{
    uint4 items;            // first item, item count, first record, first slot (in phase 0)
    uint4 lodInfo;          // LOD count, finest LOD drawn, switch count
    float4 bounds;          // model-space sphere: centre, radius
    float4 screenRange;     // wind reach (a share of the rest radius), unused, draw distance, LOD distance scale
    float4 switches[3];     // BasicInstanceLodChain::BlendSwitches radii
    uint4 coarser[3];       // and the coarsest level each switch leads to
};
struct CullIndex
{
    uint record;
};

[[vk::binding(1, 0)]] StructuredBuffer<CullRecord> records : register(t1);
[[vk::binding(2, 0)]] StructuredBuffer<CullDraw> cullDraws : register(t2);
// Every slot's count (both phases), then every slot's scatter cursor.
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> slotCounters : register(u3);
// The bins: one header entry per slot (its bin's offset), then the bins' entries: a record index with its dither code
// in the top byte (BasicLit.slang reads both).
[[vk::binding(4, 0)]] RWStructuredBuffer<CullIndex> binIndices : register(u4);
// Five words per slot: index count, instance count, first index, vertex offset, first instance. The CPU writes the
// index range; the scan fills in the instance count and first instance.
[[vk::binding(5, 0)]] RWStructuredBuffer<uint> slotArguments : register(u5);
[[vk::binding(6, 0)]] StructuredBuffer<float> hierarchy : register(t6);
// Per item: 1 when phase 0 rejected it only for occlusion, so phase 1 tests it again.
[[vk::binding(7, 0)]] RWStructuredBuffer<uint> occludedItems : register(u7);

float switchRadius(CullDraw draw, uint k) { return draw.switches[k / 4][k % 4]; }
uint switchCoarser(CullDraw draw, uint k) { return draw.coarser[k / 4][k % 4]; }

// The draw holding an item: draws are ordered by first item.
uint findDraw(uint item)
{
    uint low = cullDispatch.x, high = cullDispatch.x + cullDispatch.y - 1;
    while (low < high)
    {
        const uint middle = (low + high + 1) / 2;
        if (cullDraws[middle].items.x <= item) low = middle; else high = middle - 1;
    }
    return low;
}

// Whether the sphere lies behind the hierarchy's depth: reverse Z, so the hierarchy keeps the farthest depth.
// Anything that projected outside that view, crossed its near plane or is not finite counts as visible.
bool occluded(float3 center, float radius)
{
    if ((hierarchySource.w & 4u) == 0u || hierarchySource.z == 0u) return false;
    const bool zeroToOne = (hierarchySource.w & 1u) != 0u;
    float2 low = float2(1.0), high = float2(-1.0);
    float nearest = 0.0;
    for (uint corner = 0; corner < 8; ++corner)
    {
        const float3 offset = float3((corner & 1u) != 0u ? 1.0 : -1.0, (corner & 2u) != 0u ? 1.0 : -1.0,
                                     (corner & 4u) != 0u ? 1.0 : -1.0) * radius;
        const float4 clip = mul(hierarchyViewProjection, float4(center + offset, 1.0));
        if (!all(isfinite(clip)) || clip.w <= 0.00001 || clip.z >= clip.w || clip.z < (zeroToOne ? 0.0 : -clip.w))
            return false;
        float3 ndc = clip.xyz / clip.w;
        if ((hierarchySource.w & 2u) != 0u) ndc.y = -ndc.y;
        low = min(low, ndc.xy);
        high = max(high, ndc.xy);
        nearest = max(nearest, zeroToOne ? ndc.z : ndc.z * 0.5 + 0.5);
    }
    if (any(low < -1.0) || any(high > 1.0)) return false;
    const float2 size = float2(hierarchySource.xy);
    const float2 first = max(floor((low * 0.5 + 0.5) * size) - 1.0, 0.0);
    const float2 last = min(ceil((high * 0.5 + 0.5) * size) + 1.0, size - 1.0);
    // The level where the rectangle spans at most two cells: at most nine reads.
    uint level = 0;
    float divisor = 2.0;
    while (level + 1u < hierarchySource.z && any((last - first) / divisor > 2.0))
    {
        ++level;
        divisor *= 2.0;
    }
    const uint4 cells = hierarchyLevels[level];
    const uint2 a = min(uint2(first / divisor), cells.xy - 1u), b = min(uint2(last / divisor), cells.xy - 1u);
    float farthest = 1.0;
    for (uint y = a.y; y <= b.y; ++y)
        for (uint x = a.x; x <= b.x; ++x)
            farthest = min(farthest, hierarchy[cells.z + y * cells.x + x]);
    return nearest + 0.00001 < farthest;
}

// An instance's bin entries: up to two slots, each with the entry to store (record and dither code).
struct ItemBins
{
    uint count;
    uint slots[2];
    uint entries[2];
};

void addLevel(inout ItemBins bins, CullDraw draw, uint record, int level, uint code)
{
    if (level < 0 || level >= int(min(draw.lodInfo.x, MaxLods))) return;
    const uint lod = max(uint(level), draw.lodInfo.y);
    bins.slots[bins.count] = cullTotals.z * cullTotals.x + draw.items.w + lod;
    bins.entries[bins.count] = record | (code << DitherShift);
    ++bins.count;
}

// BasicInstanceLodChain::SelectBlend for an instance of projected radius `pixels` (distance scale applied).
void selectBlend(inout ItemBins bins, CullDraw draw, uint record, float pixels)
{
    const uint switchCount = min(draw.lodInfo.z, MaxSwitches);
    for (uint k = 0; k < switchCount; ++k)
    {
        const float radius = switchRadius(draw, k);
        if (!isfinite(radius) || !(radius > 0.0)) continue;
        const float upper = radius * (1.0 + TransitionWidth);
        if (pixels > upper || pixels < radius * (1.0 - TransitionWidth)) continue;
        const float fade = clamp((upper - pixels) / (2.0 * TransitionWidth * radius), 0.0, 1.0);
        const uint steps = uint(floor(fade * float(DitherSteps) + 0.5));
        const int finer = int(k) - 1, coarser = int(switchCoarser(draw, k));
        if (steps == 0u) addLevel(bins, draw, record, finer, 0u);
        else if (steps >= DitherSteps) addLevel(bins, draw, record, coarser, 0u);
        else
        {
            addLevel(bins, draw, record, finer, steps);
            addLevel(bins, draw, record, coarser, DitherCoarser | steps);
        }
        return;
    }
    // Outside every band: the last level whose switch the radius is below.
    int level = -1;
    for (uint k = 0; k < switchCount; ++k)
        if (pixels < switchRadius(draw, k)) level = int(k);
    addLevel(bins, draw, record, level, 0u);
}

// The bins an item draws in this phase; none when it is culled, another draw covers its size, or (phase 1) phase 0
// drew it. `rejectedForOcclusion` is set when the hierarchy alone rejected it.
ItemBins itemBins(uint item, out bool rejectedForOcclusion)
{
    ItemBins bins;
    bins.count = 0;
    rejectedForOcclusion = false;
    if (cullTotals.z == 1u && occludedItems[item] == 0u) return bins;
    const CullDraw draw = cullDraws[findDraw(item)];
    const uint record = draw.items.z + (item - draw.items.x);
    const CullRecord instance = records[record];
    const float4x4 model = float4x4(instance.modelColumns[0], instance.modelColumns[1],
                                    instance.modelColumns[2], instance.modelColumns[3]);
    // float4x4(rows) from columns: transpose to apply the model as it is stored.
    const float3 center = mul(transpose(model), float4(draw.bounds.xyz, 1.0)).xyz;
    const float scale = max(length(instance.modelColumns[0].xyz),
                            max(length(instance.modelColumns[1].xyz), length(instance.modelColumns[2].xyz)));
    const float radius = draw.bounds.w * scale;
    // Swaying instances reach past their rest bounds (InstanceBinning does the same). Only visibility widens; LOD
    // selection keeps the rest radius, so wind never changes detail.
    const float visibleRadius = radius * (1.0 + draw.screenRange.x);

    const float3 offset = center - cameraLod.xyz;
    const float distanceSquared = dot(offset, offset);
    const float reach = draw.screenRange.z + visibleRadius;
    if (distanceSquared > reach * reach) return bins;
    for (uint plane = 0; plane < 4; ++plane)
        if (dot(cullPlanes[plane].xyz, center) + cullPlanes[plane].w < -visibleRadius) return bins;

    const float pixels = radius / sqrt(max(distanceSquared, 0.000001)) * cameraLod.w * draw.screenRange.w;
    if (!(pixels > 0.0)) return bins;
    selectBlend(bins, draw, record, pixels);
    if (bins.count != 0u && occluded(center, visibleRadius))
    {
        rejectedForOcclusion = true;
        bins.count = 0;
    }
    return bins;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void countMain(uint3 id : SV_DispatchThreadID)
{
    const uint item = cullDispatch.z + id.x;
    if (item >= cullDispatch.w) return;
    bool rejectedForOcclusion;
    const ItemBins bins = itemBins(item, rejectedForOcclusion);
    // Phase 0 decides which items phase 1 tests again.
    if (cullTotals.z == 0u) occludedItems[item] = rejectedForOcclusion ? 1u : 0u;
    for (uint entry = 0; entry < bins.count; ++entry) InterlockedAdd(slotCounters[bins.slots[entry]], 1u);
}

static const uint ScanThreads = 256;
groupshared uint scanTotals[ScanThreads];

// Prefix sums of this phase's slots, into bins starting at cullTotals.w.
[shader("compute")]
[numthreads(256, 1, 1)]
void scanMain(uint3 id : SV_GroupThreadID)
{
    const uint slots = cullTotals.x, firstSlot = cullTotals.z * slots, cursors = 2u * slots;
    const uint perThread = (slots + ScanThreads - 1u) / ScanThreads;
    const uint begin = firstSlot + min(id.x * perThread, slots), end = min(begin + perThread, firstSlot + slots);
    uint total = 0;
    for (uint slot = begin; slot < end; ++slot) total += slotCounters[slot];
    scanTotals[id.x] = total;
    GroupMemoryBarrierWithGroupSync();
    // Each thread's exclusive prefix over the threads before it (256 adds at most).
    uint offset = cullTotals.w;
    for (uint other = 0; other < id.x; ++other) offset += scanTotals[other];
    for (uint slot = begin; slot < end; ++slot)
    {
        const uint count = slotCounters[slot];
        binIndices[slot].record = offset;
        slotArguments[slot * 5u + 1u] = count;
        slotArguments[slot * 5u + 4u] = cullTotals.y != 0u ? offset : 0u;
        slotCounters[cursors + slot] = 0u;
        offset += count;
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void scatterMain(uint3 id : SV_DispatchThreadID)
{
    const uint item = cullDispatch.z + id.x;
    if (item >= cullDispatch.w) return;
    bool rejectedForOcclusion;
    const ItemBins bins = itemBins(item, rejectedForOcclusion);
    for (uint entry = 0; entry < bins.count; ++entry)
    {
        const uint slot = bins.slots[entry];
        uint rank;
        InterlockedAdd(slotCounters[2u * cullTotals.x + slot], 1u, rank);
        binIndices[binIndices[slot].record + rank].record = bins.entries[entry];
    }
}
