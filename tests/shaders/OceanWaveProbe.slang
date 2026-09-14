// Compile the production wave evaluator and packet layout for CPU/GPU parity checks.
#include "../../engine/render/shaders/Ocean.slang"
[shader("fragment")]
float4 probeMain(FullscreenVertexOutput input) : SV_Target
{
    OceanWaveSample wave = sampleWaves(float2(2.3, -4.1));
    return float4(.5 + .2*wave.height, .5 + .2*wave.gradient.x, .5 + .2*wave.gradient.y, 1);
}
