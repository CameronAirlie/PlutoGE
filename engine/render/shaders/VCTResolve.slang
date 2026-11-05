struct ResolveParameters
{
    uint resolution;
    uint destinationZOffset;
    float secondaryGain;
    uint padding;
    uint4 regionOrigin;
    uint4 regionExtent;
};

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { ResolveParameters pass; };
[[vk::binding(1, 0)]] RWTexture3D<uint> accumulationR : register(u1, space0);
[[vk::binding(2, 0)]] RWTexture3D<uint> accumulationG : register(u2, space0);
[[vk::binding(3, 0)]] RWTexture3D<uint> accumulationB : register(u3, space0);
[[vk::binding(4, 0)]] RWTexture3D<uint> accumulationWeight : register(u4, space0);
// Keep the shader value type at 32-bit. The backing image may still be RGBA16F;
// the API performs the store conversion without requiring optional GLSL
// explicit-arithmetic-types extensions on OpenGL drivers.
[[vk::binding(5, 0)]] RWTexture3D<float4> resolvedVolume : register(u5, space0);

[[vk::binding(6, 0)]] RWTexture3D<float4> secondaryVolume : register(u6, space0);

[[vk::binding(7, 0)]] RWTexture3D<float4> directVolume : register(u7, space0);
// Sampled, as every image unit an OpenGL driver is sure to have is taken.
[[vk::binding(8, 0)]] Sampler3D<uint> surfaceRecord : register(t8, space0);

#include "VCTVoxelOpacity.slang"

[shader("compute")]
[numthreads(4, 4, 4)]
void computeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId >= pass.regionExtent.xyz)) return;
    const int3 coordinate = int3(dispatchThreadId + pass.regionOrigin.xyz);
    const int3 size = int3(pass.resolution);
    if (any(coordinate >= size)) return;
    const uint weight = accumulationWeight[coordinate];
    float3 radiance = 0.0;
    // Keep sub-voxel radiance premultiplied by coverage. Normalize only when
    // overlapping surfaces exceed a full footprint, never by a fractional area.
    if (weight > 0)
        radiance = float3(accumulationR[coordinate], accumulationG[coordinate],
                          accumulationB[coordinate]) * (1.0 / max(float(weight), 4095.0));
    const float coverage = saturate(float(weight) / 4095.0);
    const float opacity = vctVoxelOpacity(weight, surfaceRecord.Load(int4(coordinate, 0)));
    // Radiance was weighted by coverage: foliage's opacity is lower.
    if (coverage > 0.0) radiance *= opacity / coverage;
    if (pass.padding != 0)
        radiance = min(radiance + directVolume[coordinate].rgb, 16.0 * opacity);
    if (pass.secondaryGain > 0.0) radiance += secondaryVolume[coordinate].rgb * pass.secondaryGain;
    // Coverage and premultiplied radiance describe the same surface footprint.
    // Dilating opacity alone creates dark shells that absorb the source before
    // directional mip compositing, progressively losing distant bounce energy.
    // Conservative voxelization already records sub-voxel surface coverage.
    resolvedVolume[coordinate + int3(0, 0, pass.destinationZOffset)] = float4(radiance, opacity);
}
