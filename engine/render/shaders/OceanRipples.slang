// Interaction rings from scene::water::WaterRippleField. Mirrors EvaluateRippleProfile and
// WaterRippleField::SampleGradient: a Gaussian wave packet riding on each expanding ring.
struct OceanRippleSample
{
    float2 gradient; // local height slope (x, z) added to the wave normal
    float foam;      // 0..1 whitening of fresh rings
};
OceanRippleSample sampleRipples(float2 position)
{
    OceanRippleSample result = {float2(0.0), 0.0};
    int count = min(rippleCount.x, 32);
    float waveNumber = rippleShape.x;
    float width = max(rippleShape.y, .0001);
    float crest = 0.0;
    for (int i = 0; i < count; ++i)
    {
        float4 ring = ripples[i];
        float2 offset = position - ring.xy;
        float distance = length(offset);
        float x = distance - ring.z;
        // Beyond three packet widths the envelope is below 1e-4: skip the transcendental work.
        if (abs(x) > width * 3.0 || distance < .00001) continue;
        float envelope = exp(-(x * x) / (width * width));
        float sine, cosine;
        sincos(waveNumber * x, sine, cosine);
        float slope = ring.w * envelope * (-2.0 * x / (width * width) * cosine - waveNumber * sine);
        result.gradient += slope * offset / distance;
        crest += ring.w * envelope * max(cosine, 0.0);
    }
    // A 4 cm crest fully whitens; the profile's foam setting scales it.
    result.foam = saturate(crest / .04) * saturate(rippleShape.z);
    return result;
}
