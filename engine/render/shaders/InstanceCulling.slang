// Per-instance culling and LOD binning of instanced draws on the GPU (GpuInstanceCulling.h), the compute counterpart
// of the CPU binning in BasicRenderer::Render; selection follows BasicInstanceLodChain::Select. Three passes:
//   countMain   per instance: cull (side planes, draw distance, the previous frame's depth hierarchy), pick the LOD,
//               count it into its slot (one per draw and LOD);
//   scanMain    one group: prefix sums of the slot counts give each bin's offset, written to the bin buffer's header
//               and into each slot's indirect draw arguments (the CPU uploads their index ranges);
//   scatterMain per instance again: the same choice, then its record index goes into its slot's bin.
// count and scatter run once per instance record page, which the dispatch binds as `records`.

static const uint MaxLods = 8;
static const uint NotCounted = 0xffffffffu;

[[vk::binding(0, 0)]] cbuffer CullParameters : register(b0)
{
    float4 cullPlanes[4];                       // left, right, bottom, top: normal xyz, distance w
    float4 cameraLod;                           // camera position, pixels per unit radius over distance
    column_major float4x4 hierarchyViewProjection; // the previous frame's, which built the hierarchy
    uint4 hierarchySource;                      // depth width, height, level count, flags
    uint4 cullDispatch;                         // first draw, draw count, first item, item end
    uint4 cullTotals;                           // slot count, first instance in arguments (1) or bin header (0)
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
    uint4 items;            // first item, item count, first record, first slot
    uint4 lodInfo;          // LOD count, minimum LOD
    float4 bounds;          // model-space sphere: centre, radius
    float4 screenRange;     // drawn projected radii [x, y), draw distance
    float4 thresholds[2];   // per LOD: largest projected radius it serves
    float4 triangles[2];    // per LOD: triangles
};
struct CullIndex
{
    uint record;
};

[[vk::binding(1, 0)]] StructuredBuffer<CullRecord> records : register(t1);
[[vk::binding(2, 0)]] StructuredBuffer<CullDraw> cullDraws : register(t2);
// Slot counts, then each slot's scatter cursor.
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> slotCounters : register(u3);
// The bins: one header entry per slot (its bin's offset), then the bins' record indices.
[[vk::binding(4, 0)]] RWStructuredBuffer<CullIndex> binIndices : register(u4);
// Five words per slot: index count, instance count, first index, vertex offset, first instance. The CPU writes the
// index range; the scan fills in the instance count and first instance.
[[vk::binding(5, 0)]] RWStructuredBuffer<uint> slotArguments : register(u5);
[[vk::binding(6, 0)]] StructuredBuffer<float> hierarchy : register(t6);

float lodValue(float4 values[2], uint lod)
{
    return values[lod / 4][lod % 4];
}

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

// Whether the sphere was behind the previous frame's depth: reverse Z, so the hierarchy keeps the farthest depth.
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

// The slot an item draws in, or NotCounted when it is culled or another draw covers its size.
uint itemSlot(uint item, out uint record)
{
    const CullDraw draw = cullDraws[findDraw(item)];
    const uint local = item - draw.items.x;
    record = draw.items.z + local;
    const CullRecord instance = records[record];
    const float4x4 model = float4x4(instance.modelColumns[0], instance.modelColumns[1],
                                    instance.modelColumns[2], instance.modelColumns[3]);
    // float4x4(rows) from columns: transpose to apply the model as it is stored.
    const float3 center = mul(transpose(model), float4(draw.bounds.xyz, 1.0)).xyz;
    const float scale = max(length(instance.modelColumns[0].xyz),
                            max(length(instance.modelColumns[1].xyz), length(instance.modelColumns[2].xyz)));
    const float radius = draw.bounds.w * scale;

    const float3 offset = center - cameraLod.xyz;
    const float distanceSquared = dot(offset, offset);
    const float reach = draw.screenRange.z + radius;
    if (distanceSquared > reach * reach) return NotCounted;
    for (uint plane = 0; plane < 4; ++plane)
        if (dot(cullPlanes[plane].xyz, center) + cullPlanes[plane].w < -radius) return NotCounted;

    const float pixels = radius / sqrt(max(distanceSquared, 0.000001)) * cameraLod.w;
    if (pixels < draw.screenRange.x || pixels >= draw.screenRange.y) return NotCounted;
    if (occluded(center, radius)) return NotCounted;

    const uint lodCount = min(draw.lodInfo.x, MaxLods);
    const uint lastLod = lodCount - 1u;
    uint selected = 0;
    for (uint lod = 0; lod < lodCount; ++lod)
        if (pixels <= lodValue(draw.thresholds, lod)) selected = lod;
    selected = min(max(selected, draw.lodInfo.y), lastLod);
    // BasicInstanceLodChain::MaxTrianglesPerPixel over the projected bounding disc.
    const float budget = 0.15 * 3.14159265 * pixels * pixels;
    while (selected < lastLod && lodValue(draw.triangles, selected) > budget) ++selected;
    return draw.items.w + selected;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void countMain(uint3 id : SV_DispatchThreadID)
{
    const uint item = cullDispatch.z + id.x;
    if (item >= cullDispatch.w) return;
    uint record;
    const uint slot = itemSlot(item, record);
    if (slot != NotCounted) InterlockedAdd(slotCounters[slot], 1u);
}

static const uint ScanThreads = 256;
groupshared uint scanTotals[ScanThreads];

[shader("compute")]
[numthreads(256, 1, 1)]
void scanMain(uint3 id : SV_GroupThreadID)
{
    const uint slots = cullTotals.x;
    const uint perThread = (slots + ScanThreads - 1u) / ScanThreads;
    const uint begin = min(id.x * perThread, slots), end = min(begin + perThread, slots);
    uint total = 0;
    for (uint slot = begin; slot < end; ++slot) total += slotCounters[slot];
    scanTotals[id.x] = total;
    GroupMemoryBarrierWithGroupSync();
    // Each thread's exclusive prefix over the threads before it (256 adds at most).
    uint offset = slots;
    for (uint other = 0; other < id.x; ++other) offset += scanTotals[other];
    for (uint slot = begin; slot < end; ++slot)
    {
        const uint count = slotCounters[slot];
        binIndices[slot].record = offset;
        slotArguments[slot * 5u + 1u] = count;
        slotArguments[slot * 5u + 4u] = cullTotals.y != 0u ? offset : 0u;
        slotCounters[slots + slot] = 0u;
        offset += count;
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void scatterMain(uint3 id : SV_DispatchThreadID)
{
    const uint item = cullDispatch.z + id.x;
    if (item >= cullDispatch.w) return;
    uint record;
    const uint slot = itemSlot(item, record);
    if (slot == NotCounted) return;
    uint rank;
    InterlockedAdd(slotCounters[cullTotals.x + slot], 1u, rank);
    binIndices[binIndices[slot].record + rank].record = record;
}
