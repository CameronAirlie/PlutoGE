// Screen-space contact shadows of the directional light (ScreenSpaceContactShadows.h). Shadow maps cannot resolve
// occluders within a few texels of their receivers, so feet, props and creases look detached. This pass marches a
// short ray from each pixel towards the light through the frame's coverage depth and writes the light's visibility
// (r) with the device depth it was traced for (g). Surfaces multiply their directional shadow by it only where their
// own depth matches (BasicLit.slang contactShadowVisibility): a draw the coverage depth did not include keeps its own
// shadowing rather than one traced for whatever lay behind it.
//
// Lanes: parameters[0] the view-space direction towards the light and the ray length (world units);
// parameters[1].x the thickness an occluder is assumed to have, .y the frame index for the start dither.
// quality is the step count.
#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "ScreenSpaceGuideCommon.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);

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

[shader("fragment")]
float2 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    const int2 size = int2(round(1.0 / pass.inverseResolution));
    const float deviceDepth = depthTexture.Load(int3(pixel, 0)).r;
    const float linearDepth = linearViewDepth(deviceDepth, pass.projection, pass.zeroToOneDepth);
    if (linearDepth <= 0.0) return float2(1.0, deviceDepth);
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
    const float3 towardsLight = pass.parameters[0].xyz;
    const float length = pass.parameters[0].w;
    const float thickness = pass.parameters[1].x;
    const uint steps = max(pass.quality, 1u);
    // Interleaved gradient noise (Jimenez) staggers the samples between neighbours and frames; TAA averages them.
    const float2 noisePixel = float2(pixel) + 5.588238 * (pass.parameters[1].y % 64.0);
    const float dither = frac(52.9829189 * frac(dot(noisePixel, float2(0.06711056, 0.00583715))));
    float occlusion = 0.0;
    for (uint step = 0; step < steps; ++step)
    {
        const float travelled = (float(step) + dither) / float(steps) * length;
        const float3 position = origin + towardsLight * travelled;
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
        {
            // Occluders near the ray's end fade, so the shadow has no hard cut-off at its length.
            occlusion = saturate((1.0 - travelled / length) * 4.0);
            break;
        }
    }
    // Fade out towards the screen edges, where the occluders the ray would reach are off screen.
    return float2(1.0 - occlusion * edgeFade(uv, 0.05), deviceDepth);
}
