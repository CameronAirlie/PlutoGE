// Screen-space surface guide and depth hierarchy, built in one dispatch (see ScreenSpaceGuide.h).
//
// Each 16 x 16 group covers a 32 x 32 block of G-buffer pixels: one thread per 2 x 2 pixel quad. It writes the guide
// (at full or half resolution) and, in the PLUTO_GUIDE_HIERARCHY variant, five levels of a linear depth hierarchy that
// starts at half resolution, reduced in group-shared memory instead of one dispatch and barrier per level.
//
// parameters[0]: x = guide divisor (1 or 2), y = hierarchy falloff distance, z = hierarchy level count (1..5).
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "ScreenSpaceGuideCommon.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] [format("rgba32f")] RWTexture2D<float4> guide : register(u1, space0);
#ifdef PLUTO_GUIDE_HIERARCHY
[[vk::binding(2, 0)]] RWTexture2D<float> depthLevel0 : register(u2, space0);
[[vk::binding(3, 0)]] RWTexture2D<float> depthLevel1 : register(u3, space0);
[[vk::binding(4, 0)]] RWTexture2D<float> depthLevel2 : register(u4, space0);
[[vk::binding(5, 0)]] RWTexture2D<float> depthLevel3 : register(u5, space0);
[[vk::binding(6, 0)]] RWTexture2D<float> depthLevel4 : register(u6, space0);
#endif
[[vk::binding(7, 0)]] Sampler2D depthTexture : register(t7, space0);
[[vk::binding(8, 0)]] Sampler2D normalTexture : register(t8, space0);
[[vk::binding(9, 0)]] Sampler2D materialTexture : register(t9, space0);

static const uint GroupQuads = 16;

void writeGuide(int2 pixel, int2 guidePixel, float depth)
{
    const float3 normal = decodeNormal(normalTexture.Load(int3(pixel, 0)).xyz);
    const float roughness = saturate(materialTexture.Load(int3(pixel, 0)).g);
    guide[guidePixel] = encodeGuide(depth, normal, roughness);
}

#ifdef PLUTO_GUIDE_HIERARCHY
groupshared float levelDepths[GroupQuads * GroupQuads];

// Prefer the far surface at discontinuities: thin foreground silhouettes must not expand into broad occluders at
// coarse levels. Never average background (zero) into a surface.
float reduceDepths(float4 depths)
{
    const float farthest = max(max(depths.x, depths.y), max(depths.z, depths.w));
    float4 weights = saturate(1.0 - abs(depths - farthest) / max(pass.parameters[0].y, 0.05));
    weights *= weights * float4(depths > 0.0);
    return dot(depths, weights) / max(dot(weights, float4(1.0)), 0.000001);
}

// Reduces the shared 2^(level-1)-wide grid into the 2^level-wide one held by the first threads, then stores it.
void reduceLevel(uint level, uint2 local, uint2 group, inout float value)
{
    const uint width = GroupQuads >> level;
    const bool active = all(local < width);
    float4 quad = 0.0;
    if (active)
    {
        const uint source = GroupQuads >> (level - 1);
        const uint base = local.y * 2 * source + local.x * 2;
        quad = float4(levelDepths[base], levelDepths[base + 1], levelDepths[base + source], levelDepths[base + source + 1]);
    }
    GroupMemoryBarrierWithGroupSync();
    if (active)
    {
        value = reduceDepths(quad);
        levelDepths[local.y * width + local.x] = value;
        const uint2 coordinate = group * width + local;
        uint levelWidth, levelHeight;
        if (level == 1) depthLevel1.GetDimensions(levelWidth, levelHeight);
        else if (level == 2) depthLevel2.GetDimensions(levelWidth, levelHeight);
        else if (level == 3) depthLevel3.GetDimensions(levelWidth, levelHeight);
        else depthLevel4.GetDimensions(levelWidth, levelHeight);
        if (all(coordinate < uint2(levelWidth, levelHeight)))
        {
            if (level == 1) depthLevel1[coordinate] = value;
            else if (level == 2) depthLevel2[coordinate] = value;
            else if (level == 3) depthLevel3[coordinate] = value;
            else depthLevel4[coordinate] = value;
        }
    }
    GroupMemoryBarrierWithGroupSync();
}
#endif

[shader("compute")]
[numthreads(16, 16, 1)]
void buildMain(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    uint fullWidth, fullHeight, guideWidth, guideHeight;
    depthTexture.GetDimensions(fullWidth, fullHeight);
    guide.GetDimensions(guideWidth, guideHeight);
    const int2 fullSize = int2(fullWidth, fullHeight);
    const int2 quad = int2(groupId.xy * GroupQuads + localId.xy);
    const int2 base = quad * 2;
    // Edge quads of odd extents repeat the last row/column, as a clamped 2 x 2 reduction would.
    float4 depths;
    [ForceUnroll] for (int index = 0; index < 4; ++index)
    {
        const int2 pixel = min(base + int2(index & 1, index >> 1), fullSize - 1);
        depths[index] = linearViewDepth(depthTexture.Load(int3(pixel, 0)).r, pass.projection, pass.zeroToOneDepth);
    }

    if (pass.parameters[0].x > 1.5)
    {
        // Half resolution: one texel per quad, describing the pixel under its centre (always inside the quad).
        if (all(quad < int2(guideWidth, guideHeight)))
        {
            const int2 pixel = guideRepresentativePixel(quad, int2(guideWidth, guideHeight), fullSize);
            const int2 corner = clamp(pixel - base, 0, 1);
            writeGuide(pixel, quad, depths[corner.y * 2 + corner.x]);
        }
    }
    else
    {
        [ForceUnroll] for (int index = 0; index < 4; ++index)
        {
            const int2 pixel = base + int2(index & 1, index >> 1);
            if (all(pixel < fullSize))
                writeGuide(pixel, pixel, depths[index]);
        }
    }

#ifdef PLUTO_GUIDE_HIERARCHY
    uint levelWidth, levelHeight;
    depthLevel0.GetDimensions(levelWidth, levelHeight);
    float value = reduceDepths(depths);
    if (all(quad < int2(levelWidth, levelHeight)))
        depthLevel0[quad] = value;
    levelDepths[localId.y * GroupQuads + localId.x] = value;
    GroupMemoryBarrierWithGroupSync();
    // Levels past the configured count are bound to the last level and never written: the reduction stops there.
    const uint levels = uint(pass.parameters[0].z);
    for (uint level = 1; level < 5; ++level)
    {
        if (level >= levels) break;
        reduceLevel(level, localId.xy, groupId.xy, value);
    }
#endif
}
