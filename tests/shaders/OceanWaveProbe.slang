// Compile the production wave evaluator and packet layout for CPU/GPU parity checks.
#include "../../engine/render/shaders/Ocean.slang"
float4 shaderGraphTexture(int index, float2 uv) { return 0; }
float2 shaderGraphScreenUV(float3 worldPosition) { return 0; }
#include "../../engine/render/shaders/ShaderGraphEvaluation.slang"
[shader("fragment")]
float4 probeMain(FullscreenVertexOutput input) : SV_Target
{
    OceanWaveSample wave = sampleWaves(float2(2.3, -4.1));
    return float4(.5 + .2*wave.height, .5 + .2*wave.gradient.x, .5 + .2*wave.gradient.y, 1);
}

[shader("fragment")]
float4 noiseProbeMain(FullscreenVertexOutput input) : SV_Target
{
    float2 cell = floor(input.position.xy) - 64.0;
    float2 p = cell + float2(0, .37);
    float x = abs(waterNoise(p - float2(.0001, 0)) - waterNoise(p + float2(.0001, 0)));
    x = max(x, abs(shaderGraphNoise(p - float2(.0001, 0)) - shaderGraphNoise(p + float2(.0001, 0))));
    p = cell + float2(.37, 0);
    float y = abs(waterNoise(p - float2(0, .0001)) - waterNoise(p + float2(0, .0001)));
    y = max(y, abs(shaderGraphNoise(p - float2(0, .0001)) - shaderGraphNoise(p + float2(0, .0001))));
    return float4(x, y, abs(waterNoise(p) - shaderGraphNoise(p)), 1);
}
