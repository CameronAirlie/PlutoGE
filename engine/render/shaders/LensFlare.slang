#include "Fullscreen.slang"
#include "PostProcessParameters.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID) { return fullscreenVertex(vertexId); }

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 scene = sceneTexture.Sample(uv);
    const float2 direction = 0.5 - uv;
    const float intensity = max(pass.parameters[0].x, 0.0);
    const float threshold = max(pass.parameters[0].y, 0.0);
    const float scale = max(pass.parameters[0].z, 0.01);
    const float dispersal = max(pass.parameters[0].w, 0.0);
    float3 flare = 0.0;
    const float3 tint[4] = {float3(1.0, 0.65, 0.35), float3(0.35, 0.7, 1.0),
                            float3(0.7, 1.0, 0.45), float3(1.0, 0.4, 0.7)};
    for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex)
    {
        const float distance = (float(sampleIndex) + 1.0) * dispersal * 0.35 * scale;
        const float3 sampleColor = sceneTexture.Sample(saturate(uv + direction * distance)).rgb;
        const float brightness = max(sampleColor.r, max(sampleColor.g, sampleColor.b));
        flare += sampleColor * tint[sampleIndex] * smoothstep(threshold, threshold + 0.25, brightness);
    }
    return float4(scene.rgb + flare * intensity * 0.25, scene.a);
}
