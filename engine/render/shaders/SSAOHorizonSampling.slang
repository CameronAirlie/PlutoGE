// Horizon-slice AO using the cosine-weighted arc integral from Jimenez et al.,
// Practical Realtime Strategies for Accurate Indirect Occlusion (2016), Algorithm 1.
// Engine integration: right-handed view space, prepared depth, finite world radius,
// receiver-plane bias, and normalized empty-hemisphere response on planar surfaces.
float ssaoArc(float angle, float normalAngle)
{
    return (cos(normalAngle) + 2.0 * angle * sin(normalAngle) - cos(2.0*angle-normalAngle)) * 0.25;
}

float ssaoHorizonVisibility(float2 uv, float3 position, float3 normal,
                            float2 radiusUv, float radius, float bias, float rotation, uint budget)
{
    const float pi = 3.141592654;
    const uint slices = budget >= 18u ? 3u : 2u;
    // Three slices with three samples per side is the 18-tap high-quality
    // GTAO structure. The legacy sample setting is an upper budget, not a
    // requirement to oversample an already temporally accumulated horizon.
    const uint steps = min(3u,max(1u,budget/(2u*slices)));
    const float3 viewDirection = abs(pass.projection[3][2]) > 0.000001 ? normalize(-position) : float3(0,0,1);
    float visibility = 0.0;
    for (uint slice=0; slice<slices; ++slice)
    {
        const float angle = rotation + pi * float(slice)/float(slices);
        const float2 axisUv = float2(cos(angle),sin(angle))*radiusUv;
        const float3 tangent = normalize(ssaoViewPosition(uv+axisUv,-position.z,pass)-position);
        const float3 sliceAxis = normalize(cross(tangent,viewDirection));
        const float3 projectedNormal = normal-sliceAxis*dot(normal,sliceAxis);
        const float projectedLength = length(projectedNormal);
        if (projectedLength < 0.0001) { visibility += 1.0; continue; }
        const float3 sideDirection = normalize(tangent-viewDirection*dot(tangent,viewDirection));
        const float normalAngle = clamp(atan2(dot(projectedNormal,sideDirection),dot(projectedNormal,viewDirection)), -pi*0.5,pi*0.5);
        const float2 emptyHorizon = float2(cos(normalAngle+pi*0.5),cos(normalAngle-pi*0.5));
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
        const float left = max(normalAngle-pi*0.5,-acos(clamp(horizon.y,-1.0,1.0)));
        const float right = min(normalAngle+pi*0.5,acos(clamp(horizon.x,-1.0,1.0)));
        const float integral = ssaoArc(left,normalAngle)+ssaoArc(right,normalAngle);
        const float emptyIntegral = ssaoArc(normalAngle-pi*0.5,normalAngle)+ssaoArc(normalAngle+pi*0.5,normalAngle);
        visibility += saturate(integral/max(emptyIntegral,0.0001));
    }
    return visibility/float(slices);
}
