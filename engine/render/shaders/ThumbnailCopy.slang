#include "Fullscreen.slang"
[[vk::binding(1, 0)]] Sampler2D source : register(t1, space0);
[shader("vertex")] FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID) {
    return fullscreenVertex(vertexId);
}[shader("fragment")] float4 fragmentMain(FullscreenVertexOutput input)
    : SV_Target
{
    uint width, height;
    source.GetDimensions(width, height);
    return source.SampleLevel(input.position.xy / float2(width, height), 0.0);
}
