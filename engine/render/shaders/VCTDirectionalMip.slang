struct MipParameters
{
    uint axis;
    int sign;
    uint cascadeIndex;
    uint cascadeMipSize;
    uint sourceMip;
    uint3 padding;
};

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { MipParameters pass; };
[[vk::binding(1, 0)]] Sampler3D sourceVolume : register(t1, space0);
[[vk::binding(2, 0)]] RWTexture3D<half4> destinationVolume : register(u2, space0);

float4 over(float4 front, float4 back)
{
    return float4(front.rgb + (1.0 - front.a) * back.rgb,
                  front.a + (1.0 - front.a) * back.a);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void computeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int3 destination = int3(dispatchThreadId);
    const int3 localSize = int3(pass.cascadeMipSize);
    if (any(destination >= localSize)) return;
    const int sourceCascadeSize = int(pass.cascadeMipSize * 2);
    const int3 base = destination * 2 + int3(0, 0, int(pass.cascadeIndex) * sourceCascadeSize);
    float4 total = 0.0;
    for (int a = 0; a < 2; ++a)
    for (int b = 0; b < 2; ++b)
    {
        const int nearCoordinate = pass.sign > 0 ? 0 : 1;
        const int farCoordinate = 1 - nearCoordinate;
        int3 nearOffset = pass.axis == 0 ? int3(nearCoordinate, a, b)
                        : pass.axis == 1 ? int3(a, nearCoordinate, b)
                                         : int3(a, b, nearCoordinate);
        int3 farOffset = pass.axis == 0 ? int3(farCoordinate, a, b)
                       : pass.axis == 1 ? int3(a, farCoordinate, b)
                                        : int3(a, b, farCoordinate);
        uint sourceWidth, sourceHeight, sourceDepth, mipCount;
        sourceVolume.GetDimensions(pass.sourceMip, sourceWidth, sourceHeight, sourceDepth, mipCount);
        const float3 sourceSize = float3(sourceWidth, sourceHeight, sourceDepth);
        const float4 front = sourceVolume.SampleLevel((float3(base + nearOffset) + 0.5) / sourceSize,
                                                       pass.sourceMip);
        const float4 back = sourceVolume.SampleLevel((float3(base + farOffset) + 0.5) / sourceSize,
                                                      pass.sourceMip);
        total += over(front, back);
    }
    destinationVolume[destination + int3(0, 0, int(pass.cascadeIndex * pass.cascadeMipSize))] =
        half4(total * 0.25);
}
