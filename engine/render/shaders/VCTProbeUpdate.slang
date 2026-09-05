// Stationary ambient-cube probes. Distances are normalized by the cache span
// so squared moments remain representable in half precision.
struct ProbeParameters
{
    float4 originSize;
    uint4 counts; // source cascade, atlas cascades, voxel resolution, maximum mip
    uint4 update; // first probe, probe budget, clear only, unused
};
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { ProbeParameters pass; };
[[vk::binding(1, 0)]] RWTexture3D<float4> probeRadiance : register(u1, space0);
[[vk::binding(2, 0)]] RWTexture3D<float4> probeVisibility : register(u2, space0);
[[vk::binding(7, 0)]] Sampler3D positiveX : register(t7, space0);
[[vk::binding(8, 0)]] Sampler3D negativeX : register(t8, space0);
[[vk::binding(9, 0)]] Sampler3D positiveY : register(t9, space0);
[[vk::binding(10, 0)]] Sampler3D negativeY : register(t10, space0);
[[vk::binding(11, 0)]] Sampler3D positiveZ : register(t11, space0);
[[vk::binding(12, 0)]] Sampler3D negativeZ : register(t12, space0);
float3 axisDirection(uint axis)
{
    float3 d = 0.0; d[axis / 2] = (axis & 1) == 0 ? 1.0 : -1.0; return d;
}
float4 sourceSample(float3 tc, float3 direction, float mip)
{
    float halfTexel = 0.5 * exp2(ceil(mip)) / float(pass.counts.z);
    tc = clamp(tc, halfTexel, 1.0 - halfTexel);
    tc.z = (float(pass.counts.x) + tc.z) / float(pass.counts.y);
    float3 w = abs(direction); w /= max(w.x + w.y + w.z, 0.0001);
    float4 x = direction.x >= 0.0 ? positiveX.SampleLevel(tc, mip) : negativeX.SampleLevel(tc, mip);
    float4 y = direction.y >= 0.0 ? positiveY.SampleLevel(tc, mip) : negativeY.SampleLevel(tc, mip);
    float4 z = direction.z >= 0.0 ? positiveZ.SampleLevel(tc, mip) : negativeZ.SampleLevel(tc, mip);
    return x * w.x + y * w.y + z * w.z;
}
[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= pass.update.y) return;
    // Odd stride interleaves updates throughout the volume instead of sweeping a plane.
    uint index = ((pass.update.x + id.x) * 2053u) % 4096u;
    uint3 cell = uint3(index % 16u, (index / 16u) % 16u, index / 256u);
    if (pass.update.z != 0)
    {
        for (uint face = 0; face < 6; ++face)
        {
            uint3 at = cell + uint3(0, 0, face * 16);
            probeRadiance[at] = 0.0; probeVisibility[at] = 0.0;
        }
        return;
    }
    float3 origin = (float3(cell) + 0.5) / 16.0;
    float voxel = 1.0 / float(pass.counts.z);
    bool embedded = sourceSample(origin, float3(0, 1, 0), 0.0).a > 0.5;
    for (uint face = 0; face < 6; ++face)
    {
        float3 n = axisDirection(face);
        float3 up = abs(n.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
        float3 tangent = normalize(cross(up, n)), bitangent = cross(n, tangent);
        float3 radiance = 0.0; float first = 0.0, second = 0.0;
        for (uint ray = 0; ray < 5 && !embedded; ++ray)
        {
            float angle = 6.2831853 * (float(ray) - 1.0) / 4.0;
            float3 d = ray == 0 ? n : normalize(n * 0.55 +
                (tangent * cos(angle) + bitangent * sin(angle)) * 0.835);
            float distance = voxel, transmission = 1.0;
            float mean = 0.0, square = 0.0;
            for (uint step = 0; step < 32 && transmission > 0.02; ++step)
            {
                float3 tc = origin + d * distance;
                if (any(tc < 0.0) || any(tc >= 1.0)) break;
                float diameter = max(voxel, distance * 0.5);
                float mip = clamp(log2(diameter / voxel), 0.0, float(pass.counts.w));
                float4 sample = sourceSample(tc, d, mip);
                radiance += transmission * sample.rgb / 5.0;
                float hit = transmission * saturate(sample.a);
                mean += hit * distance; square += hit * distance * distance;
                transmission *= 1.0 - saturate(sample.a);
                distance += max(voxel, diameter * 0.5);
            }
            mean += transmission * distance; square += transmission * distance * distance;
            first += mean / 5.0; second += square / 5.0;
        }
        uint3 at = cell + uint3(0, 0, face * 16);
        float4 old = probeRadiance[at];
        // Never retain stale radiance or visibility for a newly embedded probe.
        float history = old.a > 0.0 && !embedded ? 0.75 : 0.0;
        probeRadiance[at] = float4(lerp(radiance, old.rgb, history), embedded ? 0.0 : 1.0);
        probeVisibility[at] = lerp(float4(first, second, 0, 0), probeVisibility[at], history);
    }
}
