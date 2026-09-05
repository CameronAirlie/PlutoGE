float3 probeAtlasCoordinate(int3 cell, int face)
{
    return (float3(cell) + float3(0.5, 0.5, float(face * 16) + 0.5)) / float3(16, 16, 96);
}
float4 cachedIrradiance(float3 position, float3 normal, float4 originSize)
{
    float3 tc = (position - originSize.xyz) / originSize.w;
    if (any(tc < 0.0) || any(tc > 1.0)) return 0.0;
    float3 grid = tc * 16.0 - 0.5;
    int3 base = int3(floor(grid)); float3 fraction = frac(grid);
    float3 total = 0.0; float sum = 0.0;
    float3 normalWeight = abs(normal); normalWeight /= max(dot(normalWeight, float3(1.0)), 0.0001);
    for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 2; ++x)
    {
        int3 offset = int3(x, y, z), cell = clamp(base + offset, int3(0), int3(15));
        float3 tri = lerp(1.0 - fraction, fraction, float3(offset));
        float3 probeToPoint = tc - (float3(cell) + 0.5) / 16.0;
        float distance = length(probeToPoint);
        float3 direction = probeToPoint / max(distance, 0.00001);
        float3 absolute = abs(direction);
        int axis = absolute.x > absolute.y ? (absolute.x > absolute.z ? 0 : 2) : (absolute.y > absolute.z ? 1 : 2);
        int face = axis * 2 + (direction[axis] < 0.0 ? 1 : 0);
        float2 moments = probeVisibility.SampleLevel(probeAtlasCoordinate(cell, face), 0.0).rg;
        float variance = max(moments.y - moments.x * moments.x, 0.000001);
        float delta = max(distance - moments.x - 0.005, 0.0);
        float visibility = variance / (variance + delta * delta);
        visibility = visibility * visibility * visibility;
        // Suppress probes behind the receiving surface, while allowing smooth interpolation.
        float facing = pow(saturate(dot(-direction, normal) * 0.5 + 0.5), 2.0);
        float4 rx = probeRadiance.SampleLevel(probeAtlasCoordinate(cell, normal.x >= 0.0 ? 0 : 1), 0.0);
        float4 ry = probeRadiance.SampleLevel(probeAtlasCoordinate(cell, normal.y >= 0.0 ? 2 : 3), 0.0);
        float4 rz = probeRadiance.SampleLevel(probeAtlasCoordinate(cell, normal.z >= 0.0 ? 4 : 5), 0.0);
        float4 sample = rx * normalWeight.x + ry * normalWeight.y + rz * normalWeight.z;
        float weight = tri.x * tri.y * tri.z * visibility * max(facing, 0.001) * sample.a;
        total += sample.rgb * weight; sum += weight;
    }
    float3 edge = min(tc, 1.0 - tc);
    float coverage = smoothstep(0.0, 0.1, min(min(edge.x, edge.y), edge.z));
    return float4(total / max(sum, 0.00001), coverage * smoothstep(0.0, 0.05, sum));
}
