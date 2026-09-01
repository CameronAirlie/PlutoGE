#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[[vk::binding(4, 0)]] Sampler2D materialTexture : register(t4, space0);
[[vk::binding(5, 0)]] Sampler2D motionTexture : register(t5, space0);

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 scene = sceneTexture.Sample(uv);
    if (pass.quality == 0u) return scene;

    // The RHI path exposes useful G-buffer diagnostics here. World-space GI debug
    // views remain owned by their lighting-volume path and are not fabricated.
    if (pass.quality == 1u)
        return float4(decodeNormal(normalTexture.Sample(uv).xyz) * 0.5 + 0.5, 1.0);
    if (pass.quality == 2u)
        return float4(materialTexture.Sample(uv).rgb, 1.0);
    if (pass.quality == 3u)
    {
        const float2 motion = motionTexture.Sample(uv).xy;
        return float4(abs(motion) * 16.0, depthTexture.Sample(uv).r, 1.0);
    }
    return float4(depthTexture.Sample(uv).rrr, 1.0);
}
