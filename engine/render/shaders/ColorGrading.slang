#include "Fullscreen.slang"
#include "PostProcessParameters.slang"

[[vk::binding(0, 0)]] cbuffer ColorGradingParameters : register(b0, space0)
{
    PostProcessParameterBlock pass;
};
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID) { return fullscreenVertex(vertexId); }

float hashNoise(float2 pixel)
{
    return frac(sin(dot(pixel + pass.time, float2(12.9898, 78.233))) * 43758.5453);
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    float3 color = sceneTexture.Sample(uv).rgb;
    const float4 primary = pass.parameters[0]; // brightness, contrast, saturation, temperature
    const float4 secondary = pass.parameters[1]; // tint, vibrance, lift, grading gamma
    const float4 finish = pass.parameters[2]; // gain, fade, vignette, grain
    const float4 shadowTone = pass.parameters[3];
    const float4 highlightTone = pass.parameters[4];

    color += primary.x;
    color = (color - 0.5) * max(primary.y, 0.0) + 0.5;
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(luminance.xxx, color, max(primary.z, 0.0));
    color *= float3(1.0 + 0.1 * max(primary.w, 0.0),
                    1.0 - 0.05 * abs(primary.w),
                    1.0 + 0.1 * max(-primary.w, 0.0));
    color *= float3(1.0 + 0.05 * secondary.x, 1.0, 1.0 - 0.05 * secondary.x);

    luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    const float saturation = max(color.r, max(color.g, color.b)) - min(color.r, min(color.g, color.b));
    color = lerp(luminance.xxx, color, 1.0 + secondary.y * (1.0 - saturate(saturation)));
    color = pow(max(color + secondary.z, 0.0), 1.0 / max(secondary.w, 0.001)) * max(finish.x, 0.0);
    color = lerp(color, luminance.xxx, saturate(finish.y));

    luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    const float crossover = lerp(0.25, 0.75, saturate(pass.parameters[5].x));
    const float shadowWeight = 1.0 - smoothstep(crossover - 0.20, crossover + 0.12, luminance);
    const float highlightWeight = smoothstep(crossover - 0.12, crossover + 0.24, luminance);
    const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);
    const float3 normalizedShadow = luminance * shadowTone.rgb / max(dot(shadowTone.rgb, lumaWeights), 0.001);
    const float3 normalizedHighlight = luminance * highlightTone.rgb / max(dot(highlightTone.rgb, lumaWeights), 0.001);
    color = lerp(color, normalizedShadow, shadowWeight * saturate(shadowTone.a));
    color = lerp(color, normalizedHighlight, highlightWeight * saturate(highlightTone.a));

    const float2 centered = uv * 2.0 - 1.0;
    const float vignette = 1.0 - saturate(dot(centered, centered) * 0.55) * saturate(finish.z);
    color *= vignette;
    color += (hashNoise(input.position.xy) - 0.5) * saturate(finish.w);
    return float4(max(color, 0.0), 1.0);
}
