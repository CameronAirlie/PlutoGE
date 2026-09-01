#include "Fullscreen.slang"
#include "PostProcessParameters.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID) { return fullscreenVertex(vertexId); }

float linearizeDepth(float depth, float nearPlane, float farPlane)
{
    depth = 1.0 - depth;
    const float z = depth * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) /
           max(farPlane + nearPlane - z * (farPlane - nearPlane), 0.0001);
}

float resolveFocusDistance(float nearPlane, float farPlane)
{
    if (pass.parameters[1].w < 0.5)
        return max(pass.parameters[0].x, 0.01);
    // All fragments address the same focus point, which remains texture-cache
    // friendly. A future exposure/metering graph pass can provide a filtered
    // scalar without reintroducing the legacy synchronous CPU depth readback.
    const float sampleDepth = depthTexture.Sample(saturate(pass.parameters[3].xy)).r;
    return sampleDepth > 0.000001
        ? linearizeDepth(sampleDepth, nearPlane, farPlane)
        : max(pass.parameters[0].x, 0.01);
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 center = sceneTexture.Sample(uv);
    const float nearPlane = max(pass.parameters[2].x, 0.001);
    const float farPlane = max(pass.parameters[2].y, nearPlane + 0.01);
    const float maximumRadius = max(pass.parameters[1].x, 0.0);
    if (maximumRadius <= 0.001)
        return center;

    const float rawDepth = depthTexture.Sample(uv).r;
    const float viewDepth = rawDepth <= 0.000001
        ? farPlane : linearizeDepth(rawDepth, nearPlane, farPlane);
    const float focusDistance = resolveFocusDistance(nearPlane, farPlane);
    const float focalLength = max(pass.parameters[0].y, 0.01);
    const float fStop = max(pass.parameters[0].z, 0.01);
    const float sensorWidth = max(pass.parameters[0].w, 0.01);
    const float subjectMm = max(viewDepth * 1000.0, focalLength + 0.01);
    const float focusMm = max(focusDistance * 1000.0, focalLength + 0.01);
    const float cocMm = focalLength * focalLength * (subjectMm - focusMm) /
                        max(fStop * subjectMm * (focusMm - focalLength), 0.0001);
    const float blurScale = cocMm < 0.0 ? pass.parameters[1].y : pass.parameters[1].z;
    const float cocPixels = 0.5 * abs(cocMm) / sensorWidth / max(pass.inverseResolution.x, 0.000001);
    const float radiusPixels = min(cocPixels * max(blurScale, 0.0), maximumRadius);
    if (radiusPixels < 0.35)
        return center;

    const uint sampleCount = pass.quality == 0 ? 8 : (pass.quality == 1 ? 16 : 24);
    const float goldenAngle = 2.39996323;
    float3 accumulated = center.rgb;
    float weight = 1.0;
    for (uint sampleIndex = 1; sampleIndex < sampleCount; ++sampleIndex)
    {
        const float fraction = float(sampleIndex) / float(sampleCount - 1);
        const float angle = float(sampleIndex) * goldenAngle;
        const float2 direction = float2(cos(angle), sin(angle));
        const float2 offset = direction * sqrt(fraction) * radiusPixels * pass.inverseResolution;
        const float2 sampleUv = saturate(uv + offset);
        const float sampleDepth = depthTexture.Sample(sampleUv).r;
        const float sampleViewDepth = sampleDepth <= 0.000001
            ? farPlane : linearizeDepth(sampleDepth, nearPlane, farPlane);
        // Suppress foreground bleeding into focused/background surfaces.
        const float depthWeight = sampleViewDepth + max(0.02, viewDepth * 0.01) < viewDepth && cocMm >= 0.0
            ? 0.15 : 1.0;
        accumulated += sceneTexture.Sample(sampleUv).rgb * depthWeight;
        weight += depthWeight;
    }
    return float4(accumulated / max(weight, 0.0001), center.a);
}
