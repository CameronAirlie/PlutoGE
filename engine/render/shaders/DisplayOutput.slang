#include "Fullscreen.slang"

[[vk::binding(1, 0)]]
Sampler2D sceneTexture : register(t1, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    return fullscreenVertex(vertexId);
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    return float4(saturate(sceneTexture.Sample(input.uv).rgb), 1.0);
}
