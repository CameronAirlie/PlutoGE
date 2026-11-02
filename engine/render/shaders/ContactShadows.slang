// Screen-space contact shadows (ScreenSpaceContactShadows.h). Shadow maps cannot resolve occluders within a few texels
// of their receivers, so feet, props and creases look detached; rectangle lights have no shadow maps at all. This
// pass marches a short ray from each pixel towards each of up to three lights through the frame's coverage depth and
// writes the lights' visibilities (rgb) with the device depth they were traced for (a). Surfaces multiply a light by
// its channel only where their own depth matches (BasicLit.slang contactShadowVisibility): a draw the coverage depth
// did not include keeps its own shadowing rather than one traced for whatever lay behind it.
//
// Lanes, per slot i: parameters[2i] the view-space direction towards a directional light, or a local light's
// view-space position, and the ray length (world units); parameters[2i + 1].x the kind (0 none, 1 directional,
// 2 local), .y the thickness an occluder is assumed to have. parameters[6].x is the frame index for the start dither.
// quality is the step count.
#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "ScreenSpaceGuideCommon.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);

static const uint ContactShadowSlots = 3;

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

float2 viewToUv(float3 position, out bool inFront)
{
    const float4 clip = mul(pass.projection, float4(position, 1.0));
    inFront = clip.w > 0.000001;
    float2 ndc = clip.xy / max(clip.w, 0.000001);
    if (pass.flipY != 0) ndc.y = -ndc.y;
    return ndc * 0.5 + 0.5;
}

// Linear depth of a neighbouring texel; the background (no depth) counts as infinitely far.
float neighbourDepth(int2 pixel, int2 size)
{
    const float depth = linearViewDepth(depthTexture.Load(int3(clamp(pixel, int2(0), size - 1), 0)).r,
                                        pass.projection, pass.zeroToOneDepth);
    return depth > 0.0 ? depth : 1.e30;
}

// The share of `length` world units from `origin` along `direction` (view space, unit) that no surface in the
// depth buffer occludes, with the occluders' assumed `thickness` and the receiver's depth-slope tolerance.
float marchVisibility(float3 origin, float3 direction, float length, float thickness, float slopeBias, float dither,
                      int2 size)
{
    const uint steps = max(pass.quality, 1u);
    for (uint step = 0; step < steps; ++step)
    {
        const float travelled = (float(step) + dither) / float(steps) * length;
        const float3 position = origin + direction * travelled;
        bool inFront;
        const float2 sampleUv = viewToUv(position, inFront);
        if (!inFront || any(sampleUv < 0.0) || any(sampleUv > 1.0)) break;
        const int2 samplePixel = clamp(int2(sampleUv * float2(size)), int2(0), size - 1);
        const float sceneDepth = linearViewDepth(depthTexture.Load(int3(samplePixel, 0)).r, pass.projection,
                                                 pass.zeroToOneDepth);
        const float rayDepth = -position.z;
        // Behind the depth buffer by more than its precision, and not so far behind that the surface in front is
        // thinner than the ray's distance from it.
        const float behind = rayDepth - sceneDepth;
        const float bias = 0.0015 * rayDepth + 0.002 + slopeBias;
        if (sceneDepth > 0.0 && behind > bias && behind < thickness)
            // Occluders near the ray's end fade, so the shadow has no hard cut-off at its length.
            return 1.0 - saturate((1.0 - travelled / length) * 4.0);
    }
    return 1.0;
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    const int2 size = int2(round(1.0 / pass.inverseResolution));
    const float deviceDepth = depthTexture.Load(int3(pixel, 0)).r;
    const float linearDepth = linearViewDepth(deviceDepth, pass.projection, pass.zeroToOneDepth);
    if (linearDepth <= 0.0) return float4(1.0, 1.0, 1.0, deviceDepth);
    const float2 uv = (float2(pixel) + 0.5) * pass.inverseResolution;
    const float3 origin = viewPositionFromLinearDepth(uv, linearDepth, pass);
    // Samples compare the ray with the depth at their texel's centre, up to half a texel from where the ray
    // projects. Where the receiver is seen at a grazing angle its depth changes by far more than the bias across
    // that half texel, so a ray leaving a lit surface would find the surface itself "in front" and shadow it in
    // dithered streaks. Tolerate the receiver's own depth slope (the smaller one-sided difference on each axis, so
    // a silhouette beside the pixel does not count).
    const float slopeX = min(abs(neighbourDepth(pixel + int2(1, 0), size) - linearDepth),
                             abs(linearDepth - neighbourDepth(pixel - int2(1, 0), size)));
    const float slopeY = min(abs(neighbourDepth(pixel + int2(0, 1), size) - linearDepth),
                             abs(linearDepth - neighbourDepth(pixel - int2(0, 1), size)));
    const float slopeBias = 0.5 * (slopeX + slopeY);
    // Interleaved gradient noise (Jimenez) staggers the samples between neighbours and frames; TAA averages them.
    const float2 noisePixel = float2(pixel) + 5.588238 * (pass.parameters[2 * ContactShadowSlots].x % 64.0);
    const float dither = frac(52.9829189 * frac(dot(noisePixel, float2(0.06711056, 0.00583715))));
    // Fade out towards the screen edges, where the occluders the rays would reach are off screen.
    const float edge = edgeFade(uv, 0.05);
    float visibility[ContactShadowSlots];
    [ForceUnroll] for (uint slot = 0; slot < ContactShadowSlots; ++slot)
    {
        visibility[slot] = 1.0;
        const float4 vector = pass.parameters[2 * slot];
        const float4 settings = pass.parameters[2 * slot + 1];
        if (settings.x < 0.5) continue;
        float3 direction = vector.xyz;
        float length = vector.w;
        if (settings.x > 1.5)
        {
            // A local light: towards its position, stopping there.
            const float3 toLight = vector.xyz - origin;
            const float distance = sqrt(dot(toLight, toLight));
            if (distance <= 0.0001) continue;
            direction = toLight / distance;
            length = min(length, distance);
        }
        visibility[slot] = 1.0 - (1.0 - marchVisibility(origin, direction, length, settings.y, slopeBias, dither, size)) * edge;
    }
    return float4(visibility[0], visibility[1], visibility[2], deviceDepth);
}
