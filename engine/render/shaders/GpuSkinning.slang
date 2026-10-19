// Raw scalar layouts: SourceVertex is 68 bytes; BasicVertex is 72 bytes.
// SourceVertex: position f32x3 | normal snorm16x3 + tangent snorm16x4 + pad |
//               uv f32x2 | uv2 f32x2 | joints u16x4 | weights f32x4.
// config: x end of the deformed slot range (an LOD prefix), y joints, z first
//         palette row, w bit 0 = the range still holds last frame's positions.
// range:  x first slot to deform; a later view may extend an earlier prefix.
[[vk::binding(0,0)]] cbuffer Parameters : register(b0) { uint4 config; uint4 range; };
[[vk::binding(1,0)]] ByteAddressBuffer sourceVertices : register(t1);
// Three float4 rows per joint: the affine part of each column-major matrix.
[[vk::binding(2,0)]] StructuredBuffer<float4> palettes : register(t2);
[[vk::binding(3,0)]] RWByteAddressBuffer outputVertices : register(u3);

struct Affine { float4 r0, r1, r2; };
Affine blend(uint vertex)
{
    Affine m = {float4(0),float4(0),float4(0)};
    float total = 0;
    uint2 packedJoints = sourceVertices.Load2(vertex + 44);
    float4 weights = asfloat(sourceVertices.Load4(vertex + 52));
    for (uint influence = 0; influence < 4; ++influence)
    {
        uint joint = (packedJoints[influence >> 1] >> ((influence & 1) * 16)) & 0xffff;
        float weight = weights[influence];
        if (joint >= config.y || !isfinite(weight) || weight <= 0) continue;
        uint first = config.z + joint * 3;
        m.r0 += palettes[first] * weight;
        m.r1 += palettes[first + 1] * weight;
        m.r2 += palettes[first + 2] * weight;
        total += weight;
    }
    if (total > .0001)
    {
        m.r0 /= total; m.r1 /= total; m.r2 /= total;
    }
    else
    {
        m.r0 = float4(1,0,0,0); m.r1 = float4(0,1,0,0); m.r2 = float4(0,0,1,0);
    }
    return m;
}
float3 transformPosition(Affine m, float3 p)
{
    float4 h = float4(p,1);
    return float3(dot(m.r0,h), dot(m.r1,h), dot(m.r2,h));
}
float lowSnorm(uint bits) { return max(float(asint(bits << 16) >> 16) / 32767.0, -1.0); }
float highSnorm(uint bits) { return max(float(asint(bits) >> 16) / 32767.0, -1.0); }
bool safeNormalize(inout float3 v)
{
    float squared = dot(v,v);
    if (squared <= 1e-12 || !isfinite(squared)) return false;
    v /= sqrt(squared);
    return true;
}
[shader("compute")]
[numthreads(64,1,1)]
void computeMain(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    uint index = range.x + (group.y * 65535 + group.x) * 64 + thread.x;
    if (index >= config.x) return;
    uint source = index * 68, destination = index * 72;
    float3 position = asfloat(sourceVertices.Load3(source));
    uint4 frame = sourceVertices.Load4(source + 12);
    float3 normal = float3(lowSnorm(frame.x), highSnorm(frame.x), lowSnorm(frame.y));
    float4 tangent = float4(highSnorm(frame.y), lowSnorm(frame.z), highSnorm(frame.z), lowSnorm(frame.w));
    Affine current = blend(source);
    float3 c0 = float3(current.r0.x,current.r1.x,current.r2.x);
    float3 c1 = float3(current.r0.y,current.r1.y,current.r2.y);
    float3 c2 = float3(current.r0.z,current.r1.z,current.r2.z);
    float3 n0 = cross(c1,c2), n1 = cross(c2,c0), n2 = cross(c0,c1);
    float determinant = dot(c0,n0);
    float3 n = abs(determinant) > 1e-8 ? (n0*normal.x + n1*normal.y + n2*normal.z) / determinant : normal;
    if (!safeNormalize(n)) n = float3(0,1,0);
    float3 t = c0*tangent.x + c1*tangent.y + c2*tangent.z;
    t -= n * dot(n,t);
    if (!safeNormalize(t))
    {
        t = abs(n.y) < .99 ? float3(n.z,0,-n.x) : float3(0,-n.z,n.y);
        safeNormalize(t);
    }
    float handedness = (tangent.w == 0 ? 1 : tangent.w) * (determinant < 0 ? -1 : 1);
    float3 skinned = transformPosition(current,position);
    // Each thread owns its vertex, so last frame's position is read before it
    // is replaced. This avoids blending a second, previous-frame palette.
    float3 previous = (config.w & 1) != 0 ? asfloat(outputVertices.Load3(destination)) : skinned;
    outputVertices.Store3(destination,asuint(skinned));
    outputVertices.Store3(destination + 12,asuint(n));
    outputVertices.Store2(destination + 24,sourceVertices.Load2(source + 28));
    outputVertices.Store4(destination + 32,asuint(float4(t,handedness)));
    outputVertices.Store4(destination + 48,asuint(float4(previous,1)));
    outputVertices.Store2(destination + 64,sourceVertices.Load2(source + 36));
}
