// The microfacet BRDF opaque and transparent surfaces shade analytic lights with (BasicLit.slang): Lambert diffuse,
// GGX distribution, Schlick-GGX Smith masking with k = (r + 1)^2 / 8 (Karis's remap for analytic lights) and
// Schlick Fresnel, or exact dielectric Fresnel for glass. On top of the single-scattering model:
//
//  - multiple scattering: specular is scaled by specularEnergyCompensation (EnvironmentBRDF.slang), so rough metals
//    keep the energy their environment reflections also keep;
//  - geometric specular antialiasing: roughness is widened by the screen-space variance of the shading normal, so
//    highlights on dense or curved geometry do not alias into sparkle (surfaceAntialiasedRoughness);
//  - area lights: spheres and tubes use the representative point on the light nearest the reflection ray, with the
//    distribution's normalisation widened by the light's angular size (Karis, "Real Shading in Unreal Engine 4").
//
// Each light is evaluated as brdfEvaluate(surface, light): reflectance times n.l, split into diffuse and specular so
// glass and shader-graph lighting can weigh them separately.
#ifndef PLUTO_SURFACE_BRDF
#define PLUTO_SURFACE_BRDF

#include "EnvironmentBRDF.slang"

static const float BrdfPi = 3.14159265358979;

// Exact unpolarized air/dielectric Fresnel, including index-matched glass.
float dielectricFresnel(float cosine, float ior)
{
    if (ior <= 1.000001) return 0.0;
    cosine = saturate(cosine);
    const float transmittedCosine = sqrt(max(1.0 - (1.0 - cosine * cosine) / (ior * ior), 0.0));
    const float rs = (cosine - ior * transmittedCosine) / max(cosine + ior * transmittedCosine, 0.000001);
    const float rp = (ior * cosine - transmittedCosine) / max(ior * cosine + transmittedCosine, 0.000001);
    return 0.5 * (rs * rs + rp * rp);
}

// Perceptual roughness widened by the variance of the geometric normal over the pixel footprint (Tokuyoshi and
// Kaplanyan, "Improved Geometric Specular Antialiasing", 2019; constants as Filament's). `dx` and `dy` are the
// interpolated normal's screen derivatives, taken where every lane of the quad is still alive. Normal maps alias
// less: their mip chain already averages them.
float surfaceAntialiasedRoughness(float3 dx, float3 dy, float roughness)
{
    const float variance = 0.15 * (dot(dx, dx) + dot(dy, dy));
    const float kernel = min(2.0 * variance, 0.2);
    const float alpha = roughness * roughness;
    return sqrt(sqrt(saturate(alpha * alpha + kernel)));
}

struct BrdfSurface
{
    float3 normal, view;
    float3 diffuseColor; // albedo times (1 - metallic)
    float3 f0;
    float roughness, alphaSquared, smithK;
    float ndotv, smithView;
    float3 energyCompensation;
    float glassIor;      // greater than one selects exact dielectric Fresnel
};

BrdfSurface brdfSurface(float3 normal, float3 view, float3 albedo, float metallic, float roughness, float3 f0,
                        float glassIor)
{
    BrdfSurface surface;
    surface.normal = normal;
    surface.view = view;
    surface.diffuseColor = albedo * (1.0 - metallic);
    surface.f0 = f0;
    surface.roughness = roughness;
    const float alpha = roughness * roughness;
    surface.alphaSquared = alpha * alpha;
    surface.smithK = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    surface.ndotv = max(saturate(dot(normal, view)), 0.0001);
    surface.smithView = surface.ndotv / (surface.ndotv * (1.0 - surface.smithK) + surface.smithK);
    surface.energyCompensation = specularEnergyCompensation(f0, roughness, surface.ndotv);
    surface.glassIor = glassIor;
    return surface;
}

float3 brdfFresnel(BrdfSurface surface, float hdotv)
{
    if (surface.glassIor > 1.0) return float3(dielectricFresnel(hdotv, surface.glassIor));
    return surface.f0 + (1.0 - surface.f0) * pow(1.0 - hdotv, 5.0);
}

float brdfDistribution(float alphaSquared, float ndoth)
{
    const float denominator = ndoth * ndoth * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(BrdfPi * denominator * denominator, 0.0001);
}

// An analytic light as the surface sees it: the (unit) direction towards its representative point and how much a
// non-point source widens the specular lobe's normalisation (1 for a point).
struct BrdfLight
{
    float3 direction;
    float specularNormalization;
};

BrdfLight brdfPointLight(float3 direction)
{
    BrdfLight light;
    light.direction = direction;
    light.specularNormalization = 1.0;
    return light;
}

// A sphere of `radius` (and, with `length` above zero, a tube along `axis` of that length) centred `offset` from the
// surface. Diffuse keeps the centre direction; specular uses the point on the light closest to the reflection ray.
// `centreDirection` returns the direction to the centre, which shadows and attenuation keep using.
BrdfLight brdfAreaLight(BrdfSurface surface, float3 offset, float radius, float3 axis, float length,
                        out float3 centreDirection)
{
    centreDirection = normalize(offset);
    const float3 reflection = reflect(-surface.view, surface.normal);
    float3 toLight = offset;
    const float alpha = surface.roughness * surface.roughness;
    float normalization = 1.0;
    if (length > 0.0)
    {
        // Closest point on the segment to the reflection ray (Karis, equation 19), then the sphere around it.
        const float3 start = offset - axis * (0.5 * length);
        const float3 segment = axis * length;
        const float rs = dot(reflection, segment);
        const float t = saturate((dot(reflection, start) * rs - dot(start, segment)) /
                                 max(dot(segment, segment) - rs * rs, 0.000001));
        toLight = start + segment * t;
        // Energy conservation for the line's extent.
        const float distance = max(sqrt(dot(offset, offset)), 0.0001);
        normalization *= alpha / saturate(alpha + 0.5 * length / distance);
    }
    if (radius > 0.0)
    {
        const float3 toRay = dot(toLight, reflection) * reflection - toLight;
        toLight += toRay * saturate(radius / max(sqrt(dot(toRay, toRay)), 0.0001));
        const float distance = max(sqrt(dot(offset, offset)), 0.0001);
        const float widened = saturate(alpha + 0.5 * radius / distance);
        normalization *= (alpha * alpha) / max(widened * widened, 0.000001);
    }
    BrdfLight light;
    light.direction = normalize(toLight);
    light.specularNormalization = normalization;
    return light;
}

// Diffuse and specular reflectance times n.l for one light; the caller multiplies by its radiance.
void brdfEvaluate(BrdfSurface surface, BrdfLight light, float ndotl, out float3 diffuse, out float3 specular)
{
    const float3 h = normalize(light.direction + surface.view);
    const float specularNdotl = saturate(dot(surface.normal, light.direction));
    const float ndoth = saturate(dot(surface.normal, h));
    const float hdotv = saturate(dot(h, surface.view));
    const float3 fresnel = brdfFresnel(surface, hdotv);
    const float smithLight = specularNdotl / max(specularNdotl * (1.0 - surface.smithK) + surface.smithK, 0.0001);
    const float distribution = brdfDistribution(surface.alphaSquared, ndoth) * light.specularNormalization;
    specular = distribution * surface.smithView * smithLight * fresnel * surface.energyCompensation /
               max(4.0 * surface.ndotv * specularNdotl, 0.0001) * specularNdotl;
    diffuse = (1.0 - fresnel) * surface.diffuseColor / BrdfPi * ndotl;
}

#endif
