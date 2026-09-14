#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "VirtualShadowCommon.slang"
#include "VirtualShadowSampling.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);

// This is the prefix of BasicRenderer's frame block through the fields fog
// consumes. Keeping fog on that existing buffer makes directional lighting and
// cascaded shadows identical to the opaque pass without maintaining a second
// per-frame light representation.
[[vk::binding(7, 0)]]
cbuffer FogLightingParameters : register(b7, space0)
{
    column_major float4x4 fogViewProjection;
    float4 fogCameraPositionAmbient;
    float4 fogDirectionalDirectionIntensity;
    float4 fogDirectionalColor;
    column_major float4x4 fogShadowMatrices[4];
    uint fogShadowsEnabled;
    uint fogShadowFlipY;
    float fogShadowDepthScale;
    float fogShadowDepthBias;
    float4 fogShadowInverseResolutions[4];
    float4 fogShadowCascadeSplits;
    float4 fogShadowCascadeMetrics[4];
    float4 fogShadowCascadeParameters;
    float4 fogShadowFilterParameters;
    float4 fogShadowFilterEdgeParameters;
    column_major float4x4 fogView;
};

[[vk::binding(8, 0)]] Sampler2D shadowTexture0 : register(t8, space0);
[[vk::binding(9, 0)]] Sampler2D shadowTexture1 : register(t9, space0);
[[vk::binding(10, 0)]] Sampler2D shadowTexture2 : register(t10, space0);
[[vk::binding(11, 0)]] Sampler2D shadowTexture3 : register(t11, space0);
[[vk::binding(12, 0)]] cbuffer VirtualShadowData : register(b12, space0) { VirtualShadowParameters vsm; };
[[vk::binding(13, 0)]] Sampler2D virtualShadowAtlas : register(t13, space0);
[[vk::binding(14, 0)]] Sampler2D virtualShadowTable : register(t14, space0);

static const float FOUR_PI = 12.566370614359172;

float interleavedGradientNoise(float2 pixel, float frame)
{
    const float noise = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    return frac(noise + frame * 0.61803398875);
}

float fogOpticalDepth(float3 startPosition, float3 rayDirection, float stepLength)
{
    const float falloff = max(pass.parameters[1].x, 0.0);
    const float heightChange = falloff * rayDirection.y * stepLength;
    // Integrate exponential height density over the whole segment. Midpoint
    // samples miss thin ground fog on long sky rays at low step counts.
    const float span = abs(heightChange);
    const float average = span < 0.001
        ? 1.0 - span * 0.5 + span * span / 6.0
        : (1.0 - exp(-span)) / span;
    const float denseHeight = min(startPosition.y, startPosition.y + rayDirection.y * stepLength);
    // Work in log space to avoid overflow below the fog layer, without the
    // former exp(-8) density floor that filled the sky above it.
    const float logDepth = log(max(pass.parameters[0].w, 1e-30)) +
        log(stepLength) + log(average) - falloff * (denseHeight - pass.parameters[1].y);
    return exp(clamp(logDepth, -80.0, 80.0));
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
        return 0.0;

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
    return visibility / 9.0;
}

float directionalVisibility(float3 worldPosition)
{
    if (fogShadowsEnabled == 0)
        return 1.0;
    if (fogShadowCascadeParameters.w > 0.5)
        return virtualShadowVolumeVisibility(vsm, virtualShadowAtlas, virtualShadowTable, worldPosition);

    const float receiverDistance = -(mul(fogView, float4(worldPosition, 1.0))).z;
    const int cascadeCount = clamp((int)fogShadowCascadeParameters.x, 1, 4);
    // Outside the final cascade there is no reliable visibility information.
    // A conservative result prevents an artificial bright fog wall at the
    // shadow distance while the configured fog range remains inside it.
    if (receiverDistance > fogShadowCascadeSplits[cascadeCount - 1])
        return 0.0;

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
    return visibility;
}

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

float4 integrateFog(FullscreenVertexOutput input)
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float sceneDepth = depthTexture.Sample(uv).r;
    const float maximumDistance = max(pass.parameters[1].z, 0.001);
    const float3 rayDirection = reconstructWorldRay(uv, pass.view, pass.projection, pass.flipY);

    float marchDistance = maximumDistance;
    if (sceneDepth > 0.000001)
    {
        // Ocean fog depth includes the nearest opaque/water surface and stores
        // reciprocal ray distance, independently of the camera clip planes.
        if (pass.parameters[4].x > .5)
            marchDistance = min(max(1.0/sceneDepth-1.0, 0.0), maximumDistance);
        else
        {
            const float3 surfacePosition = reconstructWorldPosition(
                uv, sceneDepth, pass.inverseViewProjection, pass.flipY, pass.zeroToOneDepth);
            marchDistance = min(length(surfacePosition - pass.cameraPosition.xyz), maximumDistance);
        }
    }
    if (marchDistance <= 0.0001 || pass.parameters[0].w <= 0.0)
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
    const float ambientIntensity = max(fogCameraPositionAmbient.w, 0.0);
    const float3 ambientLighting = fogColor * max(pass.parameters[2].y, 0.0) *
                                   scattering * ambientIntensity;
    const float3 directionalLighting = hasDirectionalLight
        ? fogColor * max(fogDirectionalColor.rgb, 0.0) *
              fogDirectionalDirectionIntensity.w * phase * pass.parameters[2].z * scattering
        : 0.0;

    float transmittance = 1.0;
    float3 accumulatedLight = 0.0;
    float lightVisibility = 1.0;
    for (int stepIndex = 0; stepIndex < stepCount; ++stepIndex)
    {
        const float sampleDistance = (float)stepIndex * stepLength;
        const float3 samplePosition = pass.cameraPosition.xyz + rayDirection * sampleDistance;
        if (hasDirectionalLight && (stepIndex % shadowStepStride) == 0)
        {
            const float shadowDistance = ((float)stepIndex +
                frac(jitter + (float)stepIndex * 0.61803398875)) * stepLength;
            lightVisibility = directionalVisibility(
                pass.cameraPosition.xyz + rayDirection * shadowDistance);
        }

        const float segmentTransmittance = exp(-fogOpticalDepth(samplePosition, rayDirection, stepLength));
        const float segmentOpacity = 1.0 - segmentTransmittance;
        accumulatedLight += transmittance * segmentOpacity *
                            (ambientLighting + directionalLighting * lightVisibility);
        transmittance *= segmentTransmittance;
        if (transmittance <= 0.001)
        {
            transmittance = 0.0;
            break;
        }
    }

    const float integratedOpacity = saturate(1.0 - transmittance);
    const float opacity = min(integratedOpacity, saturate(pass.parameters[2].w));
    const float3 fogRadiance = accumulatedLight / max(integratedOpacity, 0.0001);
    return float4(fogRadiance * opacity, 1.0 - opacity);
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
