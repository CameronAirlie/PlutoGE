struct FullscreenVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

FullscreenVertexOutput fullscreenVertex(uint vertexId)
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0)
    };
    FullscreenVertexOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.uv = output.position.xy * 0.5 + 0.5;
    return output;
}

float2 orientFullscreenUv(float2 uv, uint flipY)
{
    return float2(uv.x, flipY != 0 ? 1.0 - uv.y : uv.y);
}
