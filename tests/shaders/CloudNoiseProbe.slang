#include "../../engine/render/shaders/VolumetricCloud.slang"

[shader("fragment")]
float4 noiseProbeMain(FullscreenVertexOutput input) : SV_Target
{
    // Cross lattice planes along all three axes, at positive and negative
    // coordinates. Discontinuous corners appear as sheets in the cloud volume.
    const float3 cell = float3(floor(input.position.xy) - 64.0, 12000.0);
    const float3 x = cell + float3(0, .37, .63);
    const float3 y = cell + float3(.37, 0, .63);
    const float3 z = cell + float3(.37, .63, 0);
    const float4x4 inverseVolume = float4x4(1e-8,0,0,0, 0,.01,0,0, 0,0,1e-8,0, 0,0,0,1);
    const float2 grazingHit = intersectVolume(float3(0,100,0), float3(0,-1e-5,-1), inverseVolume);
    const float slabError = max(abs(grazingHit.x / 5000000.0 - 1.0),
                               abs(grazingHit.y / 15000000.0 - 1.0));
    return float4(max(slabError, abs(valueNoise(x - float3(.001,0,0)) - valueNoise(x + float3(.001,0,0)))),
                  abs(valueNoise(y - float3(0,.001,0)) - valueNoise(y + float3(0,.001,0))),
                  abs(valueNoise(z - float3(0,0,.001)) - valueNoise(z + float3(0,0,.001))), 1);
}
