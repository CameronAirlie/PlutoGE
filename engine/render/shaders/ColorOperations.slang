#include "../include/PlutoGE/render/PostProcessLayout.h"
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

// AgX (Troy Sobotka), in Benjamin Wrensch's polynomial fit of the default look. Bright saturated colours bleach
// towards white as they would on film, instead of skewing in hue (fire turning yellow, blue lights cyan) as per-channel
// curves such as the ACES fit do. Returns linear values.
float3 agx(float3 value)
{
    // Inset into AgX's working space, then log2 encoding over [-12.47, 4.03] EV.
    value = value.r * float3(0.842479062253094, 0.0423282422610123, 0.0423756549057051) +
            value.g * float3(0.0784335999999992, 0.878468636469772, 0.0784336) +
            value.b * float3(0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float minimumEv = -12.47393, maximumEv = 4.026069;
    value = (clamp(log2(max(value, 1.0e-10)), minimumEv, maximumEv) - minimumEv) / (maximumEv - minimumEv);
    const float3 x2 = value * value, x4 = x2 * x2;
    value = 15.5 * x4 * x2 - 40.14 * x4 * value + 31.96 * x4 - 6.868 * x2 * value + 0.4298 * x2 +
            0.1191 * value - 0.00232;
    // Outset, then from the display encoding (2.2) back to linear, which the gamma stage encodes again.
    value = value.r * float3(1.19687900512017, -0.0528968517574562, -0.0529716355144438) +
            value.g * float3(-0.0980208811401368, 1.15190312990417, -0.0980434501171241) +
            value.b * float3(-0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    return pow(saturate(value), 2.2);
}

// Khronos PBR Neutral: reproduces base colours exactly up to 0.76 (so materials look as authored in other viewers)
// and compresses only highlights, desaturating them as they approach white. Returns linear values.
float3 pbrNeutral(float3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    const float minimum = min(color.r, min(color.g, color.b));
    color -= minimum < 0.08 ? minimum - 6.25 * minimum * minimum : 0.04;
    const float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return max(color, 0.0);
    const float d = 1.0 - startCompression;
    const float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    const float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, newPeak.xxx, g);
}

// The tone mapping curves (ToneMappingCurve): 0 ACES (Narkowicz fit), 1 AgX, 2 Khronos PBR Neutral.
float3 toneMapCurve(float3 color, float curve)
{
    if (curve > 1.5) return pbrNeutral(max(color, 0.0));
    if (curve > 0.5) return agx(max(color, 0.0));
    return aces(color);
}

uint colorGrainHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

// Integer hash keeps the grain uncorrelated across pixels and frames; a sin-based hash of
// (pixel + time) drifts diagonally and loses precision, which shows up as scrolling bands.
float colorGrain(float2 pixel, float time)
{
    const uint2 coord = uint2(max(pixel, 0.0));
    uint value = coord.x * 0x9e3779b9u;
    value ^= coord.y * 0x85ebca6bu;
    value ^= colorGrainHash(asuint(time)) * 0xc2b2ae35u;
    return float(colorGrainHash(value) & 0x00ffffffu) / 16777216.0;
}

float3 applyColorGrading(float3 color, float2 uv, float2 pixel, float time, float4 parameters[PLUTO_POST_PROCESS_LANES])
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
