#include "PlutoGE/render/DetailGrass.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <bit>
#include <cmath>

// The CPU half of detail grass. DetailGrassGenerate.slang mirrors the lattice (levels, ranks, jitter, Keep, rings)
// line for line; DetailGrassTests checks the properties the GPU relies on, such as every grown candidate being
// evaluated by exactly one ring.
namespace PlutoGE::render
{
    namespace
    {
        // Candidates one field may dispatch per frame; denser or wider settings spread the lattice to stay under it.
        constexpr std::uint64_t MaxCandidates = 1u << 21;
        // The height a clump's lean is authored for (metres): leans are horizontal offsets in the clump mesh.
        constexpr float ReferenceHeight = 0.5f;
        // Blades each LOD keeps, and their segments.
        constexpr std::array<float, DetailGrassLodCount> LodBladeShare{1.0f, 0.6f, 0.35f};
        constexpr std::array<std::uint32_t, DetailGrassLodCount> LodSegments{4u, 2u, 1u};

        float Saturate(float value) { return std::clamp(value, 0.0f, 1.0f); }
        float Finite(float value, float fallback) { return std::isfinite(value) ? value : fallback; }
        float Clamp(float value, float low, float high, float fallback) { return std::clamp(Finite(value, fallback), low, high); }

        // A small deterministic generator for clump shapes (not the lattice hash: shapes need many values per blade).
        struct ShapeRandom
        {
            std::uint32_t state;
            float Next()
            {
                state = state * 747796405u + 2891336453u;
                std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
                word = (word >> 22u) ^ word;
                return float(word >> 8u) * (1.0f / 16777216.0f);
            }
        };
    }

    DetailGrassClumpShape DetailGrassClumpShape::Sanitized() const
    {
        DetailGrassClumpShape result = *this;
        result.blades = std::clamp(blades, 1u, 64u);
        result.radius = Clamp(radius, 0.0f, 1.0f, 0.12f);
        result.bladeWidth = Clamp(bladeWidth, 0.001f, 0.2f, 0.014f);
        result.minBladeHeight = Clamp(minBladeHeight, 0.05f, 1.0f, 0.45f);
        result.lean = Clamp(lean, 0.0f, 1.0f, 0.28f);
        result.curvature = Clamp(curvature, 0.0f, 1.0f, 0.35f);
        return result;
    }

    DetailGrassClumpMesh BuildDetailGrassClump(const DetailGrassClumpShape &source)
    {
        const auto shape = source.Sanitized();
        struct Blade
        {
            glm::vec2 root{0.0f}, across{1.0f, 0.0f}, lean{0.0f};
            float height = 1.0f;
        };
        // Every LOD draws a prefix of the same blades, so a LOD switch thins the clump rather than reshaping it.
        std::vector<Blade> blades(shape.blades);
        ShapeRandom random{shape.seed * 0x9e3779b9u + 0x632be5abu};
        for (auto &blade : blades)
        {
            const float rootAngle = random.Next() * glm::two_pi<float>();
            const glm::vec2 outward(std::cos(rootAngle), std::sin(rootAngle));
            blade.root = outward * (shape.radius * std::sqrt(random.Next()));
            const float facing = random.Next() * glm::two_pi<float>();
            blade.across = {std::cos(facing), std::sin(facing)};
            blade.height = shape.minBladeHeight + (1.0f - shape.minBladeHeight) * random.Next();
            const float wander = random.Next() * glm::two_pi<float>();
            glm::vec2 direction = outward + 0.6f * glm::vec2(std::cos(wander), std::sin(wander));
            direction = glm::length(direction) > 1.0e-4f ? glm::normalize(direction) : outward;
            blade.lean = direction * (shape.lean * blade.height * ReferenceHeight * (0.5f + random.Next()));
        }

        DetailGrassClumpMesh mesh;
        const float exponent = 1.0f + 2.0f * shape.curvature;
        for (std::uint32_t lod = 0; lod < DetailGrassLodCount; ++lod)
        {
            const auto count = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::lround(float(shape.blades) * LodBladeShare[lod])));
            const auto segments = LodSegments[lod];
            // Fewer blades are wider, so the clump keeps roughly its coverage.
            const float widthScale = std::min(2.0f, 1.0f / std::sqrt(float(count) / float(shape.blades)));
            mesh.lods[lod].firstIndex = static_cast<std::uint32_t>(mesh.indices.size());
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const auto &blade = blades[index];
                const glm::vec3 across(blade.across.x, 0.0f, blade.across.y);
                // The blade's spine at t (0 root .. 1 tip), in mesh units, and its direction there.
                const auto spine = [&](float t)
                {
                    const glm::vec2 offset = blade.root + blade.lean * std::pow(t, exponent);
                    return glm::vec3(offset.x, blade.height * t, offset.y);
                };
                const auto spineDirection = [&](float t)
                {
                    const glm::vec2 slope = blade.lean * (exponent * std::pow(std::max(t, 1.0e-3f), exponent - 1.0f));
                    return glm::normalize(glm::vec3(slope.x, blade.height, slope.y));
                };
                const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
                for (std::uint32_t row = 0; row < segments; ++row)
                {
                    const float t = float(row) / float(segments);
                    const glm::vec3 centre = spine(t);
                    const float halfWidth = 0.5f * shape.bladeWidth * widthScale * std::pow(1.0f - t, 0.8f);
                    const glm::vec3 normal = glm::normalize(glm::cross(across, spineDirection(t)));
                    // Normals tilt across the blade so it shades as a rounded leaf rather than a flat card.
                    for (const float side : {-1.0f, 1.0f})
                    {
                        DetailGrassClumpVertex vertex;
                        vertex.position = centre + across * (side * halfWidth);
                        vertex.normal = glm::normalize(normal + across * (0.4f * side));
                        vertex.uv = {side < 0.0f ? 0.0f : 1.0f, t};
                        vertex.tangent = glm::vec4(across, 1.0f);
                        mesh.vertices.push_back(vertex);
                    }
                }
                DetailGrassClumpVertex tip;
                tip.position = spine(1.0f);
                tip.normal = glm::normalize(glm::cross(across, spineDirection(1.0f)));
                tip.uv = {0.5f, 1.0f};
                tip.tangent = glm::vec4(across, 1.0f);
                mesh.vertices.push_back(tip);
                for (std::uint32_t row = 0; row + 1 < segments; ++row)
                {
                    const std::uint32_t a = first + row * 2;
                    mesh.indices.insert(mesh.indices.end(), {a, a + 1, a + 3, a, a + 3, a + 2});
                }
                const std::uint32_t last = first + (segments - 1) * 2;
                mesh.indices.insert(mesh.indices.end(), {last, last + 1, last + 2});
            }
            mesh.lods[lod].indexCount = static_cast<std::uint32_t>(mesh.indices.size()) - mesh.lods[lod].firstIndex;
        }
        for (const auto &vertex : mesh.vertices)
            mesh.radius = std::max(mesh.radius, glm::length(glm::vec2(vertex.position.x, vertex.position.z)));
        return mesh;
    }

    DetailGrassLayer DetailGrassLayer::Sanitized() const
    {
        DetailGrassLayer result = *this;
        result.density = Clamp(density, 0.1f, 64.0f, 14.0f);
        result.weightThreshold = Clamp(weightThreshold, 0.0f, 0.95f, 0.2f);
        result.maxSlopeDegrees = Clamp(maxSlopeDegrees, 0.0f, 90.0f, 45.0f);
        result.minAltitude = Clamp(minAltitude, -10000.0f, 10000.0f, -10000.0f);
        result.maxAltitude = Clamp(maxAltitude, result.minAltitude, 10000.0f, 10000.0f);
        result.altitudeFade = Clamp(altitudeFade, 0.01f, 500.0f, 4.0f);
        result.clustering = Clamp(clustering, 0.0f, 1.0f, 0.0f);
        result.minHeight = Clamp(minHeight, 0.01f, 5.0f, 0.35f);
        result.maxHeight = Clamp(maxHeight, result.minHeight, 5.0f, result.minHeight);
        result.minWidth = Clamp(minWidth, 0.1f, 5.0f, 0.85f);
        result.maxWidth = Clamp(maxWidth, result.minWidth, 5.0f, result.minWidth);
        result.alignToGround = Clamp(alignToGround, 0.0f, 1.0f, 0.35f);
        result.fullDensityDistance = Clamp(fullDensityDistance, 1.0f, 60.0f, 12.0f);
        result.densityFalloff = Clamp(densityFalloff, 1.5f, 3.0f, 2.0f);
        result.drawDistance = Clamp(drawDistance, result.fullDensityDistance, 300.0f, 70.0f);
        result.maxWidthCompensation = Clamp(maxWidthCompensation, 1.0f, 4.0f, 2.5f);
        result.lod1Distance = Clamp(lod1Distance, 0.0f, result.drawDistance, 14.0f);
        result.lod2Distance = Clamp(lod2Distance, result.lod1Distance, result.drawDistance, 32.0f);
        result.color = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
        result.dryColor = glm::clamp(dryColor, glm::vec3(0.0f), glm::vec3(1.0f));
        result.dryAmount = Clamp(dryAmount, 0.0f, 1.0f, 0.25f);
        result.patchSize = Clamp(patchSize, 0.5f, 200.0f, 9.0f);
        result.colorJitter = Clamp(colorJitter, 0.0f, 1.0f, 0.12f);
        result.groundTint = Clamp(groundTint, 0.0f, 1.0f, 0.6f);
        result.rootOcclusion = Clamp(rootOcclusion, 0.0f, 1.0f, 0.3f);
        result.normalBend = Clamp(normalBend, 0.0f, 1.0f, 0.6f);
        result.roughness = Clamp(roughness, 0.04f, 1.0f, 0.6f);
        result.translucency = Clamp(translucency, 0.0f, 1.0f, 0.45f);
        result.wind = wind.Sanitized();
        result.shape = shape.Sanitized();
        return result;
    }

    std::uint32_t DetailGrassHash(std::int32_t x, std::int32_t z, std::uint32_t seed, std::uint32_t salt)
    {
        std::uint32_t hash = static_cast<std::uint32_t>(x) * 0x8da6b343u ^ static_cast<std::uint32_t>(z) * 0xd8163841u ^
                             seed * 0xcb1ab31fu ^ salt * 0x165667b1u;
        hash ^= hash >> 16u;
        hash *= 0x7feb352du;
        hash ^= hash >> 15u;
        hash *= 0x846ca68bu;
        hash ^= hash >> 16u;
        return hash;
    }

    float DetailGrassUnit(std::uint32_t hash)
    {
        return float(hash >> 8u) * (1.0f / 16777216.0f);
    }

    DetailGrassLattice MakeDetailGrassLattice(const DetailGrassLayer &source)
    {
        const auto layer = source.Sanitized();
        DetailGrassLattice lattice;
        lattice.fullDensityDistance = layer.fullDensityDistance;
        lattice.densityFalloff = layer.densityFalloff;
        lattice.drawDistance = layer.drawDistance;
        // Ring k ends where Keep falls to 4^-(k+1): at full * 4^((k+1) / falloff).
        const float quarterings = layer.drawDistance > layer.fullDensityDistance
            ? layer.densityFalloff * std::log2(layer.drawDistance / layer.fullDensityDistance) * 0.5f : 0.0f;
        lattice.rings = std::clamp<std::uint32_t>(static_cast<std::uint32_t>(std::ceil(quarterings - 1.0e-4f)), 1u, DetailGrassMaxRings);
        for (std::uint32_t ring = 0; ring < lattice.rings; ++ring)
            lattice.ringRadius[ring] = ring + 1 == lattice.rings ? layer.drawDistance
                : std::min(layer.drawDistance, layer.fullDensityDistance *
                                                   std::pow(4.0f, float(ring + 1) / layer.densityFalloff));
        const auto layout = [&](float spacing)
        {
            lattice.spacing = spacing;
            // The top level's jitter reaches half its own spacing; one more stride covers the snapped origin.
            lattice.margin = 0.5f * float(1u << lattice.TopLevel()) * spacing;
            std::uint64_t total = 0;
            for (std::uint32_t ring = 0; ring < lattice.rings; ++ring)
            {
                const float stride = spacing * float(1u << ring);
                const auto half = static_cast<std::uint32_t>(std::ceil((lattice.ringRadius[ring] + lattice.margin) / stride));
                lattice.ringSide[ring] = 2u * half + 2u;
                total += std::uint64_t(lattice.ringSide[ring]) * lattice.ringSide[ring];
            }
            return total;
        };
        float spacing = 1.0f / std::sqrt(layer.density);
        if (const auto total = layout(spacing); total > MaxCandidates)
            layout(spacing * std::sqrt(float(total) / float(MaxCandidates)) * 1.01f);
        return lattice;
    }

    float DetailGrassLattice::Keep(float distance) const
    {
        if (!(distance > fullDensityDistance)) return 1.0f;
        return std::pow(fullDensityDistance / distance, densityFalloff);
    }

    std::uint32_t DetailGrassLattice::RingOf(float keep) const
    {
        if (!(keep > 0.0f)) return TopLevel();
        const float quarterings = std::log2(1.0f / std::min(keep, 1.0f)) * 0.5f;
        return std::min(TopLevel(), static_cast<std::uint32_t>(std::floor(quarterings)));
    }

    std::uint32_t DetailGrassLattice::Level(std::int32_t x, std::int32_t z) const
    {
        const auto bits = static_cast<std::uint32_t>(x) | static_cast<std::uint32_t>(z);
        if (bits == 0u) return TopLevel();
        return std::min(TopLevel(), static_cast<std::uint32_t>(std::countr_zero(bits)));
    }

    float DetailGrassLattice::Rank(std::int32_t x, std::int32_t z, std::uint32_t seed) const
    {
        const float unit = DetailGrassUnit(DetailGrassHash(x, z, seed, PLUTO_DETAIL_GRASS_SALT_RANK));
        const auto level = Level(x, z);
        const float levelShare = std::ldexp(1.0f, -2 * static_cast<int>(level)); // 4^-level
        if (level == TopLevel()) return unit * levelShare;
        return 0.25f * levelShare * (1.0f + 3.0f * unit);
    }

    glm::vec2 DetailGrassLattice::Position(std::int32_t x, std::int32_t z, std::uint32_t seed) const
    {
        const float reach = float(1u << Level(x, z)) * spacing;
        const glm::vec2 jitter(DetailGrassUnit(DetailGrassHash(x, z, seed, PLUTO_DETAIL_GRASS_SALT_JITTER_X)) - 0.5f,
                               DetailGrassUnit(DetailGrassHash(x, z, seed, PLUTO_DETAIL_GRASS_SALT_JITTER_Z)) - 0.5f);
        return glm::vec2(float(x), float(z)) * spacing + jitter * reach;
    }

    float DetailGrassLattice::Fade(float keep, float rank)
    {
        return Saturate((keep - rank) / (float(PLUTO_DETAIL_GRASS_RANK_FADE) * std::max(rank, 1.0e-6f)));
    }

    glm::ivec2 DetailGrassLattice::RingOrigin(std::uint32_t ring, glm::vec2 camera) const
    {
        const float stride = spacing * float(1u << ring);
        const glm::ivec2 cell(static_cast<int>(std::floor(camera.x / stride)), static_cast<int>(std::floor(camera.y / stride)));
        return cell - glm::ivec2(static_cast<int>(ringSide[ring] / 2u));
    }

    bool DetailGrassLattice::Grows(std::int32_t x, std::int32_t z, std::uint32_t seed, float distance, float density) const
    {
        if (!(distance < drawDistance)) return false;
        return Rank(x, z, seed) < Keep(distance) * Saturate(density);
    }

    std::uint32_t DetailGrassLod(const DetailGrassLayer &layer, float distance)
    {
        return distance < layer.lod1Distance ? 0u : distance < layer.lod2Distance ? 1u : 2u;
    }

    namespace
    {
        struct PresetInfo
        {
            const char *name;
            const char *description;
        };
        constexpr std::array<PresetInfo, std::size_t(DetailGrassPreset::Count)> Presets{{
            {"Meadow", "Dense, knee-high mixed grass with dry patches."},
            {"Lawn", "Short, very dense and even, like mown or grazed grass."},
            {"Tall Grass", "Sparse, waist-high soft grass that sways strongly."},
            {"Dry Tufts", "Scattered straw-coloured tufts for forest floors and dry ground."},
            {"Weeds", "A few low, broad-leaved plants among other cover."},
        }};

        const PresetInfo &Info(DetailGrassPreset preset)
        {
            return Presets[std::min(std::size_t(preset), Presets.size() - 1)];
        }

        // The settings a preset owns; the rest of the layer is left as constructed.
        struct PresetValues
        {
            float density;
            glm::vec2 height, width;
            float alignToGround;
            float fullDensityDistance, drawDistance, widthCompensation;
            glm::vec2 lodDistances;
            glm::vec3 color, dryColor;
            float dryAmount, patchSize, colorJitter;
            float groundTint, rootOcclusion, normalBend, roughness, translucency;
            float windStiffness, windFlutter;
            DetailGrassClumpShape shape;
        };

        PresetValues Values(DetailGrassPreset preset)
        {
            switch (preset)
            {
            case DetailGrassPreset::Lawn:
                return {40.0f, {0.06f, 0.14f}, {0.9f, 1.2f}, 0.6f, 10.0f, 60.0f, 3.0f, {10.0f, 24.0f},
                        {0.20f, 0.38f, 0.08f}, {0.42f, 0.42f, 0.16f}, 0.12f, 6.0f, 0.12f,
                        0.35f, 0.4f, 0.75f, 0.55f, 0.4f, 0.3f, 0.2f,
                        {.blades = 24, .radius = 0.12f, .bladeWidth = 0.006f, .minBladeHeight = 0.5f, .lean = 0.2f, .curvature = 0.3f}};
            case DetailGrassPreset::TallGrass:
                return {10.0f, {0.7f, 1.4f}, {0.9f, 1.4f}, 0.15f, 16.0f, 90.0f, 2.0f, {16.0f, 40.0f},
                        {0.28f, 0.36f, 0.10f}, {0.60f, 0.50f, 0.25f}, 0.45f, 14.0f, 0.2f,
                        0.4f, 0.25f, 0.55f, 0.6f, 0.5f, 0.05f, 0.4f,
                        {.blades = 16, .radius = 0.18f, .bladeWidth = 0.02f, .minBladeHeight = 0.4f, .lean = 0.35f, .curvature = 0.7f}};
            case DetailGrassPreset::DryTufts:
                return {4.0f, {0.25f, 0.55f}, {0.8f, 1.2f}, 0.4f, 10.0f, 55.0f, 2.0f, {12.0f, 28.0f},
                        {0.30f, 0.31f, 0.12f}, {0.52f, 0.42f, 0.20f}, 0.65f, 6.0f, 0.18f,
                        0.65f, 0.35f, 0.55f, 0.7f, 0.35f, 0.25f, 0.3f,
                        {.blades = 10, .radius = 0.1f, .bladeWidth = 0.013f, .minBladeHeight = 0.45f, .lean = 0.28f, .curvature = 0.35f}};
            case DetailGrassPreset::Weeds:
                return {3.0f, {0.15f, 0.4f}, {0.8f, 1.3f}, 0.5f, 10.0f, 45.0f, 1.5f, {10.0f, 24.0f},
                        {0.18f, 0.30f, 0.08f}, {0.35f, 0.33f, 0.12f}, 0.2f, 8.0f, 0.25f,
                        0.3f, 0.4f, 0.4f, 0.5f, 0.5f, 0.4f, 0.4f,
                        {.blades = 8, .radius = 0.08f, .bladeWidth = 0.03f, .minBladeHeight = 0.5f, .lean = 0.55f, .curvature = 0.3f}};
            case DetailGrassPreset::Meadow:
            case DetailGrassPreset::Count:
                break;
            }
            return {22.0f, {0.22f, 0.8f}, {0.8f, 1.5f}, 0.3f, 14.0f, 85.0f, 2.5f, {14.0f, 34.0f},
                    {0.26f, 0.37f, 0.09f}, {0.55f, 0.47f, 0.22f}, 0.35f, 11.0f, 0.22f,
                    0.45f, 0.3f, 0.6f, 0.6f, 0.45f, 0.15f, 0.35f,
                    {.blades = 20, .radius = 0.2f, .bladeWidth = 0.017f, .minBladeHeight = 0.35f, .lean = 0.45f, .curvature = 0.6f}};
        }
    }

    const char *DetailGrassPresetName(DetailGrassPreset preset) { return Info(preset).name; }
    const char *DetailGrassPresetDescription(DetailGrassPreset preset) { return Info(preset).description; }

    DetailGrassLayer DetailGrassApplyPreset(const DetailGrassLayer &source, DetailGrassPreset preset)
    {
        const auto values = Values(preset);
        DetailGrassLayer layer = source;
        layer.density = values.density;
        layer.minHeight = values.height.x, layer.maxHeight = values.height.y;
        layer.minWidth = values.width.x, layer.maxWidth = values.width.y;
        layer.alignToGround = values.alignToGround;
        layer.fullDensityDistance = values.fullDensityDistance;
        layer.drawDistance = values.drawDistance;
        layer.maxWidthCompensation = values.widthCompensation;
        layer.lod1Distance = values.lodDistances.x, layer.lod2Distance = values.lodDistances.y;
        layer.color = values.color, layer.dryColor = values.dryColor;
        layer.dryAmount = values.dryAmount, layer.patchSize = values.patchSize, layer.colorJitter = values.colorJitter;
        layer.groundTint = values.groundTint, layer.rootOcclusion = values.rootOcclusion, layer.normalBend = values.normalBend;
        layer.roughness = values.roughness, layer.translucency = values.translucency;
        layer.wind.enabled = true;
        layer.wind.stiffness = values.windStiffness, layer.wind.flutter = values.windFlutter;
        const auto shapeSeed = layer.shape.seed;
        layer.shape = values.shape;
        layer.shape.seed = shapeSeed;
        return layer.Sanitized();
    }

    DetailGrassLayer DetailGrassPresetLayer(DetailGrassPreset preset)
    {
        DetailGrassLayer layer;
        layer.name = DetailGrassPresetName(preset);
        return DetailGrassApplyPreset(layer, preset);
    }

    float DetailGrassEstimate::Clumps() const noexcept
    {
        float total = 0.0f;
        for (const float count : clumps) total += count;
        return total;
    }

    float DetailGrassEstimate::Triangles() const noexcept
    {
        float total = 0.0f;
        for (std::uint32_t lod = 0; lod < DetailGrassLodCount; ++lod) total += clumps[lod] * float(trianglesPerClump[lod]);
        return total;
    }

    DetailGrassEstimate EstimateDetailGrass(const DetailGrassLayer &source, const DetailGrassClumpMesh *clump)
    {
        const auto layer = source.Sanitized();
        const auto lattice = MakeDetailGrassLattice(layer);
        DetailGrassEstimate estimate;
        estimate.clumpsPerSquareMetre = 1.0f / (lattice.spacing * lattice.spacing);
        for (std::uint32_t ring = 0; ring < lattice.rings; ++ring)
            estimate.candidates += lattice.ringSide[ring] * lattice.ringSide[ring];

        const auto built = clump ? DetailGrassClumpMesh{} : BuildDetailGrassClump(layer.shape);
        const auto &lods = clump ? clump->lods : built.lods;
        for (std::uint32_t lod = 0; lod < DetailGrassLodCount; ++lod) estimate.trianglesPerClump[lod] = lods[lod].indexCount / 3u;

        // Expected clumps: the density times the kept share over the disc out to the draw distance, by annulus.
        constexpr float step = 0.05f;
        for (float inner = 0.0f; inner < layer.drawDistance; inner += step)
        {
            const float outer = std::min(inner + step, layer.drawDistance);
            const float middle = 0.5f * (inner + outer);
            const float area = glm::pi<float>() * (outer * outer - inner * inner);
            estimate.clumps[DetailGrassLod(layer, middle)] += area * estimate.clumpsPerSquareMetre * lattice.Keep(middle);
        }
        return estimate;
    }
}
