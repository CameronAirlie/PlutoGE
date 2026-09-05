#include "Fullscreen.slang"
[[vk::binding(1, 0)]] Sampler2D source : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthSource : register(t2, space0);
struct SnapshotOutput
{
    float4 color : SV_Target0;
    float depth : SV_Target1;
};
[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenVertex(vertexId);
}
[shader("fragment")]
SnapshotOutput fragmentMain(FullscreenVertexOutput input)
{
    uint width, height;
    source.GetDimensions(width, height);
    // SV_Position follows the native attachment origin on both APIs.
    float2 uv = input.position.xy / float2(width, height);
    SnapshotOutput output;
    output.color = source.SampleLevel(uv, 0.0);
    output.depth = depthSource.SampleLevel(uv, 0.0).r;
    return output;
}
