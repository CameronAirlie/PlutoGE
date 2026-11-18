// The light visibility pass's entry point (LightVisibilityCommon.slang documents the mask). Include after
// LightVisibilityCommon.slang and a definition of worldVisibility. One thread per pixel.

// View-space position of the coverage depth at `pixel`, or false for the background.
bool visibilityViewPosition(int2 pixel, int2 size, out float3 position, out float linearDepth)
{
    linearDepth = linearViewDepth(depthTexture.Load(int3(clamp(pixel, int2(0), size - 1), 0)).r, pass.projection,
                                  pass.zeroToOneDepth);
    position = 0.0;
    if (linearDepth <= 0.0)
        return false;
    position = viewPositionFromLinearDepth((float2(pixel) + 0.5) * pass.inverseResolution, linearDepth, pass);
    return true;
}

// The receiver's geometric normal (view space, towards the camera) from its neighbours' depths: on each axis the
// neighbour nearer in depth, so a silhouette beside the pixel does not tilt it.
float3 visibilityViewNormal(int2 pixel, int2 size, float3 origin, float linearDepth)
{
    float3 positions[4];
    float depths[4];
    const int2 offsets[4] = {int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1)};
    [ForceUnroll] for (uint i = 0; i < 4; ++i)
    {
        if (!visibilityViewPosition(pixel + offsets[i], size, positions[i], depths[i]))
            depths[i] = 1.e30;
    }
    const float3 dx = abs(depths[0] - linearDepth) <= abs(depths[1] - linearDepth) ? positions[0] - origin
                                                                                    : origin - positions[1];
    const float3 dy = abs(depths[2] - linearDepth) <= abs(depths[3] - linearDepth) ? positions[2] - origin
                                                                                    : origin - positions[3];
    float3 normal = cross(dx, dy);
    const float lengthSquared = dot(normal, normal);
    normal = lengthSquared > 1.e-20 ? normal * rsqrt(lengthSquared) : normalize(-origin);
    return dot(normal, -origin) < 0.0 ? -normal : normal;
}

[shader("compute")]
[numthreads(PLUTO_LIGHT_VISIBILITY_GROUP, PLUTO_LIGHT_VISIBILITY_GROUP, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    const int2 size = visibilityResolution();
    const int2 pixel = int2(id.xy);
    if (any(pixel >= size))
        return;
    float visibility[PLUTO_LIGHT_VISIBILITY_SLOTS];
    [ForceUnroll] for (uint slot = 0; slot < PLUTO_LIGHT_VISIBILITY_SLOTS; ++slot)
        visibility[slot] = 1.0;
    const float deviceDepth = depthTexture.Load(int3(pixel, 0)).r;
    float3 origin;
    float linearDepth;
    if (!visibilityViewPosition(pixel, size, origin, linearDepth))
    {
        visibilityMask[pixel] = packLightVisibility(visibility, deviceDepth);
        return;
    }
    const float2 uv = (float2(pixel) + 0.5) * pass.inverseResolution;
    // Samples compare the ray with the depth at their texel's centre, up to half a texel from where the ray
    // projects. Where the receiver is seen at a grazing angle its depth changes by far more than the bias across
    // that half texel, so a ray leaving a lit surface would find the surface itself "in front" and shadow it in
    // dithered streaks. Tolerate the receiver's own depth slope (the smaller one-sided difference on each axis, so
    // a silhouette beside the pixel does not count).
    const float slopeX = min(abs(visibilityNeighbourDepth(pixel + int2(1, 0), size) - linearDepth),
                             abs(linearDepth - visibilityNeighbourDepth(pixel - int2(1, 0), size)));
    const float slopeY = min(abs(visibilityNeighbourDepth(pixel + int2(0, 1), size) - linearDepth),
                             abs(linearDepth - visibilityNeighbourDepth(pixel - int2(0, 1), size)));
    const float slopeBias = 0.5 * (slopeX + slopeY);
    // Interleaved gradient noise (Jimenez) staggers the samples between neighbours and frames; TAA averages them.
    const float2 noisePixel = float2(pixel) + 5.588238 * float(visibilityCounts.y % 64u);
    const float dither = frac(52.9829189 * frac(dot(noisePixel, float2(0.06711056, 0.00583715))));
    // Fade out towards the screen edges, where the occluders contact rays would reach are off screen.
    const float edge = edgeFade(uv, 0.05);
    const float3 viewNormal = visibilityViewNormal(pixel, size, origin, linearDepth);
    const float3 worldPosition = mul(viewToWorld, float4(origin, 1.0)).xyz;
    const float3 worldNormal = normalize(mul(viewToWorld, float4(viewNormal, 0.0)).xyz);
    for (uint slot = 0; slot < min(visibilityCounts.x, uint(PLUTO_LIGHT_VISIBILITY_SLOTS)); ++slot)
    {
        const float4 light = slotLight[slot];
        const uint kind = uint(light.w + 0.5);
        if (kind == LightVisibilityNone)
            continue;
        const float4 settings = slotSettings[slot];
        float3 direction = light.xyz;
        float distance = 1.e30;
        if (kind == LightVisibilityLocal)
        {
            const float3 toLight = light.xyz - origin;
            distance = sqrt(dot(toLight, toLight));
            // Beyond its range the light adds nothing, so neither test is worth its cost.
            if (distance <= 0.0001 || distance >= settings.w)
                continue;
            direction = toLight / distance;
        }
        // A surface facing away from the light receives none of it, whatever stands in between.
        if (dot(viewNormal, direction) <= 0.0)
            continue;
        float result = 1.0;
        if (settings.x > 0.0)
        {
            const float length = min(settings.x, distance);
            result = 1.0 - (1.0 - contactVisibility(origin, direction, length, settings.y, slopeBias, dither, size)) * edge;
        }
        if (slotWorld[slot].w > 0.5 && result > 0.0)
        {
            const float2 noise = float2(visibilityRandom(pixel, slot * 2u), visibilityRandom(pixel, slot * 2u + 1u));
            result *= worldVisibility(worldPosition, worldNormal, linearDepth, slot, noise);
        }
        visibility[slot] = result;
    }
    visibilityMask[pixel] = packLightVisibility(visibility, deviceDepth);
}
