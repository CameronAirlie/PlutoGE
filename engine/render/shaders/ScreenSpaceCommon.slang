float linearViewDepth(float depth, float nearPlane, float farPlane)
{
    depth = 1.0 - depth;
    const float z = depth * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) /
           max(farPlane + nearPlane - z * (farPlane - nearPlane), 0.0001);
}

float3 decodeNormal(float3 encoded) { return normalize(encoded * 2.0 - 1.0); }

float edgeFade(float2 uv, float width)
{
    const float2 edge = min(uv, 1.0 - uv);
    return saturate(min(edge.x, edge.y) / max(width, 0.0001));
}
