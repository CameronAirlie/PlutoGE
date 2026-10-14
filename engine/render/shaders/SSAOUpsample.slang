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
[shader("vertex")] FullscreenVertexOutput vertexMain(uint id:SV_VertexID) { return fullscreenVertex(id); }
[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input):SV_Target
{
    const float2 uv=orientFullscreenUv(input.uv,pass.flipY);
    const float depth=depthTexture.SampleLevel(uv,0).r;
    float ao=1.0;
    if (depth>0.000001)
    {
        const float3 world=reconstructWorldPosition(uv,depth,pass.inverseViewProjection,pass.flipY,pass.zeroToOneDepth);
        const float3 position=mul(pass.view,float4(world,1)).xyz;
        const float3 normal=decodeNormal(normalTexture.SampleLevel(uv,0).xyz);
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
            // History describes the full-resolution receiver selected by the
            // reduced-resolution filter, not a bilinear blend of disjoint planes.
            float2 sampleUv=(float2(cell)+0.5)/float2(size);
            sampleUv=(floor(sampleUv/pass.inverseResolution)+0.5)*pass.inverseResolution;
            const float3 samplePosition=ssaoViewPosition(sampleUv,history.g,pass);
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
    const float4 scene=sceneTexture.SampleLevel(uv,0);
    const float3 indirect=indirectTexture.SampleLevel(uv,0).rgb;
    return ssaoComposite(scene,indirect,ao,pass.parameters[1].y>0.5);
}
