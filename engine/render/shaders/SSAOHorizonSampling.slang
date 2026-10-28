// Horizon-slice AO using the cosine-weighted arc integral from Jimenez et al.,
// Practical Realtime Strategies for Accurate Indirect Occlusion (2016), Algorithm 1.
// Engine integration: right-handed view space, prepared depth, finite world radius,
// receiver-plane bias, and normalized empty-hemisphere response on planar surfaces.

// XeGTAO's acos approximation (max error about 0.0001 rad over [-1, 1]); a full acos is a long sequence on GCN.
float fastAcos(float x)
{
    const float magnitude = abs(x);
    const float result = (-0.156583 * magnitude + 1.570796327) * sqrt(max(1.0 - magnitude, 0.0));
    return x >= 0.0 ? result : 3.141592654 - result;
}

// Cosine-weighted visible arc from the normal angle to horizon angle h: (cos n + 2 h sin n - cos(2h - n)) / 4.
float ssaoArc(float angle, float cosNormal, float sinNormal, float normalAngle)
{
    return (cosNormal + 2.0 * angle * sinNormal - cos(2.0 * angle - normalAngle)) * 0.25;
}

// Sample budget to slices x steps per side. Three slices of three steps is the 18-tap high-quality GTAO structure;
// eight samples give XeGTAO's medium two by two. The budget is an upper bound, not a requirement to oversample a
// temporally accumulated horizon.
uint2 ssaoHorizonPattern(uint budget)
{
    const uint slices = budget >= 18u ? 3u : (budget >= 8u ? 2u : 1u);
    return uint2(slices, min(3u, max(2u, budget / (2u * slices))));
}

float ssaoHorizonVisibility(float2 uv, float3 position, float3 normal,
                            float2 radiusUv, float radius, float bias, float rotation, uint budget)
{
    const float pi = 3.141592654;
    const uint2 pattern = ssaoHorizonPattern(budget);
    const uint slices = pattern.x;
    const uint steps = pattern.y;
    const float3 viewDirection = abs(pass.projection[3][2]) > 0.000001 ? normalize(-position) : float3(0,0,1);
    float visibility = 0.0;
    for (uint slice=0; slice<slices; ++slice)
    {
        const float angle = rotation + pi * float(slice)/float(slices);
        const float2 axisUv = float2(cos(angle),sin(angle))*radiusUv;
        const float3 tangent = normalize(viewPositionFromLinearDepth(uv+axisUv,-position.z,pass)-position);
        const float3 sliceAxis = normalize(cross(tangent,viewDirection));
        const float3 projectedNormal = normal-sliceAxis*dot(normal,sliceAxis);
        const float projectedLength = length(projectedNormal);
        if (projectedLength < 0.0001) { visibility += 1.0; continue; }
        const float3 sideDirection = normalize(tangent-viewDirection*dot(tangent,viewDirection));
        // The projected normal's signed angle from the view direction, clamped to the visible half circle.
        const float cosNormal = saturate(dot(projectedNormal,viewDirection)/projectedLength);
        const float normalAngle = (dot(projectedNormal,sideDirection) >= 0.0 ? 1.0 : -1.0) * fastAcos(cosNormal);
        const float sinNormal = sin(normalAngle);
        // cos(n + pi/2) and cos(n - pi/2).
        const float2 emptyHorizon = float2(-sinNormal, sinNormal);
        float2 horizon = emptyHorizon;
        for (uint step=0; step<steps; ++step)
        {
            // Stratified, center-heavy search preserves crevices without adding
            // redundant samples at the center texel. Rotation changes each frame.
            const float jitter = frac(rotation*0.159154943 + float(slice+step*steps)*0.618033989);
            const float fraction = (float(step)+0.5+jitter*0.5)/float(steps);
            const float2 offsetPixels = axisUv * fraction*fraction / pass.inverseResolution;
            const float2 offset = round(offsetPixels)*pass.inverseResolution;
            [ForceUnroll] for (uint side=0; side<2; ++side)
            {
                const float2 sampleUv = uv + (side==0 ? offset : -offset);
                if (any(sampleUv<0.0) || any(sampleUv>1.0)) continue;
                float depth;
                const float3 samplePosition = ssaoPosition(sampleUv,offsetPixels,depth);
                if (depth<=0.0) continue;
                const float3 delta = samplePosition-position;
                const float distance = length(delta);
                if (distance<=0.0001 || dot(delta,normal)<=bias) continue;
                const float falloff = saturate((radius-distance)/max(radius*0.5,0.0001));
                const float cosine = dot(delta,viewDirection)/distance;
                horizon[side] = max(horizon[side],lerp(emptyHorizon[side],cosine,falloff));
            }
        }
        const float left = max(normalAngle-pi*0.5,-fastAcos(clamp(horizon.y,-1.0,1.0)));
        const float right = min(normalAngle+pi*0.5,fastAcos(clamp(horizon.x,-1.0,1.0)));
        const float integral = ssaoArc(left,cosNormal,sinNormal,normalAngle)+ssaoArc(right,cosNormal,sinNormal,normalAngle);
        // The arcs at n -/+ pi/2 sum to cos n + n sin n.
        const float emptyIntegral = cosNormal + normalAngle * sinNormal;
        visibility += saturate(integral/max(emptyIntegral,0.0001));
    }
    return visibility/float(slices);
}
