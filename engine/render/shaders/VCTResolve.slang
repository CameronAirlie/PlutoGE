struct ResolveParameters
{
    uint resolution;
    uint destinationZOffset;
    uint2 padding;
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

float opacityAt(int3 coordinate, int3 size)
{
    if (any(coordinate < 0) || any(coordinate >= size)) return 0.0;
    return saturate(float(accumulationWeight[coordinate]) / 4095.0);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void computeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int3 coordinate = int3(dispatchThreadId);
    const int3 size = int3(pass.resolution);
    if (any(coordinate >= size)) return;
    const uint weight = accumulationWeight[coordinate];
    float3 radiance = 0.0;
    if (weight > 0)
        radiance = float3(accumulationR[coordinate], accumulationG[coordinate],
                          accumulationB[coordinate]) * (16.0 / float(weight));
    float opacity = opacityAt(coordinate, size);
    const int3 offsets[6] = {int3(1,0,0), int3(-1,0,0), int3(0,1,0),
                             int3(0,-1,0), int3(0,0,1), int3(0,0,-1)};
    for (uint index = 0; index < 6; ++index)
        opacity = max(opacity, opacityAt(coordinate + offsets[index], size) * 0.35);
    resolvedVolume[coordinate + int3(0, 0, pass.destinationZOffset)] = float4(radiance, opacity);
}
