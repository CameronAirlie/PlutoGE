// Shared by standalone and fused passes; keep authored operation order.
float3 aces(float3 value)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((value * (a * value + b)) / (value * (c * value + d) + e));
}

float colorGrain(float2 pixel, float time)
{
    return frac(sin(dot(pixel + time, float2(12.9898, 78.233))) * 43758.5453);
}

float3 applyColorGrading(float3 color, float2 uv, float2 pixel, float time, float4 parameters[6])
{
    const float4 primary = parameters[0]; // brightness, contrast, saturation, temperature
    const float4 secondary = parameters[1]; // tint, vibrance, lift, grading gamma
    const float4 finish = parameters[2]; // gain, fade, vignette, grain
    const float4 shadowTone = parameters[3];
    const float4 highlightTone = parameters[4];

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
    const float crossover = lerp(0.25, 0.75, saturate(parameters[5].x));
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
    color += (colorGrain(pixel, time) - 0.5) * saturate(finish.w);
    return max(color, 0.0);
}
