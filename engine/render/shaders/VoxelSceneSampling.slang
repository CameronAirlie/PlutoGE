// The voxel scene VCT publishes, as ray marches read it (VoxelSceneBinding.h binds it): up to three nested cascades
// sharing six directional radiance atlases (VCT's layout: premultiplied radiance in rgb, opacity in a, cascades
// stacked along z, directional mips). DDGI's voxel ray source and the light visibility mask's voxel occlusion
// (LightVisibilityVoxel.slang) march it.
//
// Bindings: set 0 binding 4 the parameters, 8-13 the atlases (+x, -x, +y, -y, +z, -z).
#ifndef PLUTO_VOXEL_SCENE_SAMPLING
#define PLUTO_VOXEL_SCENE_SAMPLING

[[vk::binding(4, 0)]] cbuffer VoxelSceneParameters : register(b4, space0)
{
    float4 voxelCascades[3]; // xyz origin, w size
    uint4 voxelCounts;       // x valid cascades, y cascades in the atlas, z resolution, w maximum mip
    float4 voxelTrace;       // x cone spread (footprint diameter per unit distance), y maximum distance
};
[[vk::binding(8, 0)]] Sampler3D voxelPositiveX : register(t8, space0);
[[vk::binding(9, 0)]] Sampler3D voxelNegativeX : register(t9, space0);
[[vk::binding(10, 0)]] Sampler3D voxelPositiveY : register(t10, space0);
[[vk::binding(11, 0)]] Sampler3D voxelNegativeY : register(t11, space0);
[[vk::binding(12, 0)]] Sampler3D voxelPositiveZ : register(t12, space0);
[[vk::binding(13, 0)]] Sampler3D voxelNegativeZ : register(t13, space0);

bool cascadeCoordinate(uint cascade, float3 position, out float3 coordinate)
{
    coordinate = (position - voxelCascades[cascade].xyz) / voxelCascades[cascade].w;
    return all(coordinate >= 0.0) && all(coordinate < 1.0);
}

float4 directionalSample(uint cascade, float3 coordinate, float3 direction, float mip)
{
    const float size = float(max(voxelCounts.z >> uint(ceil(mip)), 1u));
    const float halfTexel = 0.5 / size;
    coordinate = clamp(coordinate, halfTexel, 1.0 - halfTexel);
    const float3 atlas = float3(coordinate.xy, (float(cascade) + coordinate.z) / float(max(voxelCounts.y, 1u)));
    float3 weight = abs(direction);
    weight /= max(weight.x + weight.y + weight.z, 0.0001);
    const float4 x = direction.x >= 0.0 ? voxelPositiveX.SampleLevel(atlas, mip) : voxelNegativeX.SampleLevel(atlas, mip);
    const float4 y = direction.y >= 0.0 ? voxelPositiveY.SampleLevel(atlas, mip) : voxelNegativeY.SampleLevel(atlas, mip);
    const float4 z = direction.z >= 0.0 ? voxelPositiveZ.SampleLevel(atlas, mip) : voxelNegativeZ.SampleLevel(atlas, mip);
    return x * weight.x + y * weight.y + z * weight.z;
}

// Fades a cascade out towards its faces, over a band that grows with the footprint (as VCTConeTrace).
float cascadeWeight(uint cascade, float3 coordinate, float diameter)
{
    const float size = voxelCascades[cascade].w;
    const float3 edge = min(coordinate, 1.0 - coordinate) * size;
    const float band = min(size * 0.5, max(size * 0.15, diameter));
    return smoothstep(0.0, band, min(min(edge.x, edge.y), edge.z));
}

// The voxel scene around `position` for a footprint of `diameter`, blending from the finest cascade outwards.
// `covered` is the weight of cascades that contain the point (zero outside the scene); `voxelSize` the blended
// size of their voxels.
float4 sampleScene(float3 position, float3 direction, float diameter, out float covered, out float voxelSize)
{
    const uint count = min(voxelCounts.x, 3u);
    const float resolution = float(voxelCounts.z);
    float4 result = 0.0;
    float remaining = 1.0;
    voxelSize = voxelCascades[max(count, 1u) - 1].w / resolution;
    float sizeSum = 0.0;
    for (uint cascade = 0; cascade < count && remaining > 0.0; ++cascade)
    {
        float3 coordinate;
        if (!cascadeCoordinate(cascade, position, coordinate))
            continue;
        const float localVoxel = voxelCascades[cascade].w / resolution;
        const float weight = remaining * cascadeWeight(cascade, coordinate, diameter);
        if (weight <= 0.0)
            continue;
        const float mip = clamp(log2(max(diameter, localVoxel) / localVoxel), 0.0, float(voxelCounts.w));
        result += directionalSample(cascade, coordinate, direction, mip) * weight;
        sizeSum += localVoxel * weight;
        remaining -= weight;
    }
    covered = 1.0 - remaining;
    if (covered > 0.0)
        voxelSize = sizeSum / covered;
    return result;
}

// The clear share of the segment from `from` to `to`, marched through the finest voxels holding each point at a
// quarter voxel per step. Only opacity above one half counts: a sample within half a voxel of a surface layer reads
// part of it through filtering, so a segment running beside a wall, as one from a lookup near the wall to a probe
// near it does, would otherwise be blocked by the wall it never crosses. Every layer the segment touches counts,
// including one an end lies in: voxels are coarser than many rooms, and nothing tells which side of a layer such an
// end is on, so a sub-cell there sees no probe through it (DDGICommon.slang drops such sub-cells from lookups).
float segmentVisibility(float3 from, float3 to)
{
    const float3 delta = to - from;
    const float segment = length(delta);
    if (segment < 0.0001)
        return 1.0;
    const float3 direction = delta / segment;
    float covered, voxelSize;
    // The voxel at the start sets the step (Slang cannot emit a discarded call result for GLSL, so it is named).
    const float4 start = sampleScene(from, direction, 0.0, covered, voxelSize);
    const float step = max(0.25 * voxelSize, segment / 48.0);
    float transmission = 1.0;
    for (float travelled = 0.0; travelled <= segment; travelled += step)
    {
        float sampleCovered, sampleVoxel;
        const float opacity = sampleScene(from + direction * travelled, direction, 0.0, sampleCovered, sampleVoxel).a;
        if (sampleCovered <= 0.0)
            break;
        transmission *= 1.0 - saturate(opacity * 2.0 - 1.0);
        if (transmission < 0.02)
            return 0.0;
    }
    return transmission;
}

#endif
