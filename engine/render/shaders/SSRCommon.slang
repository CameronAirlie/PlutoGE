// Ray generation and depth-buffer marching shared by both SSR techniques (SSR.slang and SSRStochastic.slang).
// Includers declare `pass` (PostProcessParameterBlock) and `depthTexture` (device depth) before including this.
#ifndef PLUTO_SSR_COMMON
#define PLUTO_SSR_COMMON

bool projectRay(float4 clip, out float2 uv, out float projectedDepth)
{
    uv = 0.0;
    projectedDepth = 0.0;
    if (clip.w <= 0.000001) return false;
    const float inverseW = 1.0 / clip.w;
    uv = clip.xy * inverseW;
    projectedDepth = clip.z * inverseW;
    if (pass.zeroToOneDepth == 0) projectedDepth = projectedDepth * 0.5 + 0.5;
    if (pass.flipY != 0) uv.y = -uv.y;
    uv = uv * 0.5 + 0.5;
    return all(uv >= 0.0) && all(uv <= 1.0);
}

bool behindSurface(float2 uv, float projectedDepth, out float sceneDepth)
{
    sceneDepth = depthTexture.SampleLevel(uv, 0.0).r;
    // Both RHI backends use reverse Z. At the same screen coordinate, depth
    // ordering identifies a crossing without reconstructing view-space depth.
    // Keep the full reconstruction below for the final world-unit thickness test.
    return sceneDepth > 0.000001 && sceneDepth >= projectedDepth;
}

float depthDelta(float2 uv, float depth, float rayDepth, float4 viewDepthRow, float4 homogeneousRow)
{
    if (depth <= 0.000001) return -1e20;
    float2 ndc = uv * 2.0 - 1.0;
    if (pass.flipY != 0) ndc.y = -ndc.y;
    float4 clip = float4(ndc, pass.zeroToOneDepth != 0 ? depth : depth * 2.0 - 1.0, 1.0);
    float w = dot(homogeneousRow, clip);
    float safeW = abs(w) > 0.000001 ? w : (w < 0.0 ? -0.000001 : 0.000001);
    return rayDepth + dot(viewDepthRow, clip) / safeW;
}

// Stable pixel-local rotations decorrelate the finite GGX quadrature. Using
// identical directions across a wall creates displaced copies of the source.
float2 reflectionSampleRotation(float2 pixel)
{
    float3 seed = frac(float3(pixel.xyx) * float3(0.1031, 0.1030, 0.0973));
    seed += dot(seed, seed.yzx + 33.33);
    return frac((seed.xx + seed.yz) * seed.zy);
}

float smithG1(float cosine, float alphaSquared)
{
    return 2.0 * cosine / max(cosine + sqrt(alphaSquared +
        (1.0 - alphaSquared) * cosine * cosine), 0.000001);
}

// Samples the GGX distribution of normals visible from the view (VNDF). Unlike sampling the entire NDF, its
// BRDF/pdf weight is bounded at grazing angles, so a few rays cannot dominate a rough reflection. Everything
// that depends only on the pixel is prepared once by makeReflectionLobe.
struct ReflectionLobe
{
    float3 tangent, bitangent, normal, view;
    float alpha;
    float3 stretchedView, t1, t2;

    // The half vector of stratified sample (xi, phi) in [0, 1) x [0, 2 pi).
    float3 halfVector(float xi, float phi)
    {
        const float diskX = sqrt(xi) * cos(phi);
        float diskY = sqrt(xi) * sin(phi);
        const float blend = 0.5 * (1.0 + stretchedView.z);
        diskY = (1.0 - blend) * sqrt(max(0.0, 1.0 - diskX * diskX)) + blend * diskY;
        const float3 projectedNormal = diskX * t1 + diskY * t2 +
            sqrt(max(0.0, 1.0 - diskX * diskX - diskY * diskY)) * stretchedView;
        const float3 localHalf = normalize(float3(alpha * projectedNormal.xy, max(0.0, projectedNormal.z)));
        return tangent * localHalf.x + bitangent * localHalf.y + normal * localHalf.z;
    }
};

ReflectionLobe makeReflectionLobe(float3 normal, float3 view, float ndotv, float alpha)
{
    ReflectionLobe lobe;
    lobe.normal = normal;
    lobe.view = view;
    lobe.alpha = alpha;
    lobe.tangent = normalize(cross(abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0), normal));
    lobe.bitangent = cross(normal, lobe.tangent);
    const float3 localView = float3(dot(view, lobe.tangent), dot(view, lobe.bitangent), ndotv);
    lobe.stretchedView = normalize(float3(alpha * localView.xy, localView.z));
    const float lensq = dot(lobe.stretchedView.xy, lobe.stretchedView.xy);
    lobe.t1 = lensq > 0.000001
        ? float3(-lobe.stretchedView.y, lobe.stretchedView.x, 0.0) / sqrt(lensq) : float3(1, 0, 0);
    lobe.t2 = cross(lobe.stretchedView, lobe.t1);
    return lobe;
}

// Screen position of a ray point; false only when it is behind the eye (off-screen points still project).
bool projectPoint(float4 clip, out float2 uv)
{
    uv = 0.0;
    if (clip.w <= 0.000001) return false;
    uv = clip.xy / clip.w;
    if (pass.flipY != 0) uv.y = -uv.y;
    uv = uv * 0.5 + 0.5;
    return true;
}

// Marches a ray in `steps` samples over [start, start + distance], spaced quadratically, to its first sample
// behind the depth buffer. Returns the travel bracket [low, high] around that crossing, with the screen position
// and scene depth at `high`; false when the ray leaves the screen or ends in front of everything.
bool firstCrossing(float4 clipOrigin, float4 clipRay, float start, float distance, uint steps,
                   out float low, out float high, out float2 uv, out float sceneDepth)
{
    low = 0.0;
    high = 0.0;
    uv = 0.0;
    sceneDepth = 0.0;
    float previousTravel = 0.0;
    for (uint i = 0; i < steps; ++i)
    {
        const float fraction = (float(i) + 1.0) / float(steps);
        const float travel = start + distance * fraction * fraction;
        float projectedDepth;
        if (!projectRay(clipOrigin + clipRay * travel, uv, projectedDepth)) return false;
        // Every preceding sample is in front of the scene.
        if (behindSurface(uv, projectedDepth, sceneDepth))
        {
            low = previousTravel;
            high = travel;
            return true;
        }
        previousTravel = travel;
    }
    return false;
}

#endif
