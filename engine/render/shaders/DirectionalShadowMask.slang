// The directional shadow mask (render/src/DirectionalShadowMask.h): the directional light's filtered shadow at every
// pixel of the coverage depth, evaluated once per pixel by the same evaluateDirectionalShadow the surface shader
// would run. Prepassed shading pipelines then read one texel (kSurfaceShadowMask), so the filter's texture latency
// and registers leave the material shader, and thin geometry such as grass no longer filters on helper lanes.
//
// The receiver comes from the depth: its world position under the inverse of the jittered view-projection the
// coverage depth was rasterized with, its screen derivatives from the neighbour on the same surface (the nearer one
// in world space) on each axis, and its plane normal from those, turned to face the camera as a shaded side does.
#define PLUTO_STANDARD_MATERIAL 1
#include "BasicLit.slang"

// The coverage depth (reversed Z: 0 is the far plane, the sky).
[[vk::binding(6, 0)]] Sampler2D shadowMaskDepth : register(t6, space0);
[[vk::binding(9, 0)]] cbuffer DirectionalShadowMaskParameters : register(b9, space0)
{
    column_major float4x4 maskInverseClip; // inverse of the jittered view-projection of the coverage depth
    float4 maskResolution;                 // width, height, 1 / width, 1 / height
    float4 maskSettings;                   // x 1 when framebuffer rows run against NDC y (the Vulkan RHI's viewport)
};

float maskDepth(int2 pixel)
{
    return shadowMaskDepth.Load(int3(clamp(pixel, int2(0), int2(maskResolution.xy) - 1), 0)).r;
}

// Device depth is [0, 1] (the mask is Vulkan only).
float3 maskWorldPosition(int2 pixel, float depth)
{
    float2 ndc = (float2(pixel) + 0.5) * maskResolution.zw * 2.0 - 1.0;
    if (maskSettings.x > 0.5) ndc.y = -ndc.y;
    const float4 world = mul(maskInverseClip, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

// The receiver's world-position step to the next pixel along `axis`, as ddx/ddy give it on a triangle: from the
// neighbour after or before, whichever lies closer, so a step across a silhouette is not taken for the surface. A
// sliver (a distant grass blade) has neither neighbour on its surface: when even the closer step spans more than
// SliverSteps pixel footprints, the plane facing the view through this pixel stands in, rather than a step to a
// different surface (whose receiver-plane gradient would bias the filter wildly). Grazing surfaces up to about 85
// degrees stay below it.
static const float SliverSteps = 12.0;
float3 maskDerivative(int2 pixel, int2 axis, float3 world, float depth)
{
    const float3 after = maskWorldPosition(pixel + axis, maskDepth(pixel + axis)) - world;
    const float3 before = world - maskWorldPosition(pixel - axis, maskDepth(pixel - axis));
    const float3 nearer = dot(after, after) <= dot(before, before) ? after : before;
    const float3 facing = maskWorldPosition(pixel + axis, depth) - world;
    return dot(nearer, nearer) > SliverSteps * SliverSteps * dot(facing, facing) ? facing : nearer;
}

struct ShadowMaskVertex
{
    float4 position : SV_Position;
};

[shader("vertex")]
ShadowMaskVertex shadowMaskVertex(uint id : SV_VertexID)
{
    const float2 uv = float2(float((id << 1u) & 2u), float(id & 2u));
    ShadowMaskVertex output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 shadowMaskFragment(ShadowMaskVertex input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    const float depth = maskDepth(pixel);
    // Sky, shadows off, or the geometry diagnostic that bypasses directional receivers (shadeSurface's test).
    if (depth <= 0.0 || shadowsEnabled == 0 || pointParameters.w == 2.0)
        return 1.0;
    const float3 world = maskWorldPosition(pixel, depth);
    const float3 worldDx = maskDerivative(pixel, int2(1, 0), world, depth);
    const float3 worldDy = maskDerivative(pixel, int2(0, 1), world, depth);
    const float3 toCamera = cameraPositionAmbient.xyz - world;
    float3 normal = cross(worldDx, worldDy);
    const float normalLength = dot(normal, normal);
    normal = normalLength > 1.e-12 ? normal * rsqrt(normalLength) : normalize(toCamera);
    if (dot(normal, toCamera) < 0.0)
        normal = -normal;
    const float3 lightDirection = normalize(-directionalDirectionIntensity.xyz);
    return evaluateDirectionalShadow(world, normal, worldDx, worldDy, input.position.xy, lightDirection).visibility;
}
