// Full-resolution bilateral upsample of resolved AO history, composited onto indirect lighting.
// parameters[1].y: AO-only diagnostic; parameters[2].zw: history (guide) texel size.
#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "SSAOCommon.slang"
[[vk::binding(0,0)]] cbuffer Parameters : register(b0,space0) { PostProcessParameterBlock pass; };
[[vk::binding(1,0)]] Sampler2D sceneTexture : register(t1,space0);
[[vk::binding(2,0)]] Sampler2D depthTexture : register(t2,space0);
[[vk::binding(3,0)]] Sampler2D normalTexture : register(t3,space0);
[[vk::binding(6,0)]] Sampler2D historyTexture : register(t6,space0);
[[vk::binding(8,0)]] Sampler2D indirectTexture : register(t8,space0);
[[vk::binding(9,0)]] Sampler2D geometrySceneTexture : register(t9,space0);
[shader("vertex")] FullscreenVertexOutput vertexMain(uint id:SV_VertexID) { return fullscreenVertex(id); }
[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input):SV_Target
{
    const int2 pixel=int2(input.position.xy);
    const int2 fullSize=int2(round(1.0/pass.inverseResolution));
    const float2 uv=(float2(pixel)+0.5)*pass.inverseResolution;
    const float linearDepth=linearViewDepth(depthTexture.Load(int3(pixel,0)).r,pass.projection,pass.zeroToOneDepth);
    float ao=1.0;
    if (linearDepth>0.0)
    {
        const float3 position=viewPositionFromLinearDepth(uv,linearDepth,pass);
        const float3 normal=decodeNormal(normalTexture.Load(int3(pixel,0)).xyz);
        const float3 viewNormal=mul((float3x3)pass.view,normal);
        const int2 size=int2(round(1.0/pass.parameters[2].zw));
        const float2 coordinate=uv*float2(size)-0.5;
        const int2 base=int2(floor(coordinate));
        const float2 fraction=frac(coordinate);
        float sum=0.0,weights=0.0;
        [ForceUnroll] for (int y=0;y<2;++y) [ForceUnroll] for (int x=0;x<2;++x)
        {
            const int2 cell=clamp(base+int2(x,y),int2(0),size-1);
            const float4 history=historyTexture.Load(int3(cell,0));
            if (history.g<=0.0) continue;
            // History describes the full-resolution receiver its guide texel represents,
            // not a bilinear blend of disjoint planes.
            const float2 sampleUv=guideRepresentativeUv(cell,size,fullSize);
            const float3 samplePosition=viewPositionFromLinearDepth(sampleUv,history.g,pass);
            const float delta=abs(dot(viewNormal,samplePosition-position));
            if (delta>0.35) continue;
            float n=max(dot(normal,decodeOctahedral(history.ba)),0.0);
            n*=n; n*=n; n*=n; n*=n;
            const float2 w=lerp(1.0-fraction,fraction,float2(x,y));
            const float weight=w.x*w.y*n/(1.0+delta*24.0);
            sum+=history.r*weight; weights+=weight;
        }
        if (weights>0.0001) ao=sum/weights;
    }
    // Colour inputs share the output extent (ScreenSpaceAmbientOcclusion::Inputs). Loads skip the half-rate
    // filtering GCN applies to 64-bit formats.
    const bool diagnostic=pass.parameters[1].y>0.5;
    const float4 scene=sceneTexture.Load(int3(pixel,0));
    // Unoccluded pixels (usually most of the screen) keep the scene, without reading the indirect inputs.
    if (ao>=1.0 && !diagnostic) return scene;
    const float3 indirect=indirectTexture.Load(int3(pixel,0)).rgb +
        max(scene.rgb-geometrySceneTexture.Load(int3(pixel,0)).rgb,0.0);
    return ssaoComposite(scene,indirect,ao,diagnostic);
}
