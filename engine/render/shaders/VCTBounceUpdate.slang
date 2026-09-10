struct BounceParameters { uint resolution; uint cascade; uint cascadeCount; uint firstSlice; };
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { BounceParameters pass; };
[[vk::binding(1, 0)]] RWTexture3D<uint> surfaceRecord : register(u1, space0);
[[vk::binding(2, 0)]] RWTexture3D<uint> accumulationWeight : register(u2, space0);
[[vk::binding(3, 0)]] RWTexture3D<float4> secondaryVolume : register(u3, space0);
[[vk::binding(7, 0)]] Sampler3D bounce0 : register(t7, space0);
[[vk::binding(8, 0)]] Sampler3D bounce1 : register(t8, space0);
[[vk::binding(9, 0)]] Sampler3D bounce2 : register(t9, space0);
[[vk::binding(10, 0)]] Sampler3D bounce3 : register(t10, space0);
[[vk::binding(11, 0)]] Sampler3D bounce4 : register(t11, space0);
[[vk::binding(12, 0)]] Sampler3D bounce5 : register(t12, space0);
float4 vctBounceSample(float3 tc, float3 direction, float mip)
{
    float edge = 0.5 * exp2(ceil(mip)) / float(pass.resolution);
    tc = clamp(tc, edge, 1.0 - edge);
    tc.z = (float(pass.cascade) + tc.z) / float(max(pass.cascadeCount, 1u));
    float3 w = abs(direction); w /= max(w.x + w.y + w.z, 0.0001);
    float4 x = direction.x >= 0.0 ? bounce0.SampleLevel(tc,mip) : bounce1.SampleLevel(tc,mip);
    float4 y = direction.y >= 0.0 ? bounce2.SampleLevel(tc,mip) : bounce3.SampleLevel(tc,mip);
    float4 z = direction.z >= 0.0 ? bounce4.SampleLevel(tc,mip) : bounce5.SampleLevel(tc,mip);
    return x*w.x + y*w.y + z*w.z;
}
#include "VCTSecondaryBounce.slang"

[shader("compute")]
[numthreads(4, 4, 4)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    id.z += pass.firstSlice;
    if (any(id >= pass.resolution)) return;
    uint packed = surfaceRecord[int3(id)];
    float3 bounced = 0.0;
    if (packed != 0u)
    {
        float3 diffuse = float3((packed >> 15) & 31u, (packed >> 20) & 31u, (packed >> 25) & 31u) / 31.0;
        if (any(diffuse > 0.0))
        {
            float3 normal = normalize(float3(packed & 31u, (packed >> 5) & 31u, (packed >> 10) & 31u) / 31.0 * 2.0 - 1.0);
            float coverage = saturate(float(accumulationWeight[int3(id)]) / 4095.0);
            bounced = diffuse * coverage * vctSecondaryBounce((float3(id) + 0.5) / float(pass.resolution), normal, float(pass.resolution));
        }
    }
    secondaryVolume[int3(id)] = float4(bounced, 0.0);
}
