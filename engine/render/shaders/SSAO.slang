// Raw ambient occlusion at the surface guide's resolution (see ScreenSpaceAmbientOcclusion.h). Writes visibility
// and packed depth/normal edges for the edge-aware denoiser; the guide supplies receiver depth and normals, so the
// G-buffer is not read here.
#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "SSAOCommon.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D guideTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D preparedDepth : register(t2, space0);

// pass.inverseResolution is the full-resolution texel size; parameters[2].zw the guide's, parameters[3].z the
// hierarchy level count.
int2 guideSize() { return int2(round(1.0 / pass.parameters[2].zw)); }
int2 fullSize() { return int2(round(1.0 / pass.inverseResolution)); }

float3 guidePosition(int2 texel, out float depth)
{
    texel = clamp(texel, int2(0), guideSize() - 1);
    depth = guideTexture.Load(int3(texel, 0)).x;
    return viewPositionFromLinearDepth(guideRepresentativeUv(texel, guideSize(), fullSize()), depth, pass);
}

// A sample `offsetPixels` (full-resolution pixels) from the receiver. Distant samples read coarser hierarchy levels
// for cache locality; every position is reconstructed where its depth came from (the representative pixel, or the
// level texel centre), since reconstructing at the requested UV introduces false height on slopes.
float3 ssaoPosition(float2 uv, float2 offsetPixels, out float depth)
{
    const int level = int(floor(clamp(log2(max(length(offsetPixels), 1.0)) - 3.15, 0.0, pass.parameters[3].z) + 0.5));
    if (level == 0)
        return guidePosition(int2(uv * float2(guideSize())), depth);
    // Hierarchy level L - 1 holds full-resolution level L: texel i reduces pixels [i, i + 1) * 2^L, so reconstruct
    // at their centre (a texel-centre UV is up to a pixel away on odd extents).
    uint baseWidth, baseHeight, levels;
    preparedDepth.GetDimensions(0, baseWidth, baseHeight, levels);
    const int mip = level - 1;
    const int2 size = max(int2(1), int2(baseWidth, baseHeight) >> mip);
    const float footprint = float(1 << level);
    const int2 coordinate = clamp(int2(uv * float2(fullSize()) / footprint), int2(0), size - 1);
    depth = preparedDepth.Load(int3(coordinate, mip)).r;
    return viewPositionFromLinearDepth((float2(coordinate) + 0.5) * footprint * pass.inverseResolution, depth, pass);
}

#ifdef PLUTO_SSAO_HORIZON
#include "SSAOHorizonSampling.slang"
#endif

// XeGTAO's slope-adjusted depth edges, attenuated across creases by normal agreement.
float4 receiverEdges(float center, float4 depthsLRTB, float3 normal, float3x4 neighbourNormals)
{
    float4 deltas = depthsLRTB - center;
    const float slopeLR = (deltas.y - deltas.x) * 0.5;
    const float slopeTB = (deltas.w - deltas.z) * 0.5;
    const float4 adjusted = deltas + float4(slopeLR, -slopeLR, slopeTB, -slopeTB);
    deltas = min(abs(deltas), abs(adjusted));
    float4 edges = saturate(1.25 - deltas / (center * 0.011)) * float4(depthsLRTB > 0.0);
    [ForceUnroll] for (int side = 0; side < 4; ++side)
    {
        float agreement = saturate(dot(normal, float3(neighbourNormals[0][side], neighbourNormals[1][side],
                                                      neighbourNormals[2][side])));
        agreement *= agreement;
        agreement *= agreement;
        edges[side] *= agreement * agreement;
    }
    return edges;
}

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    const float2 uv = guideRepresentativeUv(pixel, guideSize(), fullSize());
    const GuideSurface receiver = decodeGuide(guideTexture.Load(int3(pixel, 0)));
    if (receiver.depth <= 0.0) return float4(1.0, 0.0, 0.0, 0.0);
    const float centerLinearDepth = receiver.depth;
    const float3 centerPosition = viewPositionFromLinearDepth(uv, centerLinearDepth, pass);
    const float3 viewShadingNormal = normalize(mul((float3x3)pass.view, receiver.normal));
    // Reconstruct the receiver plane from the continuous side of each axis.
    // Quad derivatives cross silhouettes and can invent occluders on a flat
    // isolated surface; normal-map detail must not define this geometric plane.
    float leftDepth, rightDepth, topDepth, bottomDepth;
    const float3 left = guidePosition(pixel - int2(1, 0), leftDepth);
    const float3 right = guidePosition(pixel + int2(1, 0), rightDepth);
    const float3 top = guidePosition(pixel - int2(0, 1), topDepth);
    const float3 bottom = guidePosition(pixel + int2(0, 1), bottomDepth);
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

    float3x4 neighbourNormals;
    [ForceUnroll] for (int side = 0; side < 4; ++side)
    {
        const int2 offset = side == 0 ? int2(-1, 0) : side == 1 ? int2(1, 0) : side == 2 ? int2(0, -1) : int2(0, 1);
        const int2 texel = clamp(pixel + offset, int2(0), guideSize() - 1);
        const float3 neighbour = decodeOctahedral(guideTexture.Load(int3(texel, 0)).yz);
        neighbourNormals[0][side] = neighbour.x;
        neighbourNormals[1][side] = neighbour.y;
        neighbourNormals[2][side] = neighbour.z;
    }
    const float4 edges = receiverEdges(centerLinearDepth, float4(leftDepth, rightDepth, topDepth, bottomDepth),
                                       receiver.normal, neighbourNormals);

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
    return float4(ao, packEdges(edges), 0.0, 0.0);
}
