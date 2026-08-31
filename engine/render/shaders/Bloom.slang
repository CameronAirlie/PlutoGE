#include "Fullscreen.slang"
#include "PostProcessParameters.slang"

[[vk::binding(0, 0)]] cbuffer BloomParameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sourceTexture : register(t1, space0);
[[vk::binding(7, 0)]] Sampler2D auxiliaryTexture : register(t7, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID) { return fullscreenVertex(vertexId); }

float3 tent9(Sampler2D textureSampler, float2 uv, float2 texel, float radius)
{
    const float2 d = texel * radius;
    float3 result = textureSampler.Sample(uv).rgb * 4.0;
    result += textureSampler.Sample(uv + float2(d.x, 0.0)).rgb * 2.0;
    result += textureSampler.Sample(uv - float2(d.x, 0.0)).rgb * 2.0;
    result += textureSampler.Sample(uv + float2(0.0, d.y)).rgb * 2.0;
    result += textureSampler.Sample(uv - float2(0.0, d.y)).rgb * 2.0;
    result += textureSampler.Sample(uv + d).rgb + textureSampler.Sample(uv - d).rgb;
    result += textureSampler.Sample(uv + float2(d.x, -d.y)).rgb;
    result += textureSampler.Sample(uv + float2(-d.x, d.y)).rgb;
    return max(result * (1.0 / 16.0), 0.0);
}

[shader("fragment")]
float4 prefilterMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float3 color = tent9(sourceTexture, uv, pass.inverseResolution, 1.0);
    const float brightness = max(color.r, max(color.g, color.b));
    const float threshold = max(pass.parameters[0].y, 0.0);
    const float knee = max(threshold * saturate(pass.parameters[0].z), 0.0001);
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);
    const float contribution = max(soft, brightness - threshold) / max(brightness, 0.00001);
    return float4(color * max(contribution, 0.0), 1.0);
}

[shader("fragment")]
float4 downsampleMain(FullscreenVertexOutput input) : SV_Target
{
    return float4(tent9(sourceTexture, orientFullscreenUv(input.uv, pass.flipY), pass.inverseResolution, 1.0), 1.0);
}

[shader("fragment")]
float4 upsampleMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float3 base = sourceTexture.Sample(uv).rgb;
    const float3 low = tent9(auxiliaryTexture, uv, pass.inverseResolution, max(pass.parameters[0].w, 0.25));
    return float4(max(base + low, 0.0), 1.0);
}

[shader("fragment")]
float4 compositeMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 scene = sourceTexture.Sample(uv);
    return float4(scene.rgb + max(auxiliaryTexture.Sample(uv).rgb, 0.0) * max(pass.parameters[0].x, 0.0), scene.a);
}
