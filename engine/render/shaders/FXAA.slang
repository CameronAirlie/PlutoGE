#include "Fullscreen.slang"
#include "PostProcessParameters.slang"

[[vk::binding(0, 0)]] cbuffer FxaaParameters : register(b0, space0)
{
    PostProcessParameterBlock pass;
};

[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenVertex(vertexId);
}

float luma(float3 color)
{
    return dot(color, float3(0.299, 0.587, 0.114));
}

float3 sampleScene(float2 uv)
{
    return sceneTexture.Sample(uv).rgb;
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float2 texel = pass.inverseResolution;
    const float3 center = sampleScene(uv);
    const float3 north = sampleScene(uv + float2(0.0, texel.y));
    const float3 south = sampleScene(uv - float2(0.0, texel.y));
    const float3 east = sampleScene(uv + float2(texel.x, 0.0));
    const float3 west = sampleScene(uv - float2(texel.x, 0.0));

    const float centerLuma = luma(center);
    const float northLuma = luma(north);
    const float southLuma = luma(south);
    const float eastLuma = luma(east);
    const float westLuma = luma(west);
    const float minimumLuma = min(centerLuma, min(min(northLuma, southLuma), min(eastLuma, westLuma)));
    const float maximumLuma = max(centerLuma, max(max(northLuma, southLuma), max(eastLuma, westLuma)));
    if (maximumLuma - minimumLuma < max(0.0312, maximumLuma * 0.125))
        return float4(center, 1.0);

    float2 direction = float2(
        -((northLuma + southLuma) - 2.0 * centerLuma),
        (eastLuma + westLuma) - 2.0 * centerLuma);
    const float directionReduce = max((northLuma + southLuma + eastLuma + westLuma) * 0.03125, 0.0078125);
    direction = clamp(direction / (min(abs(direction.x), abs(direction.y)) + directionReduce), -8.0, 8.0) * texel;

    float3 result = 0.5 * (
        sampleScene(uv + direction * (1.0 / 3.0 - 0.5)) +
        sampleScene(uv + direction * (2.0 / 3.0 - 0.5)));
    if (pass.quality != 0)
    {
        result = 0.25 * (
            sampleScene(uv - direction * 0.5) +
            sampleScene(uv - direction / 6.0) +
            sampleScene(uv + direction / 6.0) +
            sampleScene(uv + direction * 0.5));
    }
    return float4(result, 1.0);
}
