#include "LightmapBake.h"

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace PlutoGE::scene::lightmap
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;
        // Texels a draw gets at least along each side, so a tiny prop still has a gradient.
        constexpr std::uint32_t kMinTargetTexels = 4;

        glm::vec2 TriangleUv(const bake::BakeTriangle &triangle, int corner) { return triangle.lightmapUvs[corner]; }

        double UvArea(const bake::BakeTriangle &triangle)
        {
            const glm::dvec2 a = TriangleUv(triangle, 0), b = TriangleUv(triangle, 1), c = TriangleUv(triangle, 2);
            return 0.5 * std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
        }

        double WorldArea(const bake::BakeTriangle &triangle)
        {
            return 0.5 * glm::length(glm::cross(glm::dvec3(triangle.worldPositions[1] - triangle.worldPositions[0]),
                                                glm::dvec3(triangle.worldPositions[2] - triangle.worldPositions[0])));
        }

        // Deterministic per-texel sequences: a Hammersley set rotated by a hash of the texel (Cranley-Patterson), so
        // neighbouring texels see decorrelated, well-stratified directions and rebakes are identical.
        std::uint32_t Hash(std::uint32_t value)
        {
            value ^= value >> 16; value *= 0x7feb352du;
            value ^= value >> 15; value *= 0x846ca68bu;
            value ^= value >> 16;
            return value;
        }
        float RadicalInverse(std::uint32_t bits)
        {
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            return float(bits) * 2.3283064365386963e-10f;
        }
        float Unit(std::uint32_t value) { return float(Hash(value) >> 8) / float(1u << 24); }

        glm::vec3 CosineDirection(const glm::vec3 &normal, float u, float v)
        {
            glm::vec3 tangent, bitangent;
            bake::BuildTangentBasis(normal, tangent, bitangent);
            const float radius = std::sqrt(u);
            const float angle = 2.0f * kPi * v;
            return glm::normalize(tangent * (radius * std::cos(angle)) + bitangent * (radius * std::sin(angle)) +
                                  normal * std::sqrt(std::max(0.0f, 1.0f - u)));
        }

        struct Texel
        {
            glm::vec3 position{0.0f};
            glm::vec3 normal{0.0f};          // shading
            glm::vec3 geometricNormal{0.0f};
            float rayEpsilon = 0.0f;
            int target = -1;                  // covered texels only
        };

        struct Surface
        {
            glm::vec3 albedo{0.0f};
            glm::vec3 emission{0.0f}; // radiance
            bool unlit = false;       // shader graph surfaces that output their colour as radiance
        };

        Surface SampleSurface(const bake::BakeTriangle &triangle, const glm::vec3 &barycentric, const bake::TextureCache &textures)
        {
            Surface surface;
            surface.albedo = bake::SampleTriangleAlbedo(triangle, barycentric, textures);
            // As the probe bake: emission is tinted by the albedo.
            surface.emission = triangle.graphMaterial->emission * surface.albedo;
            if (triangle.graphMaterial->shaderGraphProgram)
            {
                const auto graph = bake::SampleBakeGraph(triangle, barycentric);
                if (triangle.graphMaterial->shaderGraphProgram->data.header.y != 0)
                {
                    surface.unlit = true;
                    surface.emission = glm::vec3(graph.color);
                }
                surface.emission += graph.emission;
            }
            return surface;
        }

        class Integrator
        {
        public:
            explicit Integrator(const LightmapBakeInput &input) : m_input(input) {}

            // Mean radiance (irradiance / pi) arriving along one path, after up to `bounces` surface interactions.
            glm::vec3 TracePath(glm::vec3 origin, glm::vec3 direction, std::uint32_t seed) const
            {
                const auto &triangles = *m_input.triangles;
                glm::vec3 radiance(0.0f), throughput(1.0f);
                const int bounces = m_input.settings.bounces;
                const float strength = m_input.settings.bounceStrength;
                for (int depth = 0;; ++depth)
                {
                    const auto hit = bake::TraceScene(origin, direction, std::numeric_limits<float>::max(), triangles,
                                                      *m_input.acceleration, m_input.textures);
                    if (!hit)
                    {
                        radiance += throughput * m_input.environment->Radiance(direction);
                        break;
                    }
                    if (depth >= bounces)
                        break;
                    const auto &triangle = triangles[hit->triangleIndex];
                    const glm::vec3 geometric = bake::ComputeTriangleNormal(triangle);
                    // The far side of a closed surface is inside geometry: no light comes from there.
                    if (glm::dot(geometric, direction) > 0.0f)
                        break;
                    const glm::vec3 position = triangle.worldPositions[0] * hit->barycentric.x +
                                               triangle.worldPositions[1] * hit->barycentric.y +
                                               triangle.worldPositions[2] * hit->barycentric.z;
                    glm::vec3 shading = bake::ResolveInterpolatedNormal(triangle, hit->barycentric);
                    if (glm::dot(shading, geometric) < 0.0f)
                        shading = geometric;
                    const auto surface = SampleSurface(triangle, hit->barycentric, *m_input.textures);
                    const float epsilon = bake::ResolveRayEpsilon(triangle);
                    glm::vec3 outgoing = surface.emission;
                    if (!surface.unlit)
                    {
                        const glm::vec3 irradiance = bake::EvaluateStaticLightIrradiance(
                            position, shading, geometric, epsilon, *m_input.lights, triangles, *m_input.acceleration,
                            *m_input.textures, 1, true);
                        outgoing += surface.albedo * irradiance / kPi;
                    }
                    radiance += throughput * strength * outgoing;
                    if (surface.unlit || depth + 1 >= bounces)
                        break;
                    throughput *= surface.albedo * strength;
                    if (std::max({throughput.r, throughput.g, throughput.b}) < 0.001f)
                        break;
                    origin = position + geometric * epsilon;
                    const std::uint32_t next = seed * 2654435761u + std::uint32_t(depth) * 40503u;
                    direction = CosineDirection(shading, Unit(next), Unit(next ^ 0x9e3779b9u));
                }
                return radiance;
            }

            struct Result
            {
                glm::vec3 direct{0.0f};
                glm::vec3 indirect{0.0f};
                float shadowMask = 1.0f;
            };

            Result Integrate(const Texel &texel, std::uint32_t texelIndex) const
            {
                Result result;
                const auto &settings = m_input.settings;
                const auto &lights = *m_input.lights;
                const glm::vec3 origin = texel.position + texel.geometricNormal * texel.rayEpsilon;
                for (std::size_t index = 0; index < lights.size(); ++index)
                {
                    const auto &light = lights[index];
                    if (light.mode == render::LightBakeMode::Baked)
                    {
                        const auto sample = bake::EvaluateBakeLight(light, texel.position, texel.normal, texel.geometricNormal,
                            texel.rayEpsilon, *m_input.triangles, *m_input.acceleration, *m_input.textures,
                            settings.directShadowSamples, false);
                        result.direct += sample.irradiance * sample.visibility / kPi;
                    }
                    if (static_cast<int>(index) == m_input.shadowMaskLight)
                    {
                        // Regardless of N.L: the renderer's own shading handles the side facing away.
                        const auto path = bake::ResolveBakeLightPath(light, texel.position, texel.rayEpsilon);
                        result.shadowMask = bake::TraceBakeLightVisibility(light, origin, path, *m_input.triangles,
                            *m_input.acceleration, *m_input.textures, settings.directShadowSamples, false);
                    }
                }
                const int samples = settings.indirectSamples;
                if (samples > 0)
                {
                    const std::uint32_t seed = Hash(texelIndex * 9781u + 1u);
                    const float rotateU = Unit(seed), rotateV = Unit(seed ^ 0x68bc21ebu);
                    glm::vec3 sum(0.0f);
                    for (int sample = 0; sample < samples; ++sample)
                    {
                        const float u = std::fmod((float(sample) + 0.5f) / float(samples) + rotateU, 1.0f);
                        const float v = std::fmod(RadicalInverse(std::uint32_t(sample)) + rotateV, 1.0f);
                        const glm::vec3 direction = CosineDirection(texel.normal, u, v);
                        // A direction under the triangle's plane would start inside the surface.
                        if (glm::dot(direction, texel.geometricNormal) <= 0.0f)
                            continue;
                        sum += TracePath(origin, direction, seed + std::uint32_t(sample) * 7919u);
                    }
                    result.indirect = sum / float(samples);
                }
                return result;
            }

        private:
            const LightmapBakeInput &m_input;
        };

        // Edge-aware a-trous filter over one target's covered texels: neighbours on the same plane facing the same way
        // average, so the gathered noise smooths without blurring creases or contact edges.
        void Denoise(std::vector<glm::vec3> &values, const std::vector<Texel> &texels, std::uint32_t width, std::uint32_t height,
                     float texelWorldSize)
        {
            constexpr float kernel[3] = {3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f};
            std::vector<glm::vec3> filtered(values.size());
            const float planeScale = 1.0f / std::max(texelWorldSize, 1.0e-4f);
            for (int pass = 0; pass < 3; ++pass)
            {
                const int step = 1 << pass;
                for (std::uint32_t y = 0; y < height; ++y)
                    for (std::uint32_t x = 0; x < width; ++x)
                    {
                        const auto index = std::size_t(y) * width + x;
                        const auto &center = texels[index];
                        if (center.target < 0)
                        {
                            filtered[index] = values[index];
                            continue;
                        }
                        glm::vec3 sum(0.0f);
                        float weightSum = 0.0f;
                        for (int dy = -2; dy <= 2; ++dy)
                            for (int dx = -2; dx <= 2; ++dx)
                            {
                                const int sx = int(x) + dx * step, sy = int(y) + dy * step;
                                if (sx < 0 || sy < 0 || sx >= int(width) || sy >= int(height))
                                    continue;
                                const auto neighbourIndex = std::size_t(sy) * width + std::size_t(sx);
                                const auto &neighbour = texels[neighbourIndex];
                                if (neighbour.target != center.target)
                                    continue;
                                const float facing = std::max(glm::dot(center.normal, neighbour.normal), 0.0f);
                                const float plane = std::abs(glm::dot(center.geometricNormal, neighbour.position - center.position)) * planeScale;
                                const float weight = kernel[std::abs(dx)] * kernel[std::abs(dy)] * std::pow(facing, 8.0f) *
                                                     std::exp(-plane * plane);
                                sum += values[neighbourIndex] * weight;
                                weightSum += weight;
                            }
                        filtered[index] = weightSum > 0.0f ? sum / weightSum : values[index];
                    }
                values.swap(filtered);
            }
        }
    }

    std::vector<LightmapTarget> CollectTargets(const std::vector<bake::BakeTriangle> &triangles, LightmapTargetReport &report)
    {
        std::map<std::pair<EntityID, std::uint32_t>, std::size_t> lookup;
        std::vector<LightmapTarget> targets;
        std::map<std::pair<EntityID, std::uint32_t>, bool> rejected;
        for (std::uint32_t index = 0; index < triangles.size(); ++index)
        {
            const auto &triangle = triangles[index];
            const auto *mesh = triangle.meshComponent;
            if (!mesh || !mesh->GetOwner())
                continue; // terrain and foliage
            const std::pair<EntityID, std::uint32_t> key{mesh->GetOwner()->GetID(), triangle.submeshIndex};
            if (rejected.contains(key))
                continue;
            auto found = lookup.find(key);
            if (found == lookup.end())
            {
                if (!mesh->GetMesh() || !mesh->GetMesh()->HasLightmapUvs())
                {
                    rejected[key] = true;
                    report.withoutUvs.push_back(mesh->GetOwner()->GetName());
                    continue;
                }
                LightmapTarget target;
                target.key = {key.first, key.second};
                target.name = mesh->GetOwner()->GetName();
                target.uvMin = glm::vec2(std::numeric_limits<float>::max());
                target.uvMax = glm::vec2(std::numeric_limits<float>::lowest());
                found = lookup.emplace(key, targets.size()).first;
                targets.push_back(std::move(target));
            }
            auto &target = targets[found->second];
            target.triangles.push_back(index);
            target.worldArea += WorldArea(triangle);
            target.uvArea += UvArea(triangle);
            for (int corner = 0; corner < 3; ++corner)
            {
                target.uvMin = glm::min(target.uvMin, TriangleUv(triangle, corner));
                target.uvMax = glm::max(target.uvMax, TriangleUv(triangle, corner));
            }
        }
        // Layouts that leave [0,1] would reach into other draws' texels; layouts without area have none of their own.
        std::erase_if(targets, [&](const LightmapTarget &target) {
            constexpr float tolerance = 1.0e-3f;
            const bool usable = target.uvArea > 1.0e-9 && target.worldArea > 1.0e-9 &&
                                glm::all(glm::greaterThanEqual(target.uvMin, glm::vec2(-tolerance))) &&
                                glm::all(glm::lessThanEqual(target.uvMax, glm::vec2(1.0f + tolerance))) &&
                                glm::all(glm::greaterThan(target.uvMax - target.uvMin, glm::vec2(1.0e-6f)));
            if (!usable)
                report.unusableUvs.push_back(target.name);
            return !usable;
        });
        return targets;
    }

    std::optional<LightmapLayout> LayoutTargets(const std::vector<LightmapTarget> &targets, const LightmapBakeSettings &settings)
    {
        if (targets.empty())
            return std::nullopt;
        const auto maxSize = static_cast<std::uint32_t>(settings.maxAtlasSize);
        const auto padding = static_cast<std::uint32_t>(settings.padding);
        float density = settings.texelsPerUnit;
        for (int attempt = 0; attempt < 64; ++attempt, density *= 0.9f)
        {
            std::vector<render::PackSize> sizes;
            sizes.reserve(targets.size());
            double area = 0.0;
            bool tooLarge = false;
            for (const auto &target : targets)
            {
                // World metres per unit of UV, so the draw's texels are density per metre on average.
                const double metresPerUv = std::sqrt(target.worldArea / target.uvArea);
                const glm::dvec2 extent = glm::dvec2(target.uvMax - target.uvMin) * metresPerUv * double(density);
                const render::PackSize inner{std::max(kMinTargetTexels, static_cast<std::uint32_t>(std::ceil(extent.x))),
                                             std::max(kMinTargetTexels, static_cast<std::uint32_t>(std::ceil(extent.y)))};
                const render::PackSize padded{inner.width + 2 * padding, inner.height + 2 * padding};
                tooLarge = tooLarge || padded.width > maxSize || padded.height > maxSize;
                sizes.push_back(padded);
                area += double(padded.width) * padded.height;
            }
            if (tooLarge || area > double(maxSize) * maxSize)
                continue;
            const auto width = std::clamp(static_cast<std::uint32_t>(std::ceil(std::sqrt(area) * 1.08)), 64u, maxSize);
            std::uint32_t usedHeight = 0;
            const auto positions = render::SkylinePacker::Pack(sizes, width, maxSize, &usedHeight);
            if (!positions)
                continue;
            LightmapLayout layout;
            layout.width = width;
            layout.height = std::max<std::uint32_t>((usedHeight + 3u) & ~3u, 4u);
            layout.texelsPerUnit = density;
            layout.placements.reserve(targets.size());
            for (std::size_t index = 0; index < targets.size(); ++index)
            {
                LightmapPlacement placement;
                placement.origin = {(*positions)[index].x + padding, (*positions)[index].y + padding};
                placement.size = {sizes[index].width - 2 * padding, sizes[index].height - 2 * padding};
                // atlas UV = uv2 * scale + offset maps the draw's UV bounds onto its texels.
                const auto &target = targets[index];
                const glm::dvec2 uvExtent = glm::dvec2(target.uvMax - target.uvMin);
                const glm::dvec2 scale = glm::dvec2(placement.size.width / double(layout.width), placement.size.height / double(layout.height)) / uvExtent;
                const glm::dvec2 offset = glm::dvec2(placement.origin.x / double(layout.width), placement.origin.y / double(layout.height)) -
                                          glm::dvec2(target.uvMin) * scale;
                placement.rect = {glm::vec2(scale), glm::vec2(offset)};
                layout.placements.push_back(placement);
            }
            return layout;
        }
        return std::nullopt;
    }

    render::LightmapAtlas BakeAtlas(const std::vector<LightmapTarget> &targets, const LightmapLayout &layout,
                                    const LightmapBakeInput &input, const std::shared_ptr<std::atomic<bool>> &cancel,
                                    LightmapProgress *progress)
    {
        const std::uint32_t width = layout.width, height = layout.height;
        const std::size_t texelCount = std::size_t(width) * height;
        const auto &triangles = *input.triangles;
        std::vector<Texel> texels(texelCount);
        // The target whose padded rectangle holds each texel, for dilation.
        std::vector<int> owner(texelCount, -1);
        const auto padding = static_cast<std::uint32_t>(input.settings.padding);

        // Rasterise: texel centres inside a triangle take its interpolated surface; then texels a triangle only
        // grazes (within one texel of its edge) take its nearest point, so bilinear reads at chart borders stay lit.
        for (std::size_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex)
        {
            const auto &placement = layout.placements[targetIndex];
            for (std::uint32_t y = placement.origin.y - padding; y < placement.origin.y + placement.size.height + padding; ++y)
                for (std::uint32_t x = placement.origin.x - padding; x < placement.origin.x + placement.size.width + padding; ++x)
                    owner[std::size_t(y) * width + x] = static_cast<int>(targetIndex);
            for (int pass = 0; pass < 2; ++pass)
                for (const auto triangleIndex : targets[targetIndex].triangles)
                {
                    const auto &triangle = triangles[triangleIndex];
                    glm::dvec2 p[3];
                    for (int corner = 0; corner < 3; ++corner)
                    {
                        const glm::dvec2 atlasUv = glm::dvec2(TriangleUv(triangle, corner)) * glm::dvec2(placement.rect.scale) +
                                                   glm::dvec2(placement.rect.offset);
                        p[corner] = atlasUv * glm::dvec2(width, height);
                    }
                    const double area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[1].y - p[0].y) * (p[2].x - p[0].x);
                    if (std::abs(area) < 1.0e-12)
                        continue;
                    const double grow = pass == 0 ? 0.0 : 1.0;
                    const auto lo = glm::max(glm::floor(glm::min(p[0], glm::min(p[1], p[2])) - grow), glm::dvec2(0.0));
                    const auto hi = glm::min(glm::ceil(glm::max(p[0], glm::max(p[1], p[2])) + grow), glm::dvec2(width, height));
                    const glm::vec3 geometric = bake::ComputeTriangleNormal(triangle);
                    const float epsilon = bake::ResolveRayEpsilon(triangle);
                    for (auto y = std::uint32_t(lo.y); y < std::uint32_t(hi.y); ++y)
                        for (auto x = std::uint32_t(lo.x); x < std::uint32_t(hi.x); ++x)
                        {
                            const auto index = std::size_t(y) * width + x;
                            if (texels[index].target >= 0 || owner[index] != static_cast<int>(targetIndex))
                                continue;
                            const glm::dvec2 c(x + 0.5, y + 0.5);
                            glm::dvec3 b((p[1].x - c.x) * (p[2].y - c.y) - (p[1].y - c.y) * (p[2].x - c.x),
                                         (p[2].x - c.x) * (p[0].y - c.y) - (p[2].y - c.y) * (p[0].x - c.x),
                                         (p[0].x - c.x) * (p[1].y - c.y) - (p[0].y - c.y) * (p[1].x - c.x));
                            b /= area;
                            if (pass == 0 && (b.x < 0.0 || b.y < 0.0 || b.z < 0.0))
                                continue;
                            if (pass == 1)
                            {
                                // The nearest point of the triangle, approximately: clamp and renormalise.
                                if (b.x < -1.5 || b.y < -1.5 || b.z < -1.5)
                                    continue;
                                b = glm::max(b, glm::dvec3(0.0));
                                const double sum = b.x + b.y + b.z;
                                if (sum <= 0.0)
                                    continue;
                                b /= sum;
                            }
                            const glm::vec3 barycentric(b);
                            auto &texel = texels[index];
                            texel.position = triangle.worldPositions[0] * barycentric.x + triangle.worldPositions[1] * barycentric.y +
                                             triangle.worldPositions[2] * barycentric.z;
                            texel.normal = bake::ResolveInterpolatedNormal(triangle, barycentric);
                            if (glm::dot(texel.normal, geometric) < 0.0f)
                                texel.normal = geometric;
                            texel.geometricNormal = geometric;
                            texel.rayEpsilon = epsilon;
                            texel.target = static_cast<int>(targetIndex);
                        }
                }
        }

        std::vector<std::uint32_t> covered;
        for (std::uint32_t index = 0; index < texelCount; ++index)
            if (texels[index].target >= 0)
                covered.push_back(index);
        if (progress)
            progress->totalTexels.store(covered.size(), std::memory_order_relaxed);

        std::vector<glm::vec3> direct(texelCount, glm::vec3(0.0f)), indirect(texelCount, glm::vec3(0.0f));
        std::vector<float> mask(texelCount, 1.0f);
        const Integrator integrator(input);
        const auto workers = bake::ResolveBakeWorkerCount(covered.size(), 64);
        bake::ParallelFor(covered.size(), workers, [&](std::size_t task, std::size_t) {
            if (bake::IsBakeCancelled(cancel))
                return;
            const auto index = covered[task];
            const auto result = integrator.Integrate(texels[index], index);
            direct[index] = result.direct;
            indirect[index] = result.indirect;
            mask[index] = result.shadowMask;
            if (progress)
                progress->completedTexels.fetch_add(1, std::memory_order_relaxed);
        });

        render::LightmapAtlas atlas;
        if (bake::IsBakeCancelled(cancel))
            return atlas;
        if (input.settings.denoise && input.settings.indirectSamples > 0)
            Denoise(indirect, texels, width, height, 1.0f / std::max(layout.texelsPerUnit, 1.0e-3f));

        // Compose, then dilate every target's texels into its padding and chart gutters.
        std::vector<glm::vec4> values(texelCount, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        std::vector<bool> filled(texelCount, false);
        for (const auto index : covered)
        {
            values[index] = glm::vec4(direct[index] + indirect[index], mask[index]);
            filled[index] = true;
        }
        for (std::uint32_t pass = 0; pass < padding + 2; ++pass)
        {
            std::vector<std::pair<std::size_t, glm::vec4>> grown;
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const auto index = std::size_t(y) * width + x;
                    if (filled[index] || owner[index] < 0)
                        continue;
                    glm::vec4 sum(0.0f);
                    int count = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            const int sx = int(x) + dx, sy = int(y) + dy;
                            if ((dx == 0 && dy == 0) || sx < 0 || sy < 0 || sx >= int(width) || sy >= int(height))
                                continue;
                            const auto neighbour = std::size_t(sy) * width + std::size_t(sx);
                            if (filled[neighbour] && owner[neighbour] == owner[index])
                            {
                                sum += values[neighbour];
                                ++count;
                            }
                        }
                    if (count > 0)
                        grown.emplace_back(index, sum / float(count));
                }
            if (grown.empty())
                break;
            for (const auto &[index, value] : grown)
            {
                values[index] = value;
                filled[index] = true;
            }
        }

        atlas.width = width;
        atlas.height = height;
        atlas.encoding = render::LightmapEncoding::IrradianceShadowMask;
        atlas.texels.resize(texelCount * 4);
        for (std::size_t index = 0; index < texelCount; ++index)
        {
            const auto value = glm::max(values[index], glm::vec4(0.0f));
            for (int channel = 0; channel < 4; ++channel)
                atlas.texels[index * 4 + channel] = glm::packHalf1x16(std::min(value[channel], 65000.0f));
        }
        return atlas;
    }
}
