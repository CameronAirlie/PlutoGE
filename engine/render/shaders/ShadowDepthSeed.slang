#include "Fullscreen.slang"
[[vk::binding(1, 0)]] Sampler2D cachedDepth : register(t1, space0);
struct SeedOutput { float color : SV_Target0; float depth : SV_Depth; };
[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }
[shader("fragment")]
SeedOutput fragmentMain(FullscreenVertexOutput input)
{
    SeedOutput output;
    // Exact texel copy in backend texture coordinates, including the clear
    // depth. No filtering, reprojection, bias, or resolution change is involved.
    output.depth = cachedDepth.Load(int3(int2(input.position.xy), 0)).r;
    output.color = output.depth;
    return output;
}
