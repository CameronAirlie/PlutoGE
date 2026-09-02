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
    float clipYSign;
    float padding;
};

[[vk::binding(0, 1)]] Sampler2D sourceTexture : register(t0, space1);

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(transform, float4(input.position + translation, 0.0, 1.0));
    // Vulkan's negative-height viewport and RmlUi's top-left orthographic
    // projection otherwise invert Y twice. OpenGL uses +1; Vulkan uses -1.
    output.position.y *= clipYSign;
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

 [shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    return input.color * sourceTexture.Sample(input.uv);
}
