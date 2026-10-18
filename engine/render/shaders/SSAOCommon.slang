// Shared AO geometry encoding. View depth stays positive and linear; half-float
// precision is sufficient for relative history rejection without quantizing reverse Z.
float2 encodeOctahedral(float3 normal)
{
    normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
    if (normal.z < 0.0)
        normal.xy = (1.0 - abs(normal.yx)) *
                    float2(normal.x >= 0.0 ? 1.0 : -1.0, normal.y >= 0.0 ? 1.0 : -1.0);
    return normal.xy * 0.5 + 0.5;
}

float3 decodeOctahedral(float2 encoded)
{
    const float2 f = encoded * 2.0 - 1.0;
    float3 normal = float3(f, 1.0 - abs(f.x) - abs(f.y));
    const float t = saturate(-normal.z);
    normal.xy += float2(normal.x >= 0.0 ? -t : t, normal.y >= 0.0 ? -t : t);
    return normalize(normal);
}

float3 ssaoViewPosition(float2 textureUv, float depth, PostProcessParameterBlock pass)
{
    float2 ndc = textureUv * 2.0 - 1.0;
    if (pass.flipY != 0) ndc.y = -ndc.y;
    const float z = -depth;
    const float w = pass.projection[3][2] * z + pass.projection[3][3];
    return float3((ndc.x * w - pass.projection[0][2] * z - pass.projection[0][3]) / pass.projection[0][0],
                  (ndc.y * w - pass.projection[1][2] * z - pass.projection[1][3]) / pass.projection[1][1], z);
}

// AO is visibility of indirect illumination, not a multiplier on direct light.
// Include additive lighting composites since geometry (SSGI/VCTGI) along with
// geometry's diffuse ambient; the original scene retains direct/emissive light.
float4 ssaoComposite(float4 scene, float3 indirect, float ao, bool diagnostic)
{
    return diagnostic ? float4(ao.xxx,1.0)
        : float4(max(scene.rgb-indirect*(1.0-ao),0.0),scene.a);
}
