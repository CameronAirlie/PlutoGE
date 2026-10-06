// Raw scalar layouts: SourceVertex is 88 bytes; BasicVertex is 72 bytes.
[[vk::binding(0,0)]] cbuffer Parameters : register(b0) { uint4 config; };
[[vk::binding(1,0)]] ByteAddressBuffer sourceVertices : register(t1);
[[vk::binding(2,0)]] StructuredBuffer<float4> palettes : register(t2);
[[vk::binding(3,0)]] RWByteAddressBuffer outputVertices : register(u3);

struct Affine { float3 c0, c1, c2, c3; };
Affine blend(uint vertex, uint paletteOffset)
{
    Affine m = {float3(0),float3(0),float3(0),float3(0)};
    float total = 0;
    for (uint influence = 0; influence < 4; ++influence)
    {
        int joint = asint(sourceVertices.Load(vertex + 56 + influence * 4));
        float weight = asfloat(sourceVertices.Load(vertex + 72 + influence * 4));
        if (joint < 0 || uint(joint) >= config.y || !isfinite(weight) || weight <= 0) continue;
        uint first = (paletteOffset + uint(joint)) * 4;
        m.c0 += palettes[first].xyz * weight;
        m.c1 += palettes[first + 1].xyz * weight;
        m.c2 += palettes[first + 2].xyz * weight;
        m.c3 += palettes[first + 3].xyz * weight;
        total += weight;
    }
    if (total > .0001)
    {
        m.c0 /= total; m.c1 /= total; m.c2 /= total; m.c3 /= total;
    }
    else
    {
        m.c0 = float3(1,0,0); m.c1 = float3(0,1,0); m.c2 = float3(0,0,1); m.c3 = 0;
    }
    return m;
}
float3 transformPosition(Affine m, float3 p) { return m.c0*p.x + m.c1*p.y + m.c2*p.z + m.c3; }
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
    uint index = (group.y * 65535 + group.x) * 64 + thread.x;
    if (index >= config.x) return;
    uint source = index * 88, destination = index * 72;
    float3 position = asfloat(sourceVertices.Load3(source));
    float3 normal = asfloat(sourceVertices.Load3(source + 12));
    float4 tangent = asfloat(sourceVertices.Load4(source + 32));
    Affine current = blend(source,0), previous = blend(source,config.y);
    float3 c0 = cross(current.c1,current.c2), c1 = cross(current.c2,current.c0), c2 = cross(current.c0,current.c1);
    float determinant = dot(current.c0,c0);
    float3 n = abs(determinant) > 1e-8 ? (c0*normal.x + c1*normal.y + c2*normal.z) / determinant : normal;
    if (!safeNormalize(n)) n = float3(0,1,0);
    float3 t = current.c0*tangent.x + current.c1*tangent.y + current.c2*tangent.z;
    t -= n * dot(n,t);
    if (!safeNormalize(t))
    {
        t = abs(n.y) < .99 ? float3(n.z,0,-n.x) : float3(0,-n.z,n.y);
        safeNormalize(t);
    }
    float handedness = (tangent.w == 0 ? 1 : tangent.w) * (determinant < 0 ? -1 : 1);
    outputVertices.Store3(destination,asuint(transformPosition(current,position)));
    outputVertices.Store3(destination + 12,asuint(n));
    outputVertices.Store2(destination + 24,sourceVertices.Load2(source + 24));
    outputVertices.Store4(destination + 32,asuint(float4(t,handedness)));
    outputVertices.Store4(destination + 48,asuint(float4(transformPosition(previous,position),1)));
    outputVertices.Store2(destination + 64,sourceVertices.Load2(source + 48));
}
