#define PLUTO_GLASS 1
#include "BasicLit.slang"

// Boundary uses the actual raster depth, rather than a mesh/particle center.
// Keeping the previous boundary outside this surface partitions every pixel
// into disjoint depth intervals without duplicating smoke opacity.
[shader("fragment")]
float particleBoundaryMain(VertexOutput input) : SV_Target0
{
    if (baseColor.a * baseColorTexture.Sample(input.uv * uvScale).a <= 0.001) discard;
    uint width, height;
    glassSceneColor.GetDimensions(width, height);
    const float previous = glassSceneColor.SampleLevel(input.position.xy / float2(width, height), 0).r;
    return max(previous, input.position.z);
}
