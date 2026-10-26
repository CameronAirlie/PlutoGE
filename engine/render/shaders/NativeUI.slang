// Native UI: rounded-rect and glyph distance fields. Vertex layout mirrors
// ui::DrawVertex; inputs are declared in attribute-location order.
struct VertexInput
{
    float2 position : POSITION;     // target pixels, y-up
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;          // straight alpha
    float4 local : TEXCOORD1;       // xy: point relative to shape centre, zw: half size
    float4 radii : TEXCOORD2;       // corner radii (tl, tr, br, bl); glyphs: x = weight
    float4 params : TEXCOORD3;      // x: border/outline, y: softness, z: kind, w: inverted
    float4 borderColor : COLOR1;
    float4 clipRect : TEXCOORD4;    // target pixels
    float4 clipRadii : TEXCOORD5;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 local : TEXCOORD1;
    float4 radii : TEXCOORD2;
    float4 params : TEXCOORD3;
    float4 borderColor : COLOR1;
    float4 clipRect : TEXCOORD4;
    float4 clipRadii : TEXCOORD5;
    float2 pixel : TEXCOORD6;
};

[[vk::binding(0, 0)]]
cbuffer NativeUIParameters : register(b0, space0)
{
    column_major float4x4 transform; // target pixels to clip space
    float flipTextureY;
    float encodeSrgb;
    float2 reserved;
};

[[vk::binding(0, 1)]] Sampler2D sourceTexture : register(t0, space1);

static const float kGlyph = 1.0;

float3 linearToSrgb(float3 value)
{
    const float3 low = value * 12.92;
    const float3 high = 1.055 * pow(value, 1.0 / 2.4) - 0.055;
    return select(value <= 0.0031308, low, high);
}

// Signed distance to a rounded box centred at the origin (negative inside).
// y is up, so the top corners are at positive y.
float roundedBoxDistance(float2 p, float2 halfSize, float4 radii)
{
    const float radius = p.x > 0.0 ? (p.y > 0.0 ? radii.y : radii.z) : (p.y > 0.0 ? radii.x : radii.w);
    const float2 q = abs(p) - halfSize + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

// One-pixel anti-aliased coverage of a distance field.
float edgeCoverage(float distance)
{
    const float width = max(fwidth(distance), 1e-4);
    return saturate(0.5 - distance / width);
}

float4 premultiply(float4 color)
{
    return float4(color.rgb * color.a, color.a);
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(transform, float4(input.position, 0.0, 1.0));
    output.uv = float2(input.uv.x, flipTextureY > 0.5 ? 1.0 - input.uv.y : input.uv.y);
    output.color = input.color;
    output.local = input.local;
    output.radii = input.radii;
    output.params = input.params;
    output.borderColor = input.borderColor;
    output.clipRect = input.clipRect;
    output.clipRadii = input.clipRadii;
    output.pixel = input.position;
    return output;
}

float4 shadeGlyph(VertexOutput input)
{
    const float distance = sourceTexture.Sample(input.uv).r - 0.5 + input.radii.x;
    const float softness = input.params.y;
    const float width = max(fwidth(distance), 1e-4) * 0.7;
    const float fill = softness > 0.0 ? smoothstep(-softness, softness, distance)
                                      : smoothstep(-width, width, distance);
    const float4 fillColor = premultiply(input.color) * fill;
    if (input.params.x <= 0.0)
        return fillColor;
    const float outline = softness > 0.0 ? smoothstep(-softness, softness, distance + input.params.x)
                                         : smoothstep(-width, width, distance + input.params.x);
    return fillColor + premultiply(input.borderColor) * outline * (1.0 - fillColor.a);
}

float4 shadeShape(VertexOutput input)
{
    const float distance = roundedBoxDistance(input.local.xy, input.local.zw, input.radii);
    const float softness = input.params.y;
    float inside = softness > 0.0 ? 1.0 - smoothstep(-softness, softness, distance) : edgeCoverage(distance);
    if (input.params.w > 0.5)
        inside = 1.0 - inside;

    float4 texel = sourceTexture.Sample(input.uv);
    // Render textures hold display colour in an sRGB image; sampling decoded it.
    if (encodeSrgb > 0.5)
        texel.rgb = linearToSrgb(saturate(texel.rgb));
    const float4 fill = premultiply(input.color * texel);

    const float border = input.params.x;
    if (border > 0.0 && softness <= 0.0)
    {
        const float interior = edgeCoverage(distance + border);
        return fill * interior + premultiply(input.borderColor) * saturate(inside - interior);
    }
    return fill * inside;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    float4 result = input.params.z > kGlyph - 0.5 ? shadeGlyph(input) : shadeShape(input);
    const float2 clipCentre = (input.clipRect.xy + input.clipRect.zw) * 0.5;
    const float2 clipHalf = (input.clipRect.zw - input.clipRect.xy) * 0.5;
    result *= edgeCoverage(roundedBoxDistance(input.pixel - clipCentre, clipHalf, input.clipRadii));
    return result;
}
