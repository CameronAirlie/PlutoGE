#include "Fullscreen.slang"
#include "PostProcessParameters.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[[vk::binding(5, 0)]] Sampler2D motionTexture : register(t5, space0);
[[vk::binding(6, 0)]] Sampler2D historyTexture : register(t6, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID) { return fullscreenVertex(vertexId); }

float luma(float3 color) { return dot(color, float3(0.2126, 0.7152, 0.0722)); }

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float2 textureOrientation = float2(1.0, pass.flipY != 0 ? -1.0 : 1.0);
    const float2 currentJitterUv = pass.parameters[2].xy * textureOrientation;
    const float2 previousJitterUv = pass.parameters[2].zw * textureOrientation;
    const float2 currentUv = saturate(uv - currentJitterUv);
    const float4 current = sceneTexture.Sample(currentUv);
    if (pass.parameters[5].w < 0.5)
        return current;

    const float2 encodedMotion = (motionTexture.Sample(currentUv).xy * 2.0 - 1.0) *
                                 textureOrientation;
    const float2 motion = encodedMotion + currentJitterUv - previousJitterUv;
    const float2 historyUv = uv - motion;
    if (any(historyUv < 0.0) || any(historyUv > 1.0))
        return current;

    float3 neighborhoodMin = current.rgb;
    float3 neighborhoodMax = current.rgb;
    float3 lowpass = 0.0;
    float lowpassWeight = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float2 sampleUv = saturate(currentUv + float2(x, y) * pass.inverseResolution);
            const float3 sampleColor = sceneTexture.Sample(sampleUv).rgb;
            neighborhoodMin = min(neighborhoodMin, sampleColor);
            neighborhoodMax = max(neighborhoodMax, sampleColor);
            const float kernelWeight = (x == 0 && y == 0) ? 4.0 : ((x == 0 || y == 0) ? 2.0 : 1.0);
            lowpass += sampleColor * kernelWeight;
            lowpassWeight += kernelWeight;
        }
    }

    float3 history = historyTexture.Sample(historyUv).rgb;
    history = clamp(history, neighborhoodMin, neighborhoodMax);
    const float currentDepth = depthTexture.Sample(currentUv).r;
    const float historyDepth = depthTexture.Sample(historyUv).r;
    const float3 currentNormal = normalize(normalTexture.Sample(currentUv).xyz * 2.0 - 1.0);
    const float3 historyNormal = normalize(normalTexture.Sample(historyUv).xyz * 2.0 - 1.0);
    const float depthValidity = 1.0 - smoothstep(pass.parameters[1].x,
                                                 pass.parameters[1].x * 2.0,
                                                 abs(currentDepth - historyDepth));
    const float normalValidity = smoothstep(pass.parameters[1].y - 0.12,
                                             pass.parameters[1].y,
                                             dot(currentNormal, historyNormal));
    const float motionPixels = length(motion / max(pass.inverseResolution, float2(0.000001)));
    const float baseHistoryWeight = lerp(pass.parameters[0].y, pass.parameters[0].z,
                                         saturate(motionPixels * 0.35));
    const float velocityValidity = saturate(1.0 - length(motion) * pass.parameters[1].z);
    const float lightingDelta = abs(luma(history) - luma(current.rgb)) / max(luma(current.rgb), 0.05);
    const float lightingValidity = 1.0 - smoothstep(0.12, 0.4, lightingDelta);
    const float validity = depthValidity * normalValidity * velocityValidity * lightingValidity;
    const float historyWeight = saturate(min(pass.parameters[0].x, baseHistoryWeight) * validity);
    float3 resolved = lerp(current.rgb, history, historyWeight);
    if (pass.parameters[0].w > 0.0)
    {
        const float3 detail = current.rgb - lowpass / max(lowpassWeight, 0.0001);
        resolved += detail * pass.parameters[0].w * (1.0 - historyWeight);
    }
    return float4(max(resolved, 0.0), current.a);
}
