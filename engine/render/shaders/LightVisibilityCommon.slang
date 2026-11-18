// The light visibility mask (src/lighting/LightVisibilityMask.h): how much of each of up to
// PLUTO_LIGHT_VISIBILITY_SLOTS lights reaches every pixel of the coverage depth, written once after the depth prepass
// and read by the surface shaders (BasicLit.slang lightVisibility). A slot combines two producers:
//
//   contact  a short march through the depth buffer towards the light (screen space), which catches occluders too
//            close for shadow maps: feet, props, creases. Off screen or hidden occluders cast nothing.
//   world    a test along the whole segment to the light against a world representation (LightVisibilityVoxel.slang,
//            LightVisibilityRayQuery.slang), for local lights without a shadow map: it stops their light passing
//            through walls, floors and hillsides.
//
// The pass file including this defines the world test before including LightVisibilityPass.slang:
//   float worldVisibility(float3 position, float3 normal, float viewDepth, uint slot, float2 noise)
// `position` is the receiver's world position and `normal` its geometric normal (both reconstructed from depth), and
// the result is the share of the light that reaches it, 1 for clear.
#ifndef PLUTO_LIGHT_VISIBILITY_COMMON
#define PLUTO_LIGHT_VISIBILITY_COMMON

#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "ScreenSpaceGuideCommon.slang"
#include "../include/PlutoGE/render/LightVisibilityConfig.h"

// Mirrors LightVisibilityParameters (LightVisibilityMask.cpp).
[[vk::binding(0, 0)]] cbuffer LightVisibilityParameters : register(b0, space0)
{
    // Camera, viewport and depth conventions (projection, flipY, zeroToOneDepth, inverseResolution).
    PostProcessParameterBlock pass;
    column_major float4x4 viewToWorld;
    // x slots in use, y frame index for the dither (0-63), z contact march steps.
    uint4 visibilityCounts;
    // World tests: x start distance, y start distance per unit of view depth, z normal offset.
    float4 visibilityTrace;
    // Per slot: xyz the view-space position of a local light or the view-space direction towards the directional
    // light; w the kind (0 none, 1 directional, 2 local).
    float4 slotLight[PLUTO_LIGHT_VISIBILITY_SLOTS];
    // Per slot: xyz the light's world position (local lights); w 1 when the world test runs for it.
    float4 slotWorld[PLUTO_LIGHT_VISIBILITY_SLOTS];
    // Per slot: x contact length (0 none), y occluder thickness assumed by the contact march, z the emitter's radius,
    // w the light's range.
    float4 slotSettings[PLUTO_LIGHT_VISIBILITY_SLOTS];
};
[[vk::binding(1, 0)]] [format("rgba32f")] RWTexture2D<float4> visibilityMask : register(u1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);

static const uint LightVisibilityNone = 0;
static const uint LightVisibilityDirectional = 1;
static const uint LightVisibilityLocal = 2;

int2 visibilityResolution() { return int2(round(1.0 / pass.inverseResolution)); }

float2 visibilityViewToUv(float3 position, out bool inFront)
{
    const float4 clip = mul(pass.projection, float4(position, 1.0));
    inFront = clip.w > 0.000001;
    float2 ndc = clip.xy / max(clip.w, 0.000001);
    if (pass.flipY != 0) ndc.y = -ndc.y;
    return ndc * 0.5 + 0.5;
}

// Linear depth of a neighbouring texel; the background (no depth) counts as infinitely far.
float visibilityNeighbourDepth(int2 pixel, int2 size)
{
    const float depth = linearViewDepth(depthTexture.Load(int3(clamp(pixel, int2(0), size - 1), 0)).r,
                                        pass.projection, pass.zeroToOneDepth);
    return depth > 0.0 ? depth : 1.e30;
}

// The share of `length` world units from `origin` along `direction` (view space, unit) that no surface in the
// depth buffer occludes, with the occluders' assumed `thickness` and the receiver's depth-slope tolerance.
float contactVisibility(float3 origin, float3 direction, float length, float thickness, float slopeBias, float dither,
                        int2 size)
{
    const uint steps = max(visibilityCounts.z, 1u);
    for (uint step = 0; step < steps; ++step)
    {
        const float travelled = (float(step) + dither) / float(steps) * length;
        const float3 position = origin + direction * travelled;
        bool inFront;
        const float2 sampleUv = visibilityViewToUv(position, inFront);
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

// Packs slot visibilities into the mask's three channels (LightVisibilityConfig.h).
float4 packLightVisibility(float visibility[PLUTO_LIGHT_VISIBILITY_SLOTS], float deviceDepth)
{
    uint channels[3] = {0u, 0u, 0u};
    [ForceUnroll] for (uint slot = 0; slot < PLUTO_LIGHT_VISIBILITY_SLOTS; ++slot)
    {
        const uint level = uint(round(saturate(visibility[slot]) * float(PLUTO_LIGHT_VISIBILITY_LEVELS)));
        channels[slot / PLUTO_LIGHT_VISIBILITY_SLOTS_PER_CHANNEL] |=
            level << ((slot % PLUTO_LIGHT_VISIBILITY_SLOTS_PER_CHANNEL) * PLUTO_LIGHT_VISIBILITY_BITS);
    }
    return float4(float(channels[0]), float(channels[1]), float(channels[2]), deviceDepth);
}

// A uniform value in [0, 1) per pixel, frame and salt (PCG hash), for jittering world tests over the emitter.
float visibilityRandom(int2 pixel, uint salt)
{
    uint state = uint(pixel.x) * 1973u + uint(pixel.y) * 9277u + visibilityCounts.y * 26699u + salt * 104729u;
    state = state * 747796405u + 2891336453u;
    state = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    state = (state >> 22u) ^ state;
    return float(state >> 8u) * (1.0 / 16777216.0);
}

// A point on the emitter of `slot` as seen from `position`: its centre offset within the disc of its radius facing
// the receiver, so world tests from neighbouring pixels and frames cover the emitter and TAA resolves a penumbra.
float3 visibilityEmitterPoint(uint slot, float3 position, float2 noise)
{
    const float3 centre = slotWorld[slot].xyz;
    const float radius = slotSettings[slot].z;
    if (radius <= 0.0)
        return centre;
    const float3 axis = normalize(position - centre);
    const float3 helper = abs(axis.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(helper, axis));
    const float3 bitangent = cross(axis, tangent);
    const float angle = noise.x * 6.2831853;
    const float distance = sqrt(noise.y) * radius;
    return centre + (tangent * cos(angle) + bitangent * sin(angle)) * distance;
}

#endif
