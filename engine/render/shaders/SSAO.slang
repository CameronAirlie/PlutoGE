#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "SSAOCommon.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[[vk::binding(4, 0)]] Sampler2D preparedDepth : register(t4, space0);

// Reconstruct at the texel center that supplied depth, including coarse levels.
// Reconstructing at the requested UV instead introduces false height on slopes.
float3 ssaoPosition(float2 uv, float2 offsetPixels, out float depth)
{
    if (pass.parameters[3].w > 0.5)
    {
        const int level = int(floor(clamp(log2(max(length(offsetPixels),1.0))-3.15,0.0,pass.parameters[3].z)+0.5));
        const uint2 size = max(uint2(1),uint2(round(1.0/pass.inverseResolution)) >> uint(level));
        const uint width=size.x,height=size.y;
        const int2 coordinate = clamp(int2(uv*float2(width,height)),int2(0),int2(width,height)-1);
        const float2 sampleUv = (float2(coordinate)+0.5)/float2(width,height);
        depth = preparedDepth.Load(int3(coordinate,level)).r;
        return ssaoViewPosition(sampleUv,depth,pass);
    }
    const float deviceDepth = depthTexture.Sample(uv).r;
    const float3 world = reconstructWorldPosition(uv,deviceDepth,pass.inverseViewProjection,pass.flipY,pass.zeroToOneDepth);
    const float3 position = mul(pass.view,float4(world,1)).xyz;
    depth = deviceDepth > 0.000001 ? -position.z : 0.0;
    return position;
}

#ifdef PLUTO_SSAO_HORIZON
#include "SSAOHorizonSampling.slang"
#endif

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    float centerLinearDepth;
    const float3 centerPosition = ssaoPosition(uv,float2(0),centerLinearDepth);
    if (centerLinearDepth <= 0.0) return float4(1.0,0.0,0.5,0.5);
    const float3 shadingNormal = decodeNormal(normalTexture.Load(int3(int2(uv / pass.inverseResolution),0)).xyz);
    const float3 viewShadingNormal = normalize(mul((float3x3)pass.view,shadingNormal));
    // Reconstruct the receiver plane from the continuous side of each axis.
    // Quad derivatives cross silhouettes and can invent occluders on a flat
    // isolated surface; normal-map detail must not define this geometric plane.
    const float2 step = pass.parameters[2].zw;
    float leftDepth,rightDepth,topDepth,bottomDepth;
    const float3 left = ssaoPosition(uv-float2(step.x,0),float2(0),leftDepth);
    const float3 right = ssaoPosition(uv+float2(step.x,0),float2(0),rightDepth);
    const float3 top = ssaoPosition(uv-float2(0,step.y),float2(0),topDepth);
    const float3 bottom = ssaoPosition(uv+float2(0,step.y),float2(0),bottomDepth);
    const bool useLeft = leftDepth>0 && (rightDepth<=0 || abs(leftDepth-centerLinearDepth)<abs(rightDepth-centerLinearDepth));
    const bool useTop = topDepth>0 && (bottomDepth<=0 || abs(topDepth-centerLinearDepth)<abs(bottomDepth-centerLinearDepth));
    const float3 dx = useLeft ? centerPosition-left : right-centerPosition;
    const float3 dy = useTop ? centerPosition-top : bottom-centerPosition;
    float3 planeNormal = cross(dx,dy);
    const float planeLength = length(planeNormal);
    planeNormal /= max(planeLength,0.000001);
    if (dot(planeNormal,viewShadingNormal)<0) planeNormal=-planeNormal;
    const bool validPlane = max(leftDepth,rightDepth)>0 && max(topDepth,bottomDepth)>0 && planeLength>0.000001;
    const float3 normal = validPlane && dot(planeNormal,viewShadingNormal)>0.5 ? planeNormal : viewShadingNormal;
    const float temporalRotation = pass.parameters[1].z > 0.0
        ? frac(round(pass.time * 60.0) * 0.618033989) : 0.0;
    const float noiseAngle = frac(temporalRotation + 52.9829189 * frac(dot(floor(input.position.xy),
                                                       float2(0.06711056, 0.00583715)))) * 6.2831853;
    const float radius = max(pass.parameters[0].x, 0.05);
    const float bias = max(pass.parameters[0].y, 0.0);
    const uint samples = clamp(pass.quality, 4u, 32u);
    // Project the world-space radius at the receiver depth. A minimum footprint
    // keeps distant AO stable, while the upper bound prevents a near-plane
    // receiver from scanning an excessive portion of the screen.
    const float2 projectedRadius = clamp(
        radius * abs(float2(pass.projection[0][0], pass.projection[1][1])) /
            max(centerLinearDepth * 2.0, 0.001),
        pass.inverseResolution * 2.0, float2(0.15, 0.15));
#ifdef PLUTO_SSAO_HORIZON
    float ao = 1.0 - (1.0 - ssaoHorizonVisibility(uv,centerPosition,normal,projectedRadius,
        radius,bias,noiseAngle,samples))*pass.parameters[0].z;
#else
    float occlusion = 0.0;
    float sampleWeight = 0.0;
    float2 sampleDirection = float2(cos(noiseAngle), sin(noiseAngle));
    for (uint i = 0; i < samples; ++i)
    {
        const float fraction = (float(i) + 0.5) / float(samples);
        const float2 direction = sampleDirection;
        // Rotate the spiral iteratively; avoid sine/cosine for every depth tap.
        sampleDirection = float2(
            direction.x * -0.737368878 - direction.y * 0.675490294,
            direction.x * 0.675490294 + direction.y * -0.737368878);
        const float radial = sqrt(fraction);
        const float2 sampleUv = uv + direction *
                                     projectedRadius * radial;
        if (any(sampleUv < 0.0) || any(sampleUv > 1.0)) continue;
        float sampleDepth;
        const float3 samplePosition = ssaoPosition(sampleUv,(sampleUv-uv)/pass.inverseResolution,sampleDepth);
        if (sampleDepth <= 0.0) continue;
        const float3 separation = samplePosition - centerPosition;
        const float distanceToSample = length(separation);
        const float rangeWeight = 1.0 - smoothstep(radius * 0.5, radius,
                                                   distanceToSample);
        if (rangeWeight <= 0.0) continue;
        // Camera-relative depth differences make a flat receding plane appear
        // occluded. Test against the receiver's tangent plane instead: only a
        // sample lying above the surface can block its ambient hemisphere.
        const float heightAboveSurface = dot(separation, normal);
        const float sampleOccluded = saturate(
            (heightAboveSurface - bias) / max(distanceToSample, 0.001));
        occlusion += sampleOccluded * rangeWeight;
        sampleWeight += rangeWeight;
    }
    float ao = 1.0 - occlusion / max(sampleWeight, 1.0) * pass.parameters[0].z;
#endif
    ao = pow(saturate(ao), max(pass.parameters[0].w, 0.25));
    return float4(ao, centerLinearDepth, encodeOctahedral(shadingNormal));
}
