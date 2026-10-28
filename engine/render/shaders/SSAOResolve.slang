// Edge-aware AO denoising and temporal accumulation at the raw AO resolution (see ScreenSpaceAmbientOcclusion.h).
//
// denoiseMain: one XeGTAO-style 3 x 3 pass driven by the edges the raw pass packed beside visibility; it reads only
// the raw target. resolveMain: an optional last denoise pass, then reprojected history, written as the new history
// (visibility, linear depth, octahedral normal) for the full-resolution upsample and the next frame.
//
// parameters[1]: y = AO-only diagnostic, z = history weight, w = final pass denoises (> 0.5).
// parameters[2]: x = relative depth threshold, y = normal threshold, zw = guide texel size.
// parameters[5].w: history valid. worldToLocal: current view space to the previous frame's view space.
#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "SSAOCommon.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D aoTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D guideTexture : register(t2, space0);
[[vk::binding(5, 0)]] Sampler2D motionTexture : register(t5, space0);
[[vk::binding(6, 0)]] Sampler2D historyTexture : register(t6, space0);

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

int2 guideSize() { return int2(round(1.0 / pass.parameters[2].zw)); }

float2 aoAt(int2 pixel) { return aoTexture.Load(int3(clamp(pixel, int2(0), guideSize() - 1), 0)).rg; }

struct Denoised
{
    float ao, minimum, maximum;
};

// Neighbours contribute through edges both sides agree on; diagonals through either orthogonal neighbour.
Denoised denoise(int2 pixel)
{
    const float2 center = aoAt(pixel);
    const float2 left = aoAt(pixel + int2(-1, 0)), right = aoAt(pixel + int2(1, 0));
    const float2 top = aoAt(pixel + int2(0, -1)), bottom = aoAt(pixel + int2(0, 1));
    const float4 edges = unpackEdges(center.y);
    const float4 leftEdges = unpackEdges(left.y), rightEdges = unpackEdges(right.y);
    const float4 topEdges = unpackEdges(top.y), bottomEdges = unpackEdges(bottom.y);
    const float4 orthogonal = edges * float4(leftEdges.y, rightEdges.x, topEdges.w, bottomEdges.z);
    const float diagonalWeight = 0.425;
    const float4 diagonal = diagonalWeight * float4(
        orthogonal.x * leftEdges.z + orthogonal.z * topEdges.x,     // top-left
        orthogonal.y * rightEdges.z + orthogonal.z * topEdges.y,    // top-right
        orthogonal.x * leftEdges.w + orthogonal.w * bottomEdges.x,  // bottom-left
        orthogonal.y * rightEdges.w + orthogonal.w * bottomEdges.y); // bottom-right
    const float4 orthogonalAo = float4(left.x, right.x, top.x, bottom.x);
    const float4 diagonalAo = float4(aoAt(pixel + int2(-1, -1)).x, aoAt(pixel + int2(1, -1)).x,
                                     aoAt(pixel + int2(-1, 1)).x, aoAt(pixel + int2(1, 1)).x);
    const float centerWeight = 1.0;
    Denoised result;
    result.ao = (center.x * centerWeight + dot(orthogonalAo, orthogonal) + dot(diagonalAo, diagonal)) /
                (centerWeight + dot(orthogonal, 1.0) + dot(diagonal, 1.0));
    // The temporal clamp range spans the connected neighbourhood only.
    result.minimum = result.maximum = center.x;
    [ForceUnroll] for (int index = 0; index < 4; ++index)
    {
        if (orthogonal[index] > 0.0)
        {
            result.minimum = min(result.minimum, orthogonalAo[index]);
            result.maximum = max(result.maximum, orthogonalAo[index]);
        }
        if (diagonal[index] > 0.0)
        {
            result.minimum = min(result.minimum, diagonalAo[index]);
            result.maximum = max(result.maximum, diagonalAo[index]);
        }
    }
    return result;
}

[shader("fragment")]
float4 denoiseMain(FullscreenVertexOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    return float4(denoise(pixel).ao, aoAt(pixel).y, 0.0, 0.0);
}

[shader("fragment")]
float4 resolveMain(FullscreenVertexOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    const int2 fullSize = int2(round(1.0 / pass.inverseResolution));
    const GuideSurface receiver = decodeGuide(guideTexture.Load(int3(pixel, 0)));
    if (receiver.depth <= 0.0)
        return float4(1.0, 0.0, encodeOctahedral(float3(0, 0, 1)));
    Denoised current = denoise(pixel);
    if (pass.parameters[1].w < 0.5)
        current.ao = aoAt(pixel).x;
    float resolvedAo = current.ao;
    if (pass.parameters[5].w > 0.5)
    {
        const int2 surfacePixel = guideRepresentativePixel(pixel, guideSize(), fullSize);
        const float2 orientation = float2(1.0, pass.flipY != 0 ? -1.0 : 1.0);
        const float2 motion = motionTexture.Load(int3(surfacePixel, 0)).xy * orientation;
        const float2 historyUv = (float2(pixel) + 0.5) * pass.parameters[2].zw - motion;
        if (all(historyUv >= 0.0) && all(historyUv <= 1.0))
        {
            const float4 history = historyTexture.SampleLevel(historyUv, 0);
            const float3 position = viewPositionFromLinearDepth((float2(surfacePixel) + 0.5) * pass.inverseResolution,
                                                                receiver.depth, pass);
            const float previousViewDepth = -mul(pass.worldToLocal, float4(position, 1.0)).z;
            const float relativeDepthDelta = abs(history.g - previousViewDepth) / max(previousViewDepth, 0.001);
            const float normalSimilarity = dot(decodeOctahedral(history.ba), receiver.normal);
            if (relativeDepthDelta <= pass.parameters[2].x && normalSimilarity >= pass.parameters[2].y)
                resolvedAo = lerp(current.ao, clamp(history.r, current.minimum, current.maximum), pass.parameters[1].z);
        }
    }
    return float4(resolvedAo, receiver.depth, encodeOctahedral(receiver.normal));
}
