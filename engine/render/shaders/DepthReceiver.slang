// Surface receivers rebuilt from the coverage depth, for screen passes that light or shadow what the geometry pass drew
// (DirectionalShadowMask.slang, DeferredLocalLights.slang): a pixel's world position under the inverse of the jittered
// view-projection the depth was rasterized with, its screen derivatives from the neighbour on the same surface (the
// nearer one in world space) on each axis, and its plane normal from those, turned to face the camera as a shaded side
// does. Device depth is [0, 1] (these passes are Vulkan only); reversed Z, so 0 is the far plane.
#ifndef PLUTO_DEPTH_RECEIVER_SLANG
#define PLUTO_DEPTH_RECEIVER_SLANG

[[vk::binding(6, 0)]] Sampler2D receiverDepthTexture : register(t6, space0);
[[vk::binding(9, 0)]] cbuffer DepthReceiverParameters : register(b9, space0)
{
    column_major float4x4 receiverInverseClip; // inverse of the jittered view-projection of the coverage depth
    float4 receiverResolution;                 // width, height, 1 / width, 1 / height
    float4 receiverSettings;                   // x 1 when framebuffer rows run against NDC y (the Vulkan RHI's viewport)
};

float receiverDepth(int2 pixel)
{
    return receiverDepthTexture.Load(int3(clamp(pixel, int2(0), int2(receiverResolution.xy) - 1), 0)).r;
}

float3 receiverWorldPosition(int2 pixel, float depth)
{
    float2 ndc = (float2(pixel) + 0.5) * receiverResolution.zw * 2.0 - 1.0;
    if (receiverSettings.x > 0.5) ndc.y = -ndc.y;
    const float4 world = mul(receiverInverseClip, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

// The receiver's world-position step to the next pixel along `axis`, as ddx/ddy give it on a triangle: from the
// neighbour after or before, whichever lies closer, so a step across a silhouette is not taken for the surface. A
// sliver (a distant grass blade) has neither neighbour on its surface: when even the closer step spans more than
// ReceiverSliverSteps pixel footprints, the plane facing the view through this pixel stands in, rather than a step to a
// different surface (whose receiver-plane gradient would bias a filter wildly).
//
// Length alone cannot tell a sliver from ground seen at a grazing angle, whose steps also span many footprints (beyond
// about 85 degrees from the view, which a camera at eye height reaches a few tens of metres out). Taking the facing
// plane there turned the ground's receiver towards the camera, and the wrong bias and filter plane shadowed it in a
// dark band. A row or column of pixels meets a plane along a line, so steps on one surface continue in one direction
// (nearly, on a curved one) even as perspective stretches them, while a sliver's two steps leave and return to the
// surface behind it and point in nearly opposite directions: steps within about 18 degrees of each other
// (ReceiverStepAlignment) are one surface, however long.
//
// At the screen edge, or beside the sky, the neighbour outside has no surface, so the step inside stands alone.
static const float ReceiverSliverSteps = 12.0;
static const float ReceiverStepAlignment = 0.95;
float3 receiverDerivative(int2 pixel, int2 axis, float3 world, float depth)
{
    const int2 next = pixel + axis, previous = pixel - axis;
    const float nextDepth = receiverDepth(next), previousDepth = receiverDepth(previous);
    // A sky neighbour (depth 0) has no surface to step to; under an orthographic projection it would even lie close by.
    const bool hasNext = all(next < int2(receiverResolution.xy)) && nextDepth > 0.0;
    const bool hasPrevious = all(previous >= 0) && previousDepth > 0.0;
    const float3 after = receiverWorldPosition(next, nextDepth) - world;
    const float3 before = world - receiverWorldPosition(previous, previousDepth);
    if (!hasNext && !hasPrevious)
        return receiverWorldPosition(pixel + axis, depth) - world;
    if (!hasNext || !hasPrevious)
        return hasNext ? after : before;
    const float afterLength = dot(after, after), beforeLength = dot(before, before);
    const float3 nearer = afterLength <= beforeLength ? after : before;
    const float alignment = dot(after, before);
    if (alignment > 0.0 && alignment * alignment >= ReceiverStepAlignment * ReceiverStepAlignment * afterLength * beforeLength)
        return nearer;
    const float3 facing = receiverWorldPosition(next, depth) - world;
    return dot(nearer, nearer) > ReceiverSliverSteps * ReceiverSliverSteps * dot(facing, facing) ? facing : nearer;
}

struct DepthReceiver
{
    float3 world;
    float3 worldDx, worldDy;
    float3 planeNormal; // facing `toCamera`
};

// `depth` must be a surface's (above zero). With `quadDerivatives`, the screen derivatives are those of the pixel's 2x2
// quad origin when it lies on a surface, as a fragment quad's ddx and ddy are the same for its four pixels: shadow
// lookups that select levels or filter planes from the derivatives (spot shadows) then change where the forward
// path's do, rather than a pixel apart.
DepthReceiver depthReceiver(int2 pixel, float depth, float3 cameraPosition, bool quadDerivatives = false)
{
    DepthReceiver receiver;
    receiver.world = receiverWorldPosition(pixel, depth);
    int2 origin = pixel;
    float originDepth = depth;
    if (quadDerivatives)
    {
        const int2 quad = pixel & ~1;
        const float quadDepth = receiverDepth(quad);
        if (quadDepth > 0.0)
        {
            origin = quad;
            originDepth = quadDepth;
        }
    }
    const float3 originWorld = all(origin == pixel) ? receiver.world : receiverWorldPosition(origin, originDepth);
    receiver.worldDx = receiverDerivative(origin, int2(1, 0), originWorld, originDepth);
    receiver.worldDy = receiverDerivative(origin, int2(0, 1), originWorld, originDepth);
    const float3 toCamera = cameraPosition - receiver.world;
    float3 normal = cross(receiver.worldDx, receiver.worldDy);
    const float normalLength = dot(normal, normal);
    normal = normalLength > 1.e-12 ? normal * rsqrt(normalLength) : normalize(toCamera);
    receiver.planeNormal = dot(normal, toCamera) < 0.0 ? -normal : normal;
    return receiver;
}

struct ScreenTriangleVertex
{
    float4 position : SV_Position;
};

ScreenTriangleVertex screenTriangleVertex(uint id)
{
    const float2 uv = float2(float((id << 1u) & 2u), float(id & 2u));
    ScreenTriangleVertex output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

#endif
