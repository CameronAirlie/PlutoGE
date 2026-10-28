// Screen-space global illumination (ScreenSpaceGlobalIllumination). At the surface guide's resolution unless noted:
//
//   traceMain      cosine-weighted rays per pixel (1-4), marched against the depth buffer. A ray that hits an
//                  on-screen surface gathers its lit colour; one that misses or leaves the screen gathers nothing and
//                  leaves that share of the hemisphere to the existing ambient. Output: rgb = mean gathered radiance
//                  over all rays (hit fraction times the mean radiance of the hits), a = hit fraction.
//   filterMain     edge-aware 3 x 3 a-trous pass at stride parameters[5].w, guided by plane and normal agreement.
//   temporalMain   reprojects history along surface motion, rejects disocclusions against the previous guide,
//                  clamps it to the current neighbourhood and accumulates.
//   compositeMain  full resolution: bilateral upsample against the guide, then replaces the hit share of the ambient
//                  irradiance the indirect target holds (the geometry pass's, or DDGI's):
//                      indirect' = indirect + intensity * (response * gathered - hitFraction * indirect)
//                  and moves scene colour by the same amount. Unlit and glass pixels (indirect alpha zero) are left
//                  alone. Writes scene colour and the new indirect term.
//
// Lanes: 0 = intensity, ray distance, thickness, history weight; 1 = rays per pixel, debug view (1 indirect,
// 2 hit fraction), history depth tolerance (fraction of depth), history normal threshold; 5 = trace size, frame
// index, filter stride; 6.x = history valid, 6.y = previous guide valid. Quality is the march step count.
// worldToLocal: current view space to the previous frame's unjittered clip space.
#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "ScreenSpaceGuideCommon.slang"
#include "DiffuseResponse.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D inputTexture : register(t1, space0);
[[vk::binding(2, 0)]] [format("rgba16f")] RWTexture2D<float4> rayOutput : register(u2, space0);
[[vk::binding(3, 0)]] Sampler2D guideTexture : register(t3, space0);
[[vk::binding(4, 0)]] Sampler2D depthTexture : register(t4, space0);
[[vk::binding(5, 0)]] Sampler2D normalTexture : register(t5, space0);
[[vk::binding(6, 0)]] Sampler2D materialTexture : register(t6, space0);
[[vk::binding(7, 0)]] Sampler2D motionTexture : register(t7, space0);
[[vk::binding(8, 0)]] Sampler2D sceneTexture : register(t8, space0);
[[vk::binding(9, 0)]] Sampler2D albedoTexture : register(t9, space0);
[[vk::binding(10, 0)]] Sampler2D indirectTexture : register(t10, space0);
[[vk::binding(13, 0)]] Sampler2D historyTexture : register(t13, space0);
[[vk::binding(14, 0)]] Sampler2D previousGuideTexture : register(t14, space0);

#include "SSRCommon.slang"

static const float Pi = 3.141592654;

int2 traceSize() { return int2(pass.parameters[5].xy); }
int2 fullSize() { return int2(round(1.0 / pass.inverseResolution)); }
float intensity() { return max(pass.parameters[0].x, 0.0); }
float thickness() { return max(pass.parameters[0].z, 0.01); }

struct Surface
{
    float3 position, normal; // view space
    bool valid;
};

Surface loadSurface(Sampler2D source, int2 pixel)
{
    const GuideSurface guide = decodeGuide(source.Load(int3(pixel, 0)));
    Surface surface;
    surface.valid = guide.depth > 0.0;
    surface.position = viewPositionFromLinearDepth(guideRepresentativeUv(pixel, traceSize(), fullSize()), guide.depth,
                                                   pass);
    surface.normal = normalize(mul((float3x3)pass.view, guide.normal));
    return surface;
}

// Cosine-weighted direction about `normal` for the stratified sample u in [0, 1)^2.
float3 cosineDirection(float3 normal, float2 u)
{
    const float3 tangent = normalize(cross(abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0), normal));
    const float3 bitangent = cross(normal, tangent);
    const float radius = sqrt(u.x);
    const float phi = 2.0 * Pi * u.y;
    return normalize(tangent * (radius * cos(phi)) + bitangent * (radius * sin(phi)) +
                     normal * sqrt(max(1.0 - u.x, 0.0)));
}

// One ray: the gathered radiance weighted by its on-screen confidence (rgb) and that confidence (a).
float4 traceRay(Surface surface, float3 ray)
{
    const float start = max(0.01 * -surface.position.z, 0.01);
    const float distance = max(pass.parameters[0].y, start * 2.0);
    const float3 origin = surface.position + surface.normal * start;
    const float4 clipOrigin = mul(pass.projection, float4(origin, 1.0));
    const float4 clipRay = mul(pass.projection, float4(ray, 0.0));
    float low, high, sceneDepth;
    float2 uv;
    if (!firstCrossing(clipOrigin, clipRay, start, distance, clamp(pass.quality, 4u, 64u), low, high, uv, sceneDepth))
        return 0.0;
    for (uint refinement = 0; refinement < 3; ++refinement)
    {
        const float middle = (low + high) * 0.5;
        float2 middleUv;
        float middleDepth, middleSceneDepth;
        if (!projectRay(clipOrigin + clipRay * middle, middleUv, middleDepth)) break;
        if (behindSurface(middleUv, middleDepth, middleSceneDepth))
        {
            high = middle;
            uv = middleUv;
            sceneDepth = middleSceneDepth;
        }
        else low = middle;
    }
    const float4x4 clipToView = mul(pass.view, pass.inverseViewProjection);
    const float rayDepth = -(origin.z + ray.z * high);
    const float delta = depthDelta(uv, sceneDepth, rayDepth, clipToView[2], pass.inverseViewProjection[3]);
    // Behind a thin surface rather than on it, or arriving at its back: light cannot pass.
    const float3 hitNormal = mul((float3x3)pass.view, decodeNormal(normalTexture.SampleLevel(uv, 0.0).xyz));
    if (delta < 0.0 || delta > thickness() || dot(ray, hitNormal) > 0.0)
        return 0.0;
    const float confidence = edgeFade(uv, 0.1);
    return float4(sceneTexture.SampleLevel(uv, 0.0).rgb * confidence, confidence);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void traceMain(uint3 id : SV_DispatchThreadID)
{
    const int2 pixel = int2(id.xy);
    if (any(pixel >= traceSize())) return;
    const Surface surface = loadSurface(guideTexture, pixel);
    if (!surface.valid || intensity() <= 0.0)
    {
        rayOutput[pixel] = 0.0;
        return;
    }
    const uint rays = clamp(uint(pass.parameters[1].x), 1u, 4u);
    // A stable per-pixel rotation of an R2 sequence over frames and rays.
    const float2 rotation = reflectionSampleRotation(float2(pixel));
    float4 gathered = 0.0;
    for (uint index = 0; index < rays; ++index)
    {
        const float sample = pass.parameters[5].z * float(rays) + float(index);
        const float2 u = frac(rotation + sample * float2(0.754877666, 0.569840291));
        gathered += traceRay(surface, cosineDirection(surface.normal, u));
    }
    rayOutput[pixel] = gathered / float(rays);
}

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

// Agreement of a neighbouring guide texel with the receiver: on its plane and facing the same way.
float geometryWeight(Surface receiver, Surface neighbour)
{
    if (!neighbour.valid) return 0.0;
    const float tolerance = thickness() * 0.5 + 0.02 * -receiver.position.z;
    const float plane = saturate(1.0 - abs(dot(neighbour.position - receiver.position, receiver.normal)) / tolerance);
    return plane * pow(saturate(dot(receiver.normal, neighbour.normal)), 16.0);
}

[shader("fragment")]
float4 filterMain(FullscreenVertexOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    const Surface receiver = loadSurface(guideTexture, pixel);
    if (!receiver.valid) return 0.0;
    const int stride = max(int(pass.parameters[5].w), 1);
    const float kernel[3] = {0.25, 0.5, 0.25};
    float4 sum = 0.0;
    float weightSum = 0.0;
    [ForceUnroll] for (int y = -1; y <= 1; ++y)
        [ForceUnroll] for (int x = -1; x <= 1; ++x)
        {
            const int2 tap = pixel + int2(x, y) * stride;
            if (any(tap < 0) || any(tap >= traceSize())) continue;
            const float weight = kernel[x + 1] * kernel[y + 1] *
                                 (x == 0 && y == 0 ? 1.0 : geometryWeight(receiver, loadSurface(guideTexture, tap)));
            sum += inputTexture.Load(int3(tap, 0)) * weight;
            weightSum += weight;
        }
    return sum / max(weightSum, 0.000001);
}

[shader("fragment")]
float4 temporalMain(FullscreenVertexOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    const Surface receiver = loadSurface(guideTexture, pixel);
    if (!receiver.valid) return 0.0;
    const float4 current = inputTexture.Load(int3(pixel, 0));
    if (pass.parameters[6].x < 0.5) return current;
    // Neighbourhood moments bound how far history may differ from what this frame supports.
    float4 mean = 0.0, meanSquare = 0.0;
    [ForceUnroll] for (int y = -1; y <= 1; ++y)
        [ForceUnroll] for (int x = -1; x <= 1; ++x)
        {
            const float4 value = inputTexture.Load(int3(clamp(pixel + int2(x, y), int2(0), traceSize() - 1), 0));
            mean += value;
            meanSquare += value * value;
        }
    mean /= 9.0;
    const float4 deviation = sqrt(max(meanSquare / 9.0 - mean * mean, 0.0));
    const float2 uv = (float2(pixel) + 0.5) / float2(traceSize());
    const int2 surfacePixel = guideRepresentativePixel(pixel, traceSize(), fullSize());
    const float2 orientation = float2(1.0, pass.flipY != 0 ? -1.0 : 1.0);
    const float2 previousUv = uv - motionTexture.Load(int3(surfacePixel, 0)).xy * orientation;
    if (any(previousUv < 0.0) || any(previousUv > 1.0)) return current;
    // The surface must have been visible there last frame.
    if (pass.parameters[6].y > 0.5)
    {
        const int2 previousPixel = clamp(int2(previousUv * float2(traceSize())), int2(0), traceSize() - 1);
        const GuideSurface previous = decodeGuide(previousGuideTexture.Load(int3(previousPixel, 0)));
        const float4 previousClip = mul(pass.worldToLocal, float4(receiver.position, 1.0));
        const float expectedDepth = abs(pass.projection[3][2]) > 0.000001 ? previousClip.w : previous.depth;
        const float3 worldNormal = mul(receiver.normal, (float3x3)pass.view);
        if (previous.depth <= 0.0 || abs(previous.depth - expectedDepth) > pass.parameters[1].z * expectedDepth ||
            dot(previous.normal, worldNormal) < pass.parameters[1].w)
            return current;
    }
    const float4 history = clamp(historyTexture.SampleLevel(previousUv, 0.0), mean - 1.5 * deviation,
                                 mean + 1.5 * deviation);
    return lerp(current, history, saturate(pass.parameters[0].w));
}

struct CompositeOutput
{
    float4 color : SV_Target0;
    float4 indirect : SV_Target1;
};

[shader("fragment")]
CompositeOutput compositeMain(FullscreenVertexOutput input)
{
    const int2 pixel = int2(input.position.xy);
    CompositeOutput output;
    output.color = sceneTexture.Load(int3(pixel, 0));
    output.indirect = indirectTexture.Load(int3(pixel, 0));
    const uint debugView = uint(pass.parameters[1].y);
    const float depth = linearViewDepth(depthTexture.Load(int3(pixel, 0)).r, pass.projection, pass.zeroToOneDepth);
    if (depth <= 0.0 || output.indirect.a < 0.5)
    {
        if (debugView != 0) output.color = float4(0.0, 0.0, 0.0, 1.0);
        return output;
    }
    const float2 uv = (float2(pixel) + 0.5) * pass.inverseResolution;
    const float3 position = viewPositionFromLinearDepth(uv, depth, pass);
    const float3 normal = decodeNormal(normalTexture.Load(int3(pixel, 0)).xyz);
    Surface receiver;
    receiver.position = position;
    receiver.normal = mul((float3x3)pass.view, normal);
    receiver.valid = true;
    const float2 coordinate = uv * float2(traceSize()) - 0.5;
    const int2 base = int2(floor(coordinate));
    const float2 fraction = frac(coordinate);
    float4 gathered = 0.0;
    float weightSum = 0.0;
    [ForceUnroll] for (int y = 0; y < 2; ++y)
        [ForceUnroll] for (int x = 0; x < 2; ++x)
        {
            const int2 texel = clamp(base + int2(x, y), int2(0), traceSize() - 1);
            const float2 bilinear = lerp(1.0 - fraction, fraction, float2(x, y));
            const float weight = bilinear.x * bilinear.y * geometryWeight(receiver, loadSurface(guideTexture, texel));
            gathered += inputTexture.Load(int3(texel, 0)) * weight;
            weightSum += weight;
        }
    // Silhouettes without a matching reduced texel keep the ambient they have.
    gathered = weightSum > 0.0001 ? gathered / weightSum : 0.0;
    const float3 viewDirection = -normalize(position);
    const float2 metallicRoughness = materialTexture.Load(int3(pixel, 0)).xy;
    const float3 response = ambientDiffuseResponse(albedoTexture.Load(int3(pixel, 0)).rgb, saturate(metallicRoughness.x),
        saturate(metallicRoughness.y), receiver.normal, viewDirection);
    const float3 indirect = max(output.indirect.rgb +
        intensity() * (response * gathered.rgb - saturate(gathered.a) * output.indirect.rgb), 0.0);
    if (debugView == 1)
        output.color = float4(indirect, 1.0);
    else if (debugView == 2)
        output.color = float4(saturate(gathered.aaa), 1.0);
    else
        output.color.rgb = max(output.color.rgb + indirect - output.indirect.rgb, 0.0);
    output.indirect.rgb = indirect;
    return output;
}
