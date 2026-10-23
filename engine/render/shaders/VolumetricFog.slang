#include "Fullscreen.slang"
#include "FogIntegration.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "VirtualShadowCommon.slang"
#include "VirtualShadowSampling.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);

#include "FogLightingParameters.slang"

[[vk::binding(8, 0)]] Sampler2D shadowTexture0 : register(t8, space0);
[[vk::binding(9, 0)]] Sampler2D shadowTexture1 : register(t9, space0);
[[vk::binding(10, 0)]] Sampler2D shadowTexture2 : register(t10, space0);
[[vk::binding(11, 0)]] Sampler2D shadowTexture3 : register(t11, space0);
[[vk::binding(12, 0)]] cbuffer VirtualShadowData : register(b12, space0) { VirtualShadowParameters vsm; };
[[vk::binding(13, 0)]] Sampler2D virtualShadowAtlas : register(t13, space0);
[[vk::binding(14, 0)]] Sampler2D virtualShadowTable : register(t14, space0);

static const float PI = 3.14159265359;
#include "PhysicalSkyEnvironment.slang"
#include "FogEnvironment.slang"

#define PLUTO_CLOUD_NOISE_VOLUME 1
#include "CloudNoise.slang"

static const float FOUR_PI = 12.566370614359172;

// Mean-preserving density variation. Lane 4.y is strength, 4.z the noise
// frequency per world unit and 5.xyz the wind offset. Lattice noise is
// symmetric about its 0.5 mean; centring and symmetric clamping keep that
// mean, so the analytic tail and the unvaried glass and particle fog still
// integrate the same average medium. Features are flattened vertically.
float fogDensityScale(float3 position, float fade)
{
    const float strength = saturate(pass.parameters[4].y) * fade;
    if (strength <= 0.0 || pass.parameters[4].z <= 0.0)
        return 1.0;
    const float3 p = (position + pass.parameters[5].xyz) * pass.parameters[4].z * float3(1.0, 2.0, 1.0);
    const float noise = cloudNoise(p) * 0.65 + cloudNoise(p * 2.03 + float3(17.1, 9.2, 13.7)) * 0.35;
    return 1.0 + strength * clamp((noise - 0.5) * 4.0, -1.0, 1.0);
}

float interleavedGradientNoise(float2 pixel, float frame)
{
    const float noise = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    return frac(noise + frame * 0.61803398875);
}

float henyeyGreenstein(float cosine)
{
    const float anisotropy = clamp(pass.parameters[2].x, -0.85, 0.85);
    const float denominator = max(1.0 + anisotropy * anisotropy -
                                  2.0 * anisotropy * cosine, 0.0001);
    return (1.0 - anisotropy * anisotropy) /
           (FOUR_PI * pow(denominator, 1.5));
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

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

float4 integrateFog(FullscreenVertexOutput input)
{
    // Depth is fetched with nearest filtering at full resolution. Reconstruct
    // its ray from that texel's centre, not the half-resolution sample location.
    // A half-pixel angular mismatch at altitude moves the inferred water hit
    // vertically by metres and creates bands in exponential height density.
    const float2 uv = (floor(orientFullscreenUv(input.uv, pass.flipY) / pass.inverseResolution) + 0.5) *
                      pass.inverseResolution;
    const float sceneDepth = depthTexture.Sample(uv).r;
    const float configuredDetailDistance = max(pass.parameters[1].z, 0.001);
    const float3 rayDirection = reconstructWorldRay(uv, pass.view, pass.projection, pass.flipY);

    const bool sky = sceneDepth <= 0.0;
    float surfaceDistance = 0.0;
    if (!sky)
    {
        // Ocean fog depth includes the nearest opaque/water surface and stores
        // reciprocal ray distance, independently of the camera clip planes.
        if (pass.parameters[4].x > .5)
            surfaceDistance = max(1.0/sceneDepth-1.0, 0.0);
        else
        {
            const float3 surfacePosition = reconstructWorldPosition(
                uv, sceneDepth, pass.inverseViewProjection, pass.flipY, pass.zeroToOneDepth);
            surfaceDistance = length(surfacePosition - pass.cameraPosition.xyz);
        }
    }
    float detailDistance = configuredDetailDistance;
    if (fogShadowsEnabled != 0 && fogShadowCascadeParameters.w <= 0.5)
    {
        const int count = clamp((int)fogShadowCascadeParameters.x, 1, 4);
        const float viewScale = max(-mul((float3x3)fogView, rayDirection).z, 0.0001);
        detailDistance = min(detailDistance, max(fogShadowCascadeSplits[count - 1], 0.001) / viewScale);
    }
    const float marchDistance = sky ? detailDistance : min(surfaceDistance, detailDistance);
    if ((!sky && surfaceDistance <= 0.0001) || (pass.parameters[0].w <= 0.0 && pass.parameters[3].y <= 0.0))
        return float4(0, 0, 0, 1);

    const int stepCount = clamp((int)pass.quality, 1, 64);
    const int shadowStepStride = clamp((int)pass.parameters[3].x, 1, 4);
    const float stepLength = marchDistance / (float)stepCount;
    const float jitter = interleavedGradientNoise(input.position.xy, pass.time * 60.0);
    const float3 lightDirection = normalize(-fogDirectionalDirectionIntensity.xyz);
    // Keep the phase normalized: multiplying by 4 PI creates excess radiance.
    const float phase = henyeyGreenstein(dot(rayDirection, lightDirection));
    const float scattering = max(pass.parameters[1].w, 0.0);
    const float3 fogColor = max(pass.parameters[0].rgb, 0.0);
    const bool hasDirectionalLight = fogDirectionalDirectionIntensity.w > 0.0 &&
                                     any(fogDirectionalColor.rgb > 0.0) &&
                                     pass.parameters[2].z > 0.0;
    const FogMedium medium = makeFogMedium(pass.parameters[0].w, pass.parameters[1].x, pass.parameters[1].y);
    FogLighting lighting;
    lighting.ambientAlbedo = fogColor * max(pass.parameters[2].y, 0.0) * scattering;
    lighting.environment = fogEnvironmentLighting(fogCameraPositionAmbient.w);
    lighting.sunRadiance = hasDirectionalLight
        ? fogColor * max(fogDirectionalColor.rgb, 0.0) *
              fogDirectionalDirectionIntensity.w * phase * pass.parameters[2].z * scattering
        : 0.0;

    float transmittance = 1.0;
    float3 accumulatedLight = 0.0;
    float3 pointRadiance = 0.0;
    // Opacity-weighted scattering distance, which places the fog within the haze.
    float scatterDistance = 0.0;
    float scatterWeight = 0.0;
    for (int stepIndex = 0; stepIndex < stepCount; ++stepIndex)
    {
        const float sampleDistance = (float)stepIndex * stepLength;
        const float3 samplePosition = pass.cameraPosition.xyz + rayDirection * sampleDistance;
        // One stratified point per cell carries the cell's lighting and
        // density variation. Lighting is reused across the shadow stride.
        const float cellDistance = ((float)stepIndex +
            frac(jitter + (float)stepIndex * 0.61803398875)) * stepLength;
        const float3 cellPosition = pass.cameraPosition.xyz + rayDirection * cellDistance;
        const float detailWeight = fogShadowWeight(cellDistance, detailDistance);
        if ((stepIndex % shadowStepStride) == 0)
        {
            const float lightVisibility = hasDirectionalLight
                ? lerp(1.0, directionalVisibility(cellPosition), detailWeight)
                : 1.0;
            pointRadiance = fogPointRadiance(lighting, medium, fogCloudOcclusion, cellPosition,
                                             lightDirection, lightVisibility);
        }

        // Density variation fades with shadow detail so the march meets the
        // analytic mean-density tail without a visible boundary.
        const float segmentTransmittance = exp(-heightFogOpticalDepth(medium.density, medium.falloff,
            medium.heightOffset, samplePosition.y, rayDirection.y, stepLength) *
            fogDensityScale(cellPosition, detailWeight));
        const float segmentWeight = transmittance * (1.0 - segmentTransmittance);
        accumulatedLight += segmentWeight * pointRadiance;
        scatterDistance += segmentWeight * cellDistance;
        scatterWeight += segmentWeight;
        transmittance *= segmentTransmittance;
        if (transmittance <= 0.000001)
        {
            transmittance = 0.0;
            break;
        }
    }

    // Compose the distant integral behind the near volume with its remaining
    // transmittance. Surface rays stop at the actual opaque/water hit. The
    // tail is lit at its scattering midpoint, beyond shadow coverage.
    const float tailHeight = pass.cameraPosition.y + rayDirection.y * marchDistance;
    const float tailLength = sky ? 1e30 : max(surfaceDistance - marchDistance, 0.0);
    const float tailDepth = sky
        ? infiniteHeightFogOpticalDepth(medium.density, medium.falloff, medium.heightOffset, tailHeight, rayDirection.y)
        : heightFogOpticalDepth(medium.density, medium.falloff, medium.heightOffset, tailHeight, rayDirection.y, tailLength);
    const float tailTransmittance = exp(-tailDepth);
    if (transmittance > 0.0 && tailTransmittance < 1.0)
    {
        const float tailMidpoint = heightFogScatteringMidpoint(medium.density, medium.falloff, medium.heightOffset,
            tailHeight, rayDirection.y, tailDepth, tailLength);
        const float3 tailPosition = pass.cameraPosition.xyz + rayDirection * (marchDistance + tailMidpoint);
        const float tailWeight = transmittance * (1.0 - tailTransmittance);
        accumulatedLight += tailWeight *
            fogPointRadiance(lighting, medium, fogCloudOcclusion, tailPosition, lightDirection, 1.0);
        scatterDistance += tailWeight * (marchDistance + tailMidpoint);
        scatterWeight += tailWeight;
    }
    transmittance *= tailTransmittance;

    // Max Opacity scales the layer instead of clamping it, so contrast keeps
    // falling with distance rather than flattening behind a constant veil.
    const float opacityScale = saturate(pass.parameters[2].w);
    accumulatedLight *= opacityScale;
    transmittance = 1.0 - opacityScale * (1.0 - transmittance);
    // The opacity scale does not limit the separate, more distant haze.
    float4 fog = float4(accumulatedLight, transmittance);
    if (pass.parameters[3].y > 0.0)
    {
        const float3 hazeLighting = horizonHazeLighting(rayDirection, fogCameraPositionAmbient.w,
            fogDirectionalColor.rgb * max(fogDirectionalDirectionIntensity.w, 0.0), pass.parameters[3].z,
            evaluateCloudLighting(fogCloudOcclusion, pass.cameraPosition.xyz, lightDirection));
        fog = composeHorizonHaze(fog, scatterWeight > 0.0 ? scatterDistance / scatterWeight : 0.0,
                                 rayDirection.y, surfaceDistance, sky, pass.parameters[3], hazeLighting);
    }
    return fog;
}

[shader("fragment")]
float4 traceMain(FullscreenVertexOutput input) : SV_Target { return integrateFog(input); }

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float4 scene = sceneTexture.Sample(orientFullscreenUv(input.uv, pass.flipY));
    const float4 volume = integrateFog(input);
    return float4(scene.rgb * volume.a + volume.rgb, scene.a);
}
