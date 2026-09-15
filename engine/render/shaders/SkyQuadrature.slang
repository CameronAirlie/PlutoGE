#include "Fullscreen.slang"
[[vk::binding(0, 0)]] cbuffer SkyParameters : register(b0, space0)
{
    float4 physicalSkyParameters[6];
    float4 physicalSkySettings;
};
static const float PI = 3.14159265359;
#include "PhysicalSkyEnvironment.slang"
[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }
[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target0
{
    const uint tap = min(uint(input.position.x), 13u);
    return float4(samplePhysicalSkyEnvironment(skyQuadratureDirections[tap], 0.0), 1.0);
}
