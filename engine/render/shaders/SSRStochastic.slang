// Stochastic screen-space reflections (see ScreenSpaceReflections.h), after Stachowiak, "Stochastic Screen-Space
// Reflections" (2015) and AMD FidelityFX SSSR. At trace resolution (half of the output in each axis), unless noted:
//
//   traceMain      one GGX visible-normal ray per reflective pixel, marched against the depth buffer.
//   reuseMain      each pixel re-weights its own and neighbours' rays by its BRDF over their sampling pdf (ray
//                  reuse) and forms the signed lighting correction SSR.slang uses, so a constant environment stays
//                  exact.
//   temporalMain   reprojects accumulated history along surface motion or reflection parallax, clipped to the
//                  current neighbourhood, and accumulates.
//   compositeMain  full resolution: bilateral upsample against the guide, added to the scene.
//
// Every stage works in view space at the guide's representative pixels (ScreenSpaceGuideCommon.slang).
// Effect lanes 0..3 match SSR.slang. parameters[5]: xy = trace size, z = frame index.
// parameters[6]: x = history valid, y = previous guide valid.
// worldToLocal: current view space to the previous frame's unjittered clip space.
//
// Tile classification feeding an indirect trace dispatch was measured and removed: with one ray per pixel, pixels
// without a ray leave the trace almost at once, so the classification pass cost more than it saved
// (docs/screen-space-effects-optimisation.md).
#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "ScreenSpaceGuideCommon.slang"
#include "EnvironmentBRDF.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(2, 0)]] [format("rgba16f")] RWTexture2D<float4> rayOutput : register(u2, space0);
[[vk::binding(3, 0)]] Sampler2D guideTexture : register(t3, space0);
[[vk::binding(4, 0)]] Sampler2D depthTexture : register(t4, space0);
[[vk::binding(5, 0)]] Sampler2D normalTexture : register(t5, space0);
[[vk::binding(6, 0)]] Sampler2D materialTexture : register(t6, space0);
[[vk::binding(7, 0)]] Sampler2D motionTexture : register(t7, space0);
[[vk::binding(8, 0)]] Sampler2D sceneTexture : register(t8, space0);
[[vk::binding(9, 0)]] Sampler2D albedoTexture : register(t9, space0);
[[vk::binding(10, 0)]] Sampler2D environmentSpecularTexture : register(t10, space0);
[[vk::binding(11, 0)]] Sampler2D rayTexture : register(t11, space0);
[[vk::binding(12, 0)]] Sampler2D resolvedTexture : register(t12, space0);
[[vk::binding(13, 0)]] Sampler2D historyTexture : register(t13, space0);
[[vk::binding(14, 0)]] Sampler2D previousGuideTexture : register(t14, space0);

#include "SSRCommon.slang"

static const float Pi = 3.141592654;

int2 traceSize() { return int2(pass.parameters[5].xy); }
int2 fullSize() { return int2(round(1.0 / pass.inverseResolution)); }

// The receiver a trace pixel describes, in view space.
struct Receiver
{
    float3 position, normal, view;
    float roughness, ndotv;
    // Below the roughness cutoff, in front of the camera and with a ray interval to march.
    bool traced;
};

float reflectionRoughnessFade(float roughness)
{
    const float maxRoughness = pass.parameters[2].x > 0.0 ? min(pass.parameters[2].x, 1.0) : 1.0;
    return (1.0 - roughness) *
        (maxRoughness < 1.0 ? 1.0 - smoothstep(maxRoughness - 0.15, maxRoughness, roughness) : 1.0);
}

Receiver loadReceiver(Sampler2D guideSource, int2 pixel)
{
    const GuideSurface surface = decodeGuide(guideSource.Load(int3(pixel, 0)));
    Receiver receiver;
    receiver.roughness = clamp(surface.roughness, 0.04, 1.0);
    receiver.position = viewPositionFromLinearDepth(guideRepresentativeUv(pixel, traceSize(), fullSize()),
                                                    surface.depth, pass);
    receiver.normal = normalize(mul((float3x3)pass.view, surface.normal));
    receiver.view = -normalize(receiver.position);
    receiver.ndotv = dot(receiver.normal, receiver.view);
    const float start = max(pass.parameters[0].w, 0.001);
    receiver.traced = surface.depth > 0.0 && pass.parameters[0].x > 0.0 && pass.parameters[0].y > start &&
                      reflectionRoughnessFade(receiver.roughness) > 0.0 && receiver.ndotv > 0.0;
    return receiver;
}

// The share of GGX's long tail that is never sampled. With one ray per pixel, the rare far-off-lobe directions
// flip between hit and miss from frame to frame and dominate the residual noise; trimming them (as Frostbite's
// stochastic SSR does) slightly narrows rough reflections. The constant pdf factor cancels in reuseMain's ratio.
static const float LobeTailTrim = 0.1;

// Per-pixel, per-frame sample of the visible-normal distribution (R2 sequence over frames).
float2 lobeSample(int2 pixel)
{
    const float2 rotation = reflectionSampleRotation(float2(pixel));
    const float2 u = frac(rotation + pass.parameters[5].z * float2(0.754877666, 0.569840291));
    return float2(u.x * (1.0 - LobeTailTrim), u.y);
}

// Output texel: octahedral view-space direction, hit travel (negative for a miss), 1. Zero: no ray.
[shader("compute")]
[numthreads(8, 8, 1)]
void traceMain(uint3 id : SV_DispatchThreadID)
{
    const int2 pixel = int2(id.xy);
    if (any(pixel >= traceSize())) return;
    const Receiver receiver = loadReceiver(guideTexture, pixel);
    if (!receiver.traced)
    {
        rayOutput[pixel] = 0.0;
        return;
    }
    const float alpha = receiver.roughness * receiver.roughness;
    const ReflectionLobe lobe = makeReflectionLobe(receiver.normal, receiver.view, receiver.ndotv, alpha);
    const float2 u = lobeSample(pixel);
    const float3 ray = reflect(-receiver.view, lobe.halfVector(u.x, 6.283185307 * u.y));
    if (dot(receiver.normal, ray) <= 0.0)
    {
        // Below the horizon: no sample, as the reference path discards it.
        rayOutput[pixel] = 0.0;
        return;
    }
    const float start = max(pass.parameters[0].w, 0.001);
    const float distance = pass.parameters[0].y;
    const float thickness = max(pass.parameters[0].z, 0.001);
    const float3 origin = receiver.position + receiver.normal * start;
    const float4 clipOrigin = mul(pass.projection, float4(origin, 1.0));
    const float4 clipRay = mul(pass.projection, float4(ray, 0.0));
    const float4x4 clipToView = mul(pass.view, pass.inverseViewProjection);
    float travel = -1.0;
    float low, high;
    float2 rayUv;
    float hitDepth;
    if (firstCrossing(clipOrigin, clipRay, start, distance, clamp(pass.quality, 8u, 128u), low, high, rayUv, hitDepth))
    {
        for (uint j = 0; j < min(uint(max(pass.parameters[1].w, 0.0)), 8u); ++j)
        {
            const float middle = (low + high) * 0.5;
            float2 middleUv;
            float middleDepth;
            if (!projectRay(clipOrigin + clipRay * middle, middleUv, middleDepth)) break;
            float middleSceneDepth;
            if (behindSurface(middleUv, middleDepth, middleSceneDepth))
            {
                high = middle;
                rayUv = middleUv;
                hitDepth = middleSceneDepth;
            }
            else low = middle;
        }
        const float rayDepth = -(origin.z + ray.z * high);
        const float delta = depthDelta(rayUv, hitDepth, rayDepth, clipToView[2], pass.inverseViewProjection[3]);
        const float3 hitNormal = mul((float3x3)pass.view, decodeNormal(normalTexture.SampleLevel(rayUv, 0.0).xyz));
        if (delta >= 0.0 && delta <= thickness && dot(ray, hitNormal) < -0.01)
            travel = high;
    }
    rayOutput[pixel] = float4(encodeOctahedral(ray), travel, 1.0);
}

float ggxDistribution(float ndoth, float alphaSquared)
{
    const float denominator = ndoth * ndoth * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(Pi * denominator * denominator, 0.0000001);
}

// Neighbours whose rays a pixel reuses, in trace pixels: the orthogonal four on even frames and the diagonal four
// on odd ones, so temporal accumulation covers all eight at half the cost. Rough lobes reach twice as far.
static const int ReuseNeighbours = 4;
static const int2 ReuseOffsets[2][ReuseNeighbours] = {
    {int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1)}, {int2(1, 1), int2(-1, -1), int2(1, -1), int2(-1, 1)}};

// Output: signed lighting correction (rgb) and the BRDF-weighted mean hit distance (zero without hits).
[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

[shader("fragment")]
float4 reuseMain(FullscreenVertexOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    const Receiver receiver = loadReceiver(guideTexture, pixel);
    if (!receiver.traced) return 0.0;
    const int2 surfacePixel = guideRepresentativePixel(pixel, traceSize(), fullSize());
    const float metallic = saturate(materialTexture.Load(int3(surfacePixel, 0)).r);
    const float3 f0 = lerp(float3(0.04), albedoTexture.Load(int3(surfacePixel, 0)).rgb, metallic);
    const float alpha = receiver.roughness * receiver.roughness;
    const float a2 = alpha * alpha;
    const float start = max(pass.parameters[0].w, 0.001);
    const float thickness = max(pass.parameters[0].z, 0.01);
    const int stride = receiver.roughness > 0.35 ? 2 : 1;
    const uint pattern = uint(pass.parameters[5].z) & 1u;

    float3 reflection = 0.0, totalResponse = 0.0, coveredResponse = 0.0;
    float hitDistance = 0.0, hitWeight = 0.0;
    for (int index = -1; index < ReuseNeighbours; ++index)
    {
        const bool own = index < 0;
        const int2 source = pixel + (own ? int2(0) : ReuseOffsets[pattern][max(index, 0)] * stride);
        if (any(source < 0) || any(source >= traceSize())) continue;
        const float4 rayData = rayTexture.Load(int3(source, 0));
        if (rayData.w < 0.5) continue;
        const Receiver sender = own ? receiver : loadReceiver(guideTexture, source);
        float similarity = 1.0;
        if (!own)
        {
            // Reuse only rays leaving the same surface: they share the receiver's visibility.
            const float planeDistance = abs(dot(sender.position - receiver.position, receiver.normal));
            similarity = saturate(1.0 - planeDistance / thickness) *
                         pow(saturate(dot(receiver.normal, sender.normal)), 32.0);
            if (similarity <= 0.0) continue;
        }
        const float3 senderRay = decodeOctahedral(rayData.xy);
        const bool hit = rayData.z >= 0.0;
        const float3 hitPoint = sender.position + sender.normal * start + senderRay * rayData.z;
        const float3 ray = own || !hit ? senderRay : normalize(hitPoint - receiver.position);
        const float ndotl = dot(receiver.normal, ray);
        if (ndotl <= 0.0) continue;
        // The sender's visible-normal pdf of its ray: G1(V) D(H) / (4 N.V).
        const float senderAlpha = sender.roughness * sender.roughness;
        const float3 senderHalf = normalize(sender.view + senderRay);
        const float senderPdf = ggxDistribution(saturate(dot(sender.normal, senderHalf)), senderAlpha * senderAlpha) *
            smithG1(sender.ndotv, senderAlpha * senderAlpha) / (4.0 * sender.ndotv);
        // The receiver's BRDF times cosine without Fresnel: D(H) G1(V) G1(L) / (4 N.V). For its own ray the
        // ratio reduces to G1(L), the reference path's per-ray weight.
        const float3 halfVector = normalize(receiver.view + ray);
        const float brdf = ggxDistribution(saturate(dot(receiver.normal, halfVector)), a2) *
            smithG1(receiver.ndotv, a2) * smithG1(ndotl, a2) / (4.0 * receiver.ndotv);
        const float weight = similarity * brdf / max(senderPdf, 0.000001);
        if (!(weight > 0.0) || !isfinite(weight)) continue;
        const float vdoth = saturate(dot(receiver.view, halfVector));
        const float3 response = (f0 + (1.0 - f0) * pow(1.0 - vdoth, 5.0)) * weight;
        // Misses stay in the denominator, as in the reference path.
        totalResponse += response;
        if (!hit) continue;
        float2 hitUv;
        if (!projectPoint(mul(pass.projection, float4(hitPoint, 1.0)), hitUv) || any(hitUv < 0.0) || any(hitUv > 1.0))
            continue;
        const float confidence = edgeFade(hitUv, pass.parameters[1].x);
        reflection += sceneTexture.SampleLevel(hitUv, 0.0).rgb * response * confidence;
        coveredResponse += response * confidence;
        hitDistance += length(hitPoint - receiver.position) * weight;
        hitWeight += weight;
    }
    if (all(totalResponse <= 0.0)) return 0.0;
    const float3 inverseResponse = 1.0 / max(totalResponse, float3(0.000001));
    const float2 surfaceUv = (float2(surfacePixel) + 0.5) * pass.inverseResolution;
    const float3 fallback = environmentSpecularTexture.SampleLevel(surfaceUv, 0.0).rgb;
    const float3 coveredFraction = saturate(coveredResponse * inverseResponse);
    const float3 reflected = reflection * inverseResponse * iblSpecularResponse(f0, receiver.roughness, receiver.ndotv);
    const float3 correction = saturate(pass.parameters[0].x) * (reflected - fallback * coveredFraction);
    return float4(correction, hitWeight > 0.0 ? hitDistance / hitWeight : 0.0);
}

float luminance(float3 color) { return dot(color, float3(0.2126, 0.7152, 0.0722)); }

// Output: accumulated correction (rgb) and the number of frames it holds.
[shader("fragment")]
float4 temporalMain(FullscreenVertexOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    const Receiver receiver = loadReceiver(guideTexture, pixel);
    if (!receiver.traced) return 0.0;
    const float4 current = resolvedTexture.Load(int3(pixel, 0));
    // Neighbourhood moments bound how far history may differ from what this frame can support.
    float3 mean = 0.0, meanSquare = 0.0;
    [ForceUnroll] for (int y = -1; y <= 1; ++y)
        [ForceUnroll] for (int x = -1; x <= 1; ++x)
        {
            const float3 value = resolvedTexture.Load(int3(clamp(pixel + int2(x, y), int2(0), traceSize() - 1), 0)).rgb;
            mean += value;
            meanSquare += value * value;
        }
    mean /= 9.0;
    const float3 deviation = sqrt(max(meanSquare / 9.0 - mean * mean, 0.0));
    // Mirror-like reflections converge fastest and reproject accurately along their parallax; rough ones need the
    // longest history.
    const float maxFrames = lerp(8.0, 16.0, saturate(receiver.roughness / 0.3));
    if (pass.parameters[6].x < 0.5)
        return float4(current.rgb, 1.0);

    const float2 traceTexel = 1.0 / float2(traceSize());
    const float2 uv = (float2(pixel) + 0.5) * traceTexel;
    const int2 surfacePixel = guideRepresentativePixel(pixel, traceSize(), fullSize());
    const float2 orientation = float2(1.0, pass.flipY != 0 ? -1.0 : 1.0);
    const float2 surfaceUv = uv - motionTexture.Load(int3(surfacePixel, 0)).xy * orientation;
    // Reflected content moves with the virtual image behind the surface, at the hit distance.
    float2 reflectionUv = surfaceUv;
    if (current.a > 0.0)
    {
        const float3 virtualPoint = -receiver.view * (length(receiver.position) + current.a);
        const float4 previousClip = mul(pass.worldToLocal, float4(virtualPoint, 1.0));
        if (previousClip.w > 0.000001)
        {
            reflectionUv = previousClip.xy / previousClip.w;
            if (pass.flipY != 0) reflectionUv.y = -reflectionUv.y;
            reflectionUv = reflectionUv * 0.5 + 0.5;
        }
    }
    const bool surfaceInside = all(surfaceUv >= 0.0) && all(surfaceUv <= 1.0);
    const bool reflectionInside = all(reflectionUv >= 0.0) && all(reflectionUv <= 1.0);
    if (!surfaceInside && !reflectionInside)
        return float4(current.rgb, 1.0);
    // The surface must have been visible there last frame (disocclusion).
    if (pass.parameters[6].y > 0.5 && surfaceInside)
    {
        const int2 previousPixel = clamp(int2(surfaceUv * float2(traceSize())), int2(0), traceSize() - 1);
        const GuideSurface previous = decodeGuide(previousGuideTexture.Load(int3(previousPixel, 0)));
        const float4 previousClip = mul(pass.worldToLocal, float4(receiver.position, 1.0));
        const float expectedDepth = abs(pass.projection[3][2]) > 0.000001 ? previousClip.w : previous.depth;
        const float3 worldNormal = mul(receiver.normal, (float3x3)pass.view);
        if (previous.depth <= 0.0 || abs(previous.depth - expectedDepth) > 0.05 * expectedDepth ||
            dot(previous.normal, worldNormal) < 0.8)
            return float4(current.rgb, 1.0);
    }
    // Of the two reprojections, keep the history closer to what this frame sees (FidelityFX SSSR).
    const float4 surfaceHistory = surfaceInside ? historyTexture.SampleLevel(surfaceUv, 0.0) : float4(0.0);
    const float4 reflectionHistory = reflectionInside ? historyTexture.SampleLevel(reflectionUv, 0.0) : float4(0.0);
    const float surfaceError = surfaceInside ? abs(luminance(surfaceHistory.rgb - mean)) : 1e20;
    const float reflectionError = reflectionInside ? abs(luminance(reflectionHistory.rgb - mean)) : 1e20;
    const float4 history = reflectionError < surfaceError ? reflectionHistory : surfaceHistory;
    const float3 clipped = clamp(history.rgb, mean - 1.25 * deviation, mean + 1.25 * deviation);
    const float frames = min(max(history.a, 0.0) + 1.0, maxFrames);
    return float4(lerp(clipped, current.rgb, 1.0 / frames), frames);
}

// Full resolution: the receiving pixel's G-buffer against the four nearest trace texels, weighted bilinearly and
// by agreement in plane, normal and roughness (the guide replaces the reference composite's three G-buffer reads
// and position reconstruction per tap). Correction fades with roughness as in SSR.slang.
[shader("fragment")]
float4 compositeMain(FullscreenVertexOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    const float4 scene = sceneTexture.Load(int3(pixel, 0));
    const float depth = linearViewDepth(depthTexture.Load(int3(pixel, 0)).r, pass.projection, pass.zeroToOneDepth);
    if (depth <= 0.0 || pass.parameters[0].x <= 0.0) return scene;
    const float roughness = clamp(materialTexture.Load(int3(pixel, 0)).g, 0.04, 1.0);
    const float fade = reflectionRoughnessFade(roughness);
    if (fade <= 0.0) return scene;
    const float2 uv = (float2(pixel) + 0.5) * pass.inverseResolution;
    const float3 position = viewPositionFromLinearDepth(uv, depth, pass);
    const float3 normal = decodeNormal(normalTexture.Load(int3(pixel, 0)).xyz);
    const float3 viewNormal = mul((float3x3)pass.view, normal);
    const float thickness = max(pass.parameters[0].z, 0.01);
    const float2 coordinate = uv * float2(traceSize()) - 0.5;
    const int2 base = int2(floor(coordinate));
    const float2 fraction = frac(coordinate);
    float3 sum = 0.0;
    float weightSum = 0.0;
    [ForceUnroll] for (int y = 0; y < 2; ++y)
        [ForceUnroll] for (int x = 0; x < 2; ++x)
        {
            const int2 texel = clamp(base + int2(x, y), int2(0), traceSize() - 1);
            const GuideSurface tap = decodeGuide(guideTexture.Load(int3(texel, 0)));
            if (tap.depth <= 0.0) continue;
            const float3 tapPosition = viewPositionFromLinearDepth(
                guideRepresentativeUv(texel, traceSize(), fullSize()), tap.depth, pass);
            const float planeWeight = saturate(1.0 - abs(dot(tapPosition - position, viewNormal)) / thickness);
            const float normalWeight = pow(saturate(dot(normal, tap.normal)), 32.0);
            const float roughnessWeight = saturate(1.0 - 8.0 * abs(tap.roughness - roughness));
            const float2 bilinear = lerp(1.0 - fraction, fraction, float2(x, y));
            const float weight = bilinear.x * bilinear.y * planeWeight * normalWeight * roughnessWeight;
            sum += historyTexture.Load(int3(texel, 0)).rgb * weight;
            weightSum += weight;
        }
    const float3 correction = sum / max(weightSum, 0.000001);
    return float4(max(scene.rgb + fade * correction, 0.0), scene.a);
}
