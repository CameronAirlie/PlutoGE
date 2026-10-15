#define PLUTO_GLASS 1
#include "BasicLit.slang"

// Boundary uses the actual raster depth, rather than a mesh/particle center.
// Keeping the previous boundary outside this surface partitions every pixel
// into disjoint depth intervals without duplicating smoke opacity.
[shader("fragment")]
float particleBoundaryMain(VertexOutput input) : SV_Target0
{
    const SurfaceDerivatives derivatives = surfaceDerivatives(input);
    if (baseColor.a * baseColorTexture.SampleGrad(input.uv * uvScale, derivatives.uvDx, derivatives.uvDy).a <= 0.001) discard;
    uint width, height;
    glassSceneColor.GetDimensions(width, height);
    const float previous = glassSceneColor.SampleLevel(input.position.xy / float2(width, height), 0).r;
    return max(previous, input.position.z);
}
