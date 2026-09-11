#include "Fullscreen.slang"
#include "ColorOperations.slang"

struct ColorOperation
{
    float4 control; // type: 0 tone map, 1 gamma, 2 grading; exposure, gamma, reserved
    float4 parameters[6];
};
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0)
{
    float4 header; // operation count, flipY, time, reserved
    ColorOperation operations[8];
};
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, (uint)header.y);
    float3 color = sceneTexture.Sample(uv).rgb;
    for (uint index = 0; index < min((uint)header.x, 8u); ++index)
    {
        const ColorOperation operation = operations[index];
        if (operation.control.x < 0.5)
            color = pow(aces(color * max(operation.control.y, 0.0)), 1.0 / max(operation.control.z, 0.001));
        else if (operation.control.x < 1.5)
            color = pow(max(color, 0.0), 1.0 / max(operation.control.z, 0.001));
        else
            color = applyColorGrading(color, uv, input.position.xy, header.z, operation.parameters);
    }
    return float4(color, 1);
}
