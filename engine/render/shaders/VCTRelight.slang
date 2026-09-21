#include "VCTLightingParameters.slang"
[[vk::binding(0, 0)]] cbuffer Lighting : register(b0, space0) { VoxelParameters voxel; };
[[vk::binding(4, 0)]] cbuffer Region : register(b4, space0) { uint4 regionOrigin; uint4 regionExtent; };
[[vk::binding(1, 0)]] RWTexture3D<uint> surfaceRecord : register(u1, space0);
[[vk::binding(2, 0)]] RWTexture3D<uint> accumulationWeight : register(u2, space0);
[[vk::binding(3, 0)]] RWTexture3D<float4> directVolume : register(u3, space0);
[[vk::binding(10, 0)]] Sampler2D shadowTexture0 : register(t10, space0);
[[vk::binding(11, 0)]] Sampler2D shadowTexture1 : register(t11, space0);
[[vk::binding(12, 0)]] Sampler2D shadowTexture2 : register(t12, space0);
[[vk::binding(13, 0)]] Sampler2D shadowTexture3 : register(t13, space0);
#include "VCTLighting.slang"

[shader("compute")]
[numthreads(4, 4, 4)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    if (any(thread >= regionExtent.xyz)) return;
    uint3 cell = thread + regionOrigin.xyz;
    uint packed = surfaceRecord[cell];
    float3 radiance = 0.0;
    if (packed != 0)
    {
        float3 normal = normalize(float3(packed & 31u, (packed >> 5) & 31u, (packed >> 10) & 31u) / 31.0 * 2.0 - 1.0);
        float3 diffuse = float3((packed >> 15) & 31u, (packed >> 20) & 31u, (packed >> 25) & 31u) / 31.0;
        float3 world = voxel.volumeOrigin + (float3(cell) + 0.5) * (voxel.volumeSize / voxel.resolution);
        float3 direct = evaluateDirectLighting(world, normal);
        radiance = clamp(diffuse * direct / 3.14159265, 0.0, 16.0) *
            saturate(float(accumulationWeight[cell]) / 4095.0);
    }
    directVolume[cell] = float4(radiance, 0);
}
