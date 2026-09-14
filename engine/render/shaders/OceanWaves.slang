// Mirrors scene::SampleOceanSurface. Spectrum construction and phase wrapping happen on the CPU.
struct OceanWaveSample
{
    float height;
    float2 gradient;
    float velocity;
    float crest;
};
OceanWaveSample sampleWaves(float2 position)
{
    OceanWaveSample result = {0.0, float2(0.0), 0.0, 0.0};
    for (int i = 0; i < 8; ++i)
    {
        float4 shape = waveShape[i], motion = waveMotion[i];
        float phase = dot(position, shape.xy) * shape.z + motion.x;
        float sine, cosine;
        sincos(phase, sine, cosine);
        float cosine2 = cosine*cosine - sine*sine;
        float derivative = cosine + 4.0*motion.z*sine*cosine;
        result.height += shape.w * (sine - motion.z*cosine2);
        result.gradient += shape.xy * (shape.w*shape.z*derivative);
        result.velocity -= shape.w*motion.y*derivative;
        result.crest += shape.w*shape.z*(sine-4.0*motion.z*cosine2);
    }
    result.crest = max(result.crest, 0.0);
    return result;
}
float waterNoise(float2 p)
{
    float2 cell = floor(p), f = frac(p);
    f = f*f*(3.0-2.0*f);
    float4 corners = float4(dot(cell,float2(127.1,311.7)), dot(cell+float2(1,0),float2(127.1,311.7)),
                           dot(cell+float2(0,1),float2(127.1,311.7)), dot(cell+1.0,float2(127.1,311.7)));
    float4 values = frac(sin(corners)*43758.5453);
    return lerp(lerp(values.x,values.y,f.x),lerp(values.z,values.w,f.x),f.y);
}
float waterFoamPattern(float2 p, float time)
{
    float2 drift = flow.xy * time * waves.z * .3;
    float broad = waterNoise(p * crestFoam.z - drift);
    float fine = waterNoise(p * crestFoam.z * 3.1 - drift*.7);
    float pattern = smoothstep(.25,.8,broad*.65+fine*.35);
    float footprint = max(length(ddx(p*crestFoam.z)),length(ddy(p*crestFoam.z)));
    // Resolve subpixel foam to its mean coverage rather than sparkling at the horizon.
    return lerp(pattern,.5,smoothstep(.25,1.0,footprint));
}
float waterCaustics(float3 localPosition, float time)
{
    float2 p = localPosition.xz * detail.z;
    float2 drift = flow.xy * time * waves.z;
    float first = sin(p.x*2.3 + sin(p.y*1.7 + drift.x)) + sin(p.y*2.1 - drift.y);
    float second = sin(p.x*1.9 - drift.x*.7) + sin(p.y*2.7 + sin(p.x + drift.y));
    // Two intersecting moving ridges approximate refractive focusing; fade with receiver depth.
    float width = max(fwidth(first)+fwidth(second), .08);
    float ridges = 1.0-smoothstep(.0,width+.18,min(abs(first),abs(second)));
    return ridges * exp(-max(-localPosition.y,0.0) / max(foamVisibility.w,.01)) * detail.y;
}
