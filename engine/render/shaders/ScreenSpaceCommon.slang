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

float3 reconstructWorldPosition(float2 uv, float depth, float4x4 inverseViewProjection,
                                uint flipY, uint zeroToOneDepth)
{
    float2 ndc = uv * 2.0 - 1.0;
    if (flipY != 0) ndc.y = -ndc.y;
    const float ndcDepth = zeroToOneDepth != 0 ? depth : depth * 2.0 - 1.0;
    const float4 world = mul(inverseViewProjection, float4(ndc, ndcDepth, 1.0));
    const float safeW = abs(world.w) > 0.000001 ? world.w
                                                : (world.w < 0.0 ? -0.000001 : 0.000001);
    return world.xyz / safeW;
}

float3 reconstructWorldRay(float2 uv, float4x4 view, float4x4 projection, uint flipY)
{
    float2 ndc = uv * 2.0 - 1.0;
    if (flipY != 0) ndc.y = -ndc.y;
    // Derive a perspective ray from the projection scale and rotate it back
    // with the camera view basis. This avoids unprojecting exactly at the
    // reverse-Z far plane, which collapses to a nearly constant direction on
    // some OpenGL GLSL drivers after Slang's matrix-layout translation.
    const float2 projectionScale = max(abs(float2(projection[0][0], projection[1][1])), 0.000001);
    const float3 viewDirection = normalize(float3(ndc / projectionScale, -1.0));
    // Slang lowers column-major uniform matrices to row-major GLSL blocks.
    // Keep the multiplication side explicit per target: Vulkan consumes the
    // native Slang convention, while OpenGL consumes the transposed block.
    return normalize(flipY != 0
        ? mul(viewDirection, (float3x3)view)
        : mul((float3x3)view, viewDirection));
}

float3 transformWorldToLocalPosition(float3 position, float4x4 worldToLocal, uint flipY)
{
    return (flipY != 0
        ? mul(worldToLocal, float4(position, 1.0))
        : mul(float4(position, 1.0), worldToLocal)).xyz;
}

float3 transformWorldToLocalDirection(float3 direction, float4x4 worldToLocal, uint flipY)
{
    return (flipY != 0
        ? mul(worldToLocal, float4(direction, 0.0))
        : mul(float4(direction, 0.0), worldToLocal)).xyz;
}
