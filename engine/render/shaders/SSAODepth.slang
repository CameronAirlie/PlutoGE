// View-depth preparation and a depth-aware mip hierarchy shared by AO estimators.
#include "PostProcessParameters.slang"
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sourceDepth : register(t1, space0);
[[vk::binding(2, 0)]] RWTexture2D<float> outputDepth : register(u2, space0);

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    outputDepth.GetDimensions(width, height);
    if (any(id.xy >= uint2(width, height))) return;
    if (pass.parameters[3].w < 0.5)
    {
        const float deviceDepth = sourceDepth.Load(int3(id.xy, 0)).r;
        const float z = pass.zeroToOneDepth != 0 ? deviceDepth : deviceDepth * 2.0 - 1.0;
        const float viewZ = (pass.projection[2][3] - z * pass.projection[3][3]) /
                           (z * pass.projection[3][2] - pass.projection[2][2]);
        outputDepth[id.xy] = deviceDepth > 0.000001 ? max(-viewZ, 0.0) : 0.0;
        return;
    }
    const int level = int(pass.parameters[3].x);
    uint sourceWidth, sourceHeight, levels;
    sourceDepth.GetDimensions(level, sourceWidth, sourceHeight, levels);
    const int2 limit = int2(sourceWidth, sourceHeight) - 1;
    const int2 base = int2(id.xy) * 2;
    const float4 depths = float4(sourceDepth.Load(int3(min(base,limit),level)).r,
        sourceDepth.Load(int3(min(base+int2(1,0),limit),level)).r,
        sourceDepth.Load(int3(min(base+int2(0,1),limit),level)).r,
        sourceDepth.Load(int3(min(base+int2(1,1),limit),level)).r);
    const float farthest = max(max(depths.x,depths.y),max(depths.z,depths.w));
    // Prefer the far surface at discontinuities: thin foreground silhouettes
    // must not expand into broad occluders at coarse levels. Never average sky.
    float4 weights = saturate(1.0 - abs(depths-farthest) / max(pass.parameters[0].x,0.05));
    weights *= weights * float4(depths > 0.0);
    outputDepth[id.xy] = dot(depths,weights) / max(dot(weights,float4(1.0)),0.000001);
}
