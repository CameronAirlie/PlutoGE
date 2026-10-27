#include "Fullscreen.slang"
#include "../include/PlutoGE/render/PhysicalSkyLayout.h"
[[vk::binding(0, 0)]] cbuffer SkyParameters : register(b0, space0)
{
    float4 physicalSkyParameters[PLUTO_SKY_LANE_COUNT];
    float4 physicalSkySettings;
};
static const float PI = 3.14159265359;
#include "PhysicalSkyEnvironment.slang"
[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }
[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target0
{
    const uint2 pixel = uint2(input.position.xy);
    if (pixel.y == PLUTO_SKY_DIFFUSE_ROW) {
        if (pixel.x >= 14) return 0.0;
        return float4(samplePhysicalSkyEnvironment(skyQuadratureDirections[pixel.x], 0.0), 1.0);
    }
    for (uint level = 0; level <= PLUTO_SKY_MAX_LEVEL; ++level) {
        const int size = PLUTO_SKY_LEVEL_SIZE(level);
        const uint start = PLUTO_SKY_LEVEL_X(level);
        const uint tile = PLUTO_SKY_TILE_SIZE(level);
        if (pixel.x < start || pixel.x >= start + tile) continue;
        int face = int(pixel.y / tile);
        if (face >= 6) return 0.0;
        int2 texel = int2(pixel.x - start, pixel.y % tile) - PLUTO_SKY_GUTTER;
        if (any(texel < 0) || any(texel >= size)) {
            // A gutter texel stores the texel a bilinear tap past this edge
            // belongs to: the neighbouring face's, not a clamped edge texel.
            float2 uv;
            environmentCubeCoordinate(environmentCubeDirection(face, (float2(texel) + 0.5) / float(size)), face, uv);
            texel = clamp(int2(floor(uv * float(size))), int2(0), int2(size - 1));
        }
        const float3 direction = environmentCubeDirection(face, (float2(texel) + 0.5) / float(size));
        return float4(convolvePhysicalSkySpecular(direction, float(level) / PLUTO_SKY_MAX_LEVEL), 1.0);
    }
    return 0.0;
}
