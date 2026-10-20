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
        const uint size = PLUTO_SKY_FACE_SIZE >> level;
        const uint start = 2 * PLUTO_SKY_FACE_SIZE - 2 * size;
        if (pixel.x < start || pixel.x >= start + size) continue;
        const uint face = pixel.y / size;
        if (face >= 6) return 0.0;
        const float2 uv = (float2(pixel.x - start, pixel.y % size) + 0.5) / float(size);
        const float3 direction = environmentCubeDirection(int(face), uv);
        return float4(convolvePhysicalSkySpecular(direction, float(level) / PLUTO_SKY_MAX_LEVEL), 1.0);
    }
    return 0.0;
}
