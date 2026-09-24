cbuffer LoadingScreenParameters : register(b0)
{
    float4 screenScale;
};

struct VertexInput
{
    float2 position : POSITION;
    float3 color : COLOR0;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};
[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position * screenScale.xy, 0, 1);
    output.color = input.color;
    return output;
}
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(input.color, 1);
}
