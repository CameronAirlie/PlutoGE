struct VertexInput
{
    float2 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

[[vk::binding(0, 0)]]
cbuffer RmlUiParameters : register(b0, space0)
{
    column_major float4x4 transform;
    float2 translation;
    float2 padding;
};

[[vk::binding(0, 1)]] Sampler2D sourceTexture : register(t0, space1);

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(transform, float4(input.position + translation, 0.0, 1.0));
    // The legacy GL3 renderer draws into an intermediate layer and flips that
    // layer during its final composite. RHI draws directly into the scene's
    // bottom-up presentation target, so perform the equivalent flip here.
    output.position.y = -output.position.y;
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

 [shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    return input.color * sourceTexture.Sample(input.uv);
}
