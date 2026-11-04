#include "Fullscreen.slang"
#include "PostProcessParameters.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(6, 0)]] Sampler2D exposureTexture : register(t6, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID) { return fullscreenVertex(vertexId); }

float luminance(float3 color)
{
    return max(dot(max(color, 0.0), float3(0.2126, 0.7152, 0.0722)), 0.0001);
}

// Mirrors AutoExposureEffect::ComputeTargetExposure. Lanes: 0 = key, min,
// max, adapt up; 1 = adapt down, low-light luminance, low-light darkening.
float targetExposure(float averageLuminance)
{
    const float key = pass.parameters[0].x;
    const float minExposure = pass.parameters[0].y;
    const float maxExposure = pass.parameters[0].z;
    const float lowLightLuminance = pass.parameters[1].y;
    const float lowLightDarkening = pass.parameters[1].z;
    // Scenes darker than the low-light level lose `darkening` stops of
    // exposure per stop, so night stays dark instead of adapting to mid-grey.
    const float stopsBelowLowLight = max(0.0, log2(lowLightLuminance / averageLuminance));
    const float exposure = key / averageLuminance * exp2(-stopsBelowLowLight * lowLightDarkening);
    return clamp(exposure, min(minExposure, maxExposure), max(minExposure, maxExposure));
}

[shader("fragment")]
float meterMain(FullscreenVertexOutput input) : SV_Target
{
    float logarithmicLuminance = 0.0;
    const uint sampleExtent = 16;
    for (uint y = 0; y < sampleExtent; ++y)
    {
        for (uint x = 0; x < sampleExtent; ++x)
        {
            float2 uv = (float2(x, y) + 0.5) / float(sampleExtent);
            uv = orientFullscreenUv(uv, pass.flipY);
            logarithmicLuminance += log(luminance(sceneTexture.Sample(uv).rgb));
        }
    }
    const float averageLuminance = exp(logarithmicLuminance / float(sampleExtent * sampleExtent));
    const float target = targetExposure(averageLuminance);
    if (pass.parameters[5].w < 0.5)
        return target;
    const float previous = exposureTexture.Sample(float2(0.5, 0.5)).r;
    const float rate = target > previous ? pass.parameters[0].w : pass.parameters[1].x;
    return lerp(previous, target, saturate(rate));
}

[shader("fragment")]
float4 applyMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 color = sceneTexture.Sample(uv);
    const float exposure = max(exposureTexture.Sample(float2(0.5, 0.5)).r, 0.0);
    return float4(color.rgb * exposure, color.a);
}
