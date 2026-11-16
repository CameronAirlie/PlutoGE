// The sun's visibility at a point in the air, where there is no surface normal to bias along: the directional shadow
// maps (virtual or cascaded) and terrain occlusion beyond their reach. Shared by volumetric fog (VolumetricFog.slang)
// and scattering particles (Particles.slang), so a speck of dust in a sunbeam is lit exactly where the fog's shaft is.
// BasicRenderer::BindSunShadowResources and BindSunOcclusionMaps bind these slots.
//
// The includer first includes FogLightingParameters.slang (the frame's shadow matrices and cascade settings, slot 7),
// FogIntegration.slang (fogShadowWeight), VirtualShadowCommon.slang and VirtualShadowSampling.slang.
#ifndef PLUTO_DIRECTIONAL_VOLUME_SHADOW
#define PLUTO_DIRECTIONAL_VOLUME_SHADOW

[[vk::binding(8, 0)]] Sampler2D shadowTexture0 : register(t8, space0);
[[vk::binding(9, 0)]] Sampler2D shadowTexture1 : register(t9, space0);
[[vk::binding(10, 0)]] Sampler2D shadowTexture2 : register(t10, space0);
[[vk::binding(11, 0)]] Sampler2D shadowTexture3 : register(t11, space0);
[[vk::binding(12, 0)]] cbuffer VirtualShadowData : register(b12, space0) { VirtualShadowParameters vsm; };
[[vk::binding(13, 0)]] Sampler2D virtualShadowAtlas : register(t13, space0);
[[vk::binding(14, 0)]] Sampler2D virtualShadowTable : register(t14, space0);
// Terrain sun occlusion (FogTerrainShadow.h): per texel, the world height below which terrain hides the sun.
[[vk::binding(17, 0)]] Sampler2D fogTerrainShadow : register(t17, space0);

// Sunlight left by the terrain at `position` (1 lit, 0 in a terrain's shadow), beyond the shadow maps' reach: a
// low sun behind a distant ridge. `origin` is the grid's world XZ corner and `extent` its size (0 = off).
// Fetched texels filtered by hand: the heights are 32-bit floats and the fetch needs no derivatives.
float terrainSunVisibility(float3 position, float2 origin, float extent)
{
    if (extent <= 0.0)
        return 1.0;
    uint width, height;
    fogTerrainShadow.GetDimensions(width, height);
    const float size = (float)max(width, 1u);
    const float2 texel = (position.xz - origin) / extent * size - 0.5;
    if (any(texel < -0.5) || any(texel > size - 0.5))
        return 1.0;
    const int2 base = int2(floor(texel));
    const float2 f = texel - float2(base);
    const int last = (int)width - 1;
    const float h00 = fogTerrainShadow.Load(int3(clamp(base, 0, last), 0)).r;
    const float h10 = fogTerrainShadow.Load(int3(clamp(base + int2(1, 0), 0, last), 0)).r;
    const float h01 = fogTerrainShadow.Load(int3(clamp(base + int2(0, 1), 0, last), 0)).r;
    const float h11 = fogTerrainShadow.Load(int3(clamp(base + int2(1, 1), 0, last), 0)).r;
    const float shadowHeight = lerp(lerp(h00, h10, f.x), lerp(h01, h11, f.x), f.y);
    // A soft edge about a texel deep hides the grid.
    const float softness = max(extent / size, 1.0);
    return saturate((position.y - shadowHeight) / softness + 0.5);
}

float sampleShadowCascade(int cascade, float2 uv)
{
    if (cascade == 0) return shadowTexture0.Sample(uv).r;
    if (cascade == 1) return shadowTexture1.Sample(uv).r;
    if (cascade == 2) return shadowTexture2.Sample(uv).r;
    return shadowTexture3.Sample(uv).r;
}

float evaluateShadowCascade(int cascade, float3 worldPosition)
{
    const float4 lightClip = mul(fogShadowMatrices[cascade], float4(worldPosition, 1.0));
    const float safeW = abs(lightClip.w) > 0.000001
                            ? lightClip.w
                            : (lightClip.w < 0.0 ? -0.000001 : 0.000001);
    const float3 shadowCoord = lightClip.xyz / safeW;
    float2 shadowUv = shadowCoord.xy * 0.5 + 0.5;
    if (fogShadowFlipY != 0) shadowUv.y = 1.0 - shadowUv.y;
    if (!all(shadowUv >= 0.0) || !all(shadowUv <= 1.0) ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0)
        return 1.0;

    // Volumetric samples have no receiver surface normal. A small fixed bias
    // avoids shadow-map self-occlusion while retaining thin shaft silhouettes.
    const float receiverDepth = shadowCoord.z * fogShadowDepthScale +
                                fogShadowDepthBias - 0.0015;
    const float2 texel = fogShadowInverseResolutions[cascade].xy *
                         max(fogShadowCascadeParameters.z, 1.0);
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            visibility += receiverDepth <=
                                  sampleShadowCascade(cascade, shadowUv + float2(x, y) * texel)
                              ? 1.0 : 0.0;
    const float edge = min(min(min(shadowUv.x, 1.0 - shadowUv.x),
                              min(shadowUv.y, 1.0 - shadowUv.y)),
                              min(shadowCoord.z, 1.0 - shadowCoord.z));
    return lerp(1.0, visibility / 9.0, smoothstep(0.0, 0.05, edge));
}

// The directional shadow maps' visibility at a point in the air: the virtual shadow maps when the frame uses them,
// otherwise the cascades, blended across cascade splits and faded out at the last one's reach.
float directionalVisibility(float3 worldPosition)
{
    if (fogShadowsEnabled == 0)
        return 1.0;
    if (fogShadowCascadeParameters.w > 0.5)
        return virtualShadowFogVisibility(vsm, virtualShadowAtlas, virtualShadowTable, worldPosition);

    const float receiverDistance = -(mul(fogView, float4(worldPosition, 1.0))).z;
    const int cascadeCount = clamp((int)fogShadowCascadeParameters.x, 1, 4);
    if (receiverDistance >= fogShadowCascadeSplits[cascadeCount - 1])
        return 1.0;

    int cascade = cascadeCount - 1;
    for (int index = 0; index < cascadeCount; ++index)
        if (receiverDistance <= fogShadowCascadeSplits[index])
        {
            cascade = index;
            break;
        }

    float visibility = evaluateShadowCascade(cascade, worldPosition);
    const float blendDistance = fogShadowCascadeParameters.y;
    if (cascade + 1 < cascadeCount && blendDistance > 0.0)
    {
        const float blend = saturate((receiverDistance -
            (fogShadowCascadeSplits[cascade] - blendDistance)) / blendDistance);
        if (blend > 0.0)
            visibility = lerp(visibility,
                              evaluateShadowCascade(cascade + 1, worldPosition), blend);
    }
    return lerp(1.0, visibility, fogShadowWeight(receiverDistance,
        max(fogShadowCascadeSplits[cascadeCount - 1], 0.001)));
}

#endif
