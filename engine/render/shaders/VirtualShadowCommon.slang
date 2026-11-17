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
    float4 settings; // softness, enabled, reset cache, resolution scale
    float4 camera;
    uint4 pool; // capacity, atlas tiles per axis, atlas pixels per axis, spot fine grid
    uint4 scheduling; // maximum preferred dirty age, allow oversized page, unused
    float4 culling; // minimum coarse caster radius in texels, shadow sway distance (0 = everywhere), shadow sway minimum texels, unused
    uint4 levels; // directional root level (= active fine levels), static layer generation (0 = none), unused
    uint4 transition; // incoming clipmap active, incoming clipmap promoted this frame, unused, unused
    uint4 membershipEpochs[(PLUTO_VSM_LEVELS + 3) / 4];
};
// The incoming clipmap (PLUTO_VSM_INCOMING_BASE): rendered for the new direction while the light turns, and presented
// once resident. Receivers sample only the presented clipmap (levels 0..virtualRootLevel).
bool virtualIncomingActive(VirtualShadowParameters p) { return p.transition.x != 0; }
bool virtualIncomingPromoted(VirtualShadowParameters p) { return p.transition.y != 0; }
bool virtualIncomingLevel(uint level) { return level >= PLUTO_VSM_INCOMING_BASE && level < PLUTO_VSM_DIRECTIONAL_LEVELS; }
// The presented level an incoming level mirrors (its grid, texels and caster LODs); other levels are their own.
uint virtualPresentedLevel(uint level) { return virtualIncomingLevel(level) ? level - PLUTO_VSM_INCOMING_BASE : level; }
// Whether a caster level mask (one bit per presented or spot level) includes `level`.
bool virtualLevelInMask(uint mask, uint level) { return ((mask >> virtualPresentedLevel(level)) & 1u) != 0; }
struct VirtualShadowPage
{
    int4 address; // absolute page XY, level, projection epoch
    uint4 state; // current virtual key, selected frame, valid depth, scheduled dirty
    uint4 signature; // cached hash low/high, historical dirty start, last publish
    uint4 pending; // candidate hash low/high, triangle cost, membership generation
    // Continuously requested dirty start; this frame's static-layer hash low/high; 1 when this update redraws the static
    // layer (always without one).
    uint4 freshness;
    uint4 layer; // static layer: hash low/high and static layer generation it was drawn under, unused
};
// With a static layer (levels.y, its generation, nonzero) each page keeps the static casters' depth apart: an update
// whose static casters are unchanged restores it and draws the dynamic casters only.
bool virtualStaticLayer(VirtualShadowParameters p) { return p.levels.y != 0; }
// Casters per physical page in the page-major caster/page membership records (levels.z, the caster capacity).
uint virtualMembershipStride(VirtualShadowParameters p) { return p.levels.z; }
bool virtualRedrawsStatic(VirtualShadowParameters p, VirtualShadowPage page) { return !virtualStaticLayer(p) || page.freshness.w != 0; }
struct VirtualShadowCaster
{
    float4 bounds;
    uint4 draw; // index count, first index, model count, original draw index
    uint4 identity; // swaying signature low/high, membership generation, levels served (one bit per level)
    float4 extents; // conservative world AABB around bounds.xyz; negative = unknown
    uint4 still; // rest-pose signature low/high, its largest sway (world units, float bits; 0 if its wind does not animate it), 1 if dynamic
};
// A caster chunk draws only into pages of the levels it serves: LOD variants of one chunk split the levels between
// them by texel size (VirtualShadowCasterLods.h). Other chunks serve PLUTO_VSM_ALL_LEVELS.
bool virtualCasterServesLevel(VirtualShadowCaster caster, uint level) { return virtualLevelInMask(caster.identity.w, level); }
// Masked chunks list one entry per (page, instance); others one entry per page, drawn for every model.
bool virtualMaskedCaster(VirtualShadowCaster caster) { return caster.draw.z <= PLUTO_VSM_MASKED_INSTANCES; }
bool virtualMaskedModelCount(uint models) { return models <= PLUTO_VSM_MASKED_INSTANCES; }
uint virtualListStride(uint4 limits) { return limits.z * PLUTO_VSM_MASKED_INSTANCES; }
uint virtualPageEntry(uint slot, uint instance, bool sways)
{
    return slot | (instance << PLUTO_VSM_ENTRY_INSTANCE_SHIFT) | (sways ? PLUTO_VSM_ENTRY_SWAY : 0u);
}
uint virtualEntrySlot(uint entry) { return entry & PLUTO_VSM_ENTRY_SLOT_MASK; }
uint virtualEntryInstance(uint entry) { return (entry >> PLUTO_VSM_ENTRY_INSTANCE_SHIFT) & PLUTO_VSM_ENTRY_INSTANCE_MASK; }
bool virtualEntrySways(uint entry) { return (entry & PLUTO_VSM_ENTRY_SWAY) != 0; }
uint2 virtualPageCoordinate(uint key) { return uint2(key % PLUTO_VSM_GRID, key / PLUTO_VSM_GRID); }
// Directional levels are 0..virtualRootLevel: the fine levels, then the coarse root.
uint virtualRootLevel(VirtualShadowParameters p) { return p.levels.x; }
uint virtualGrid(VirtualShadowParameters p, uint level)
{
    return level >= PLUTO_VSM_DIRECTIONAL_LEVELS && level < PLUTO_VSM_SPOT_ROOT_BASE
        ? p.pool.w : PLUTO_VSM_LEVEL_GRID(level, virtualRootLevel(p));
}
uint virtualResolution(VirtualShadowParameters p, uint level) { return virtualGrid(p, level) * PLUTO_VSM_PAGE_SIZE; }
bool virtualDirectionalRoot(VirtualShadowParameters p, uint level)
{
    return level < PLUTO_VSM_DIRECTIONAL_LEVELS && virtualPresentedLevel(level) == virtualRootLevel(p);
}
bool virtualCoarseLevel(VirtualShadowParameters p, uint level)
{
    return virtualDirectionalRoot(p, level) || level >= PLUTO_VSM_SPOT_ROOT_BASE;
}
// Whether a level holds pages this frame: incoming levels only while the light turns.
bool virtualLevelActive(VirtualShadowParameters p, uint level)
{
    return !virtualIncomingLevel(level) || virtualIncomingActive(p);
}
float3 virtualCoordinate(VirtualShadowParameters p, uint level, float3 world)
{
    float4 projected = mul(p.matrices[level], float4(world, 1));
    float3 clip = projected.xyz / projected.w;
    return float3(clip.xy * 0.5 + 0.5, clip.z);
}
float virtualReceiverFootprint(VirtualShadowParameters p, float3 world)
{
    // Use the same constant-depth pixel footprint for residency and shading.
    // Surface derivatives include receiver slope, unlike depth-request pixel
    // offsets; mixing them selects unrequested levels on oblique surfaces.
    float clipW = mul(p.viewProjection, float4(world, 1)).w;
    float4 dx = mul(p.inverseViewProjection, float4(2.0 * clipW / p.viewport.x, 0, 0, 0));
    float4 dy = mul(p.inverseViewProjection, float4(0, 2.0 * clipW / p.viewport.y, 0, 0));
    return max(length((dx.xyz - world * dx.w) / (1.0 + dx.w)),
               length((dy.xyz - world * dy.w) / (1.0 + dy.w)));
}
// The level of the directional clipmap at `base` (the presented one, or PLUTO_VSM_INCOMING_BASE) whose texels suit a
// receiver footprint at `world`, or -1 outside it.
int selectVirtualLevel(VirtualShadowParameters p, float3 world, float footprint, int base = 0)
{
    footprint *= max(p.settings.w, 1.0);
    const int root = base + int(virtualRootLevel(p));
    for (int level = base; level <= root; ++level)
    {
        float3 uv = virtualCoordinate(p, level, world);
        float guard = 7.0 / virtualResolution(p, level);
        // The outermost fine level and the root accept any footprint.
        if (all(uv.xy > guard) && all(uv.xy < 1.0 - guard) && uv.z >= 0 && uv.z <= 1 &&
            (p.metrics[level].x * 2.0 >= footprint || level >= root - 1)) return level;
    }
    return -1;
}

// Match shading and residency at clipmap/footprint transitions. Start blending
// before the level selector advances, ending at exactly the selector boundary.
float virtualLevelBlend(VirtualShadowParameters p, int level, float3 world, float footprint, int base = 0)
{
    const int root = base + int(virtualRootLevel(p));
    if (level < base || level >= root) return 0.0;
    float3 uv = virtualCoordinate(p, level, world);
    float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    float guard = 7.0 / virtualResolution(p, level);
    float spatial = 1.0 - smoothstep(guard, 0.1, edge);
    float resolutionBlend = level < root - 1
        ? smoothstep(1.5, 2.0, footprint * max(p.settings.w, 1.0) / p.metrics[level].x)
        : 0.0;
    return max(spatial, resolutionBlend);
}
