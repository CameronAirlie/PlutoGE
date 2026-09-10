// Shared by both voxelizers. The caller supplies a read-only first-bounce
// directional field via vctBounceSample; it must never sample its own output.
float3 vctSecondaryBounce(float3 position, float3 normal, float resolution)
{
    float voxelWidth = 1.0 / resolution;
    float3 tangent = normalize(cross(normal, abs(normal.y) < 0.95 ? float3(0,1,0) : float3(1,0,0)));
    float3 bitangent = cross(normal, tangent);
    // Four equal-weight cosine-distributed directions integrate diffuse
    // irradiance / pi. Material reflectance is applied by the caller once.
    float3 result = float3(0,0,0);
    float3 origin = position + normal * voxelWidth * 1.5;
    for (int cone = 0; cone < 4; ++cone)
    {
        float angle = (float(cone) + 0.5) * 1.57079632679;
        float3 direction = normal * 0.70710678 +
            (tangent * cos(angle) + bitangent * sin(angle)) * 0.70710678;
        float distance = voxelWidth;
        float transmission = 1.0;
        for (int step = 0; step < 24; ++step)
        {
            float3 tc = origin + direction * distance;
            if (min(min(tc.x, tc.y), tc.z) <= 0.0 || max(max(tc.x, tc.y), tc.z) >= 1.0) break;
            float diameter = max(voxelWidth, distance * 0.75);
            float mip = clamp(log2(diameter / voxelWidth), 0.0, log2(resolution));
            float4 radianceSample = vctBounceSample(tc, direction, mip);
            result += radianceSample.rgb * transmission * 0.25;
            transmission *= 1.0 - clamp(radianceSample.a, 0.0, 1.0);
            if (transmission < 0.02) break;
            distance += max(voxelWidth, diameter * 0.5);
        }
    }
    return result;
}
