// Shared encoding of the screen-space surface guide (ScreenSpaceGuide.slang) and helpers for its consumers.
// A guide texel describes one representative G-buffer pixel: linear view depth (zero where nothing was drawn),
// the world-space normal (octahedral) and perceptual roughness. Reduced-resolution guides pick the same pixel for
// a texel that SSR's trace and SSAO's reconstruction use, so every stage agrees on which surface it describes.
#ifndef PLUTO_SCREEN_SPACE_GUIDE_COMMON
#define PLUTO_SCREEN_SPACE_GUIDE_COMMON

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

struct GuideSurface
{
    float depth;     // linear view depth; zero for background
    float3 normal;   // world space
    float roughness;
};

float4 encodeGuide(float depth, float3 normal, float roughness)
{
    return depth > 0.0 ? float4(depth, encodeOctahedral(normal), roughness) : float4(0.0, 0.5, 0.5, 1.0);
}

GuideSurface decodeGuide(float4 texel)
{
    GuideSurface surface;
    surface.depth = texel.x;
    surface.normal = decodeOctahedral(texel.yz);
    surface.roughness = texel.w;
    return surface;
}

// The full-resolution pixel a guide texel describes: the one under the texel centre, floor((2i + 1) W / 2G). Guides
// are full resolution or ceil(W / 2), for which that is i, 2i + 1 (even W) or 2i (odd W). Exact on every backend and
// free of integer division, which GCN emulates with a long sequence (and which ran per tap).
int2 guideRepresentativePixel(int2 guidePixel, int2 guideSize, int2 fullSize)
{
    const int2 halfResolution = guidePixel * 2 + int2(guideSize * 2 == fullSize);
    return select(guideSize == fullSize, guidePixel, halfResolution);
}

float2 guideRepresentativeUv(int2 guidePixel, int2 guideSize, int2 fullSize)
{
    return (float2(guideRepresentativePixel(guidePixel, guideSize, fullSize)) + 0.5) / float2(fullSize);
}

// Positive linear view depth from device depth (either clip-depth convention). Zero for background.
float linearViewDepth(float deviceDepth, float4x4 projection, uint zeroToOneDepth)
{
    const float z = zeroToOneDepth != 0 ? deviceDepth : deviceDepth * 2.0 - 1.0;
    const float viewZ = (projection[2][3] - z * projection[3][3]) / (z * projection[3][2] - projection[2][2]);
    return deviceDepth > 0.000001 ? max(-viewZ, 0.0) : 0.0;
}

// View-space position of the surface at texture UV with positive linear depth (right-handed, -z forward).
float3 viewPositionFromLinearDepth(float2 textureUv, float depth, PostProcessParameterBlock pass)
{
    float2 ndc = textureUv * 2.0 - 1.0;
    if (pass.flipY != 0) ndc.y = -ndc.y;
    const float z = -depth;
    const float w = pass.projection[3][2] * z + pass.projection[3][3];
    return float3((ndc.x * w - pass.projection[0][2] * z - pass.projection[0][3]) / pass.projection[0][0],
                  (ndc.y * w - pass.projection[1][2] * z - pass.projection[1][3]) / pass.projection[1][1], z);
}

#endif
