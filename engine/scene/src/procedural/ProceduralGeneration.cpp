#include "PlutoGE/scene/procedural/ProceduralGeneration.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>
#include <unordered_map>

namespace PlutoGE::scene::procedural
{
    namespace
    {
        // The bit FoliageComponent reserves for generated instances (ProceduralFoliageInstanceIdBit).
        constexpr std::uint64_t ProceduralIdBit = 1ull << 63;

        void WarnOnce(std::vector<std::string> &warnings, std::string message)
        {
            if (std::find(warnings.begin(), warnings.end(), message) == warnings.end())
                warnings.push_back(std::move(message));
        }

        // A box blur of one channel of a row-major grid, in place, along x (stride 1) or z (stride width).
        void BoxBlur(std::vector<float> &grid, int width, int height, std::size_t channels, std::size_t channel, int radius, bool alongX)
        {
            if (radius <= 0)
                return;
            const int lines = alongX ? height : width;
            const int length = alongX ? width : height;
            std::vector<float> line(static_cast<std::size_t>(length));
            for (int lineIndex = 0; lineIndex < lines; ++lineIndex)
            {
                const auto at = [&](int i) -> float & {
                    const int x = alongX ? i : lineIndex;
                    const int z = alongX ? lineIndex : i;
                    return grid[(static_cast<std::size_t>(z) * width + x) * channels + channel];
                };
                for (int i = 0; i < length; ++i)
                    line[static_cast<std::size_t>(i)] = at(i);
                float sum = 0.0f;
                for (int i = -radius; i <= radius; ++i)
                    sum += line[static_cast<std::size_t>(std::clamp(i, 0, length - 1))];
                for (int i = 0; i < length; ++i)
                {
                    at(i) = sum / static_cast<float>(2 * radius + 1);
                    sum += line[static_cast<std::size_t>(std::clamp(i + radius + 1, 0, length - 1))] -
                           line[static_cast<std::size_t>(std::clamp(i - radius, 0, length - 1))];
                }
            }
        }

        float BiomeRawWeight(const ProceduralBiome &biome, const TerrainPoint &point, const ProceduralInputs &inputs)
        {
            if (!biome.enabled)
                return 0.0f;
            const float mask = biome.mask.empty() ? 1.0f : inputs.Mask(biome.mask, point.world);
            return mask > 0.0f ? mask * biome.weight.Evaluate(point, inputs) : 0.0f;
        }

        // Foliage instances are rotated Y * X * Z; this tilts the instance's up axis onto `up` under the given yaw (the
        // convention FoliageComponent's brush uses).
        glm::vec3 RotationFor(glm::vec3 up, float yawDegrees)
        {
            const float yaw = glm::radians(-yawDegrees);
            const glm::vec3 unyawed(up.x * std::cos(yaw) + up.z * std::sin(yaw), up.y, -up.x * std::sin(yaw) + up.z * std::cos(yaw));
            return {glm::degrees(std::atan2(unyawed.z, unyawed.y)), yawDegrees, -glm::degrees(std::asin(std::clamp(unyawed.x, -1.0f, 1.0f)))};
        }

        void CheckInputs(const TerrainRuleStack &stack, const ProceduralInputs &inputs, std::vector<std::string> &warnings)
        {
            for (const auto &rule : stack.rules)
            {
                if (!rule.enabled)
                    continue;
                if (rule.input == RuleInput::Mask && !inputs.HasMask(rule.source))
                    WarnOnce(warnings, "No mask named \"" + rule.source + "\": the rule reads 0.");
                if (rule.input == RuleInput::Distance && !inputs.Distances().Has(rule.source))
                    WarnOnce(warnings, "No distance source tagged \"" + rule.source + "\": the rule reads as far away.");
                if (rule.input == RuleInput::WaterDepth && !inputs.HasWater())
                    WarnOnce(warnings, "A Water Depth rule found no water in the scene: everywhere reads as dry land.");
            }
        }
    }

    void ParallelFor(int count, const std::function<void(int)> &body)
    {
        if (count <= 0)
            return;
        const int workers = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, std::min(count, 32));
        if (workers == 1 || count < 4)
        {
            for (int index = 0; index < count; ++index)
                body(index);
            return;
        }
        std::atomic<int> next{0};
        const auto work = [&] {
            for (int index = next++; index < count; index = next++)
                body(index);
        };
        std::vector<std::thread> threads;
        threads.reserve(static_cast<std::size_t>(workers - 1));
        for (int worker = 1; worker < workers; ++worker)
            threads.emplace_back(work);
        work();
        for (auto &thread : threads)
            thread.join();
    }

    float BiomeWeights::Sample(std::size_t biome, glm::vec2 uv) const
    {
        if (m_resolution <= 0 || biome >= m_biomes)
            return 0.0f;
        const glm::vec2 texel = glm::clamp(uv, 0.0f, 1.0f) * static_cast<float>(m_resolution) - 0.5f;
        const glm::vec2 base = glm::floor(texel);
        const glm::vec2 t = texel - base;
        const auto at = [&](int x, int z) {
            return At(biome, std::clamp(x, 0, m_resolution - 1), std::clamp(z, 0, m_resolution - 1));
        };
        const int x = static_cast<int>(base.x), z = static_cast<int>(base.y);
        const float lower = at(x, z) + (at(x + 1, z) - at(x, z)) * t.x;
        const float upper = at(x, z + 1) + (at(x + 1, z + 1) - at(x, z + 1)) * t.x;
        return lower + (upper - lower) * t.y;
    }

    std::size_t ProceduralGenerationResult::InstanceCount() const
    {
        std::size_t count = 0;
        for (const auto &type : instances)
            count += type.size();
        return count;
    }

    BiomeWeights EvaluateBiomeWeights(const ProceduralTerrainDefinition &definition, const TerrainSurface &surface,
                                      const ProceduralInputs &inputs, int resolution)
    {
        resolution = std::clamp(resolution, 4, 4096);
        const std::size_t count = definition.biomes.size();
        BiomeWeights result(resolution, count);
        if (count == 0 || !surface.Valid())
            return result;

        const glm::vec2 extent = surface.Extent();
        const float texelSize = std::max(extent.x, extent.y) / static_cast<float>(resolution);
        // Two passes of a box this wide approximate a smooth blend over the blend distance.
        const int radius = std::clamp(static_cast<int>(std::lround(definition.biomeBlend / std::max(texelSize, 1e-3f) * 0.25f)), 0, 64);
        // Evaluated beyond the terrain's edge by the blur's reach, so tiles blend the same way across their seams.
        const int pad = 2 * radius;
        const int padded = resolution + 2 * pad;
        std::vector<float> grid(static_cast<std::size_t>(padded) * padded * count, 0.0f);
        ParallelFor(padded, [&](int pz) {
            for (int px = 0; px < padded; ++px)
            {
                const glm::vec2 uv((static_cast<float>(px - pad) + 0.5f) / resolution, (static_cast<float>(pz - pad) + 0.5f) / resolution);
                const auto point = surface.PointAt(uv * extent);
                for (std::size_t biome = 0; biome < count; ++biome)
                    grid[(static_cast<std::size_t>(pz) * padded + px) * count + biome] = BiomeRawWeight(definition.biomes[biome], point, inputs);
            }
        });
        if (radius > 0)
            ParallelFor(static_cast<int>(count), [&](int biome) {
                for (int pass = 0; pass < 2; ++pass)
                {
                    BoxBlur(grid, padded, padded, count, static_cast<std::size_t>(biome), radius, true);
                    BoxBlur(grid, padded, padded, count, static_cast<std::size_t>(biome), radius, false);
                }
            });
        for (int z = 0; z < resolution; ++z)
            for (int x = 0; x < resolution; ++x)
            {
                const auto *source = &grid[(static_cast<std::size_t>(z + pad) * padded + (x + pad)) * count];
                float sum = 0.0f;
                for (std::size_t biome = 0; biome < count; ++biome)
                    sum += source[biome];
                for (std::size_t biome = 0; biome < count; ++biome)
                    result.At(biome, x, z) = sum > 1e-6f ? source[biome] / std::max(sum, 1.0f) : 0.0f;
            }
        // Weights above sum to at most 1: where biomes overlap they share the texel, and where only a faint biome
        // reaches (a blurred border into nothing) it keeps its faint weight rather than becoming full strength.
        return result;
    }

    TerrainWeightMap BakeLayerWeights(const ProceduralTerrainDefinition &definition, const BiomeWeights &biomes,
                                      const TerrainSurface &surface, const ProceduralInputs &inputs,
                                      const std::vector<std::string> &layerNames, std::vector<std::string> &warnings)
    {
        const int resolution = std::max(biomes.Resolution(), 1);
        constexpr std::size_t MaxLayers = TerrainWeightMap::kLayers;

        // Each biome's layer rules resolved to terrain layer indices (bottom to top); unknown layers are skipped.
        struct ResolvedLayer
        {
            std::size_t index;
            const TerrainRuleStack *coverage;
        };
        std::vector<std::vector<ResolvedLayer>> resolved(definition.biomes.size());
        for (std::size_t biome = 0; biome < definition.biomes.size(); ++biome)
            for (const auto &layer : definition.biomes[biome].layers)
            {
                const auto found = std::find(layerNames.begin(), layerNames.end(), layer.layer);
                if (found == layerNames.end() || static_cast<std::size_t>(found - layerNames.begin()) >= MaxLayers)
                {
                    WarnOnce(warnings, "Biome \"" + definition.biomes[biome].name + "\" paints \"" + layer.layer +
                                           "\", which is not one of the terrain's material layers.");
                    continue;
                }
                CheckInputs(layer.coverage, inputs, warnings);
                resolved[biome].push_back({static_cast<std::size_t>(found - layerNames.begin()), &layer.coverage});
            }

        const glm::vec2 extent = surface.Extent();
        std::array<std::vector<std::uint8_t>, TerrainWeightMap::kPlanes> planes;
        for (auto &plane : planes)
            plane.assign(static_cast<std::size_t>(resolution) * resolution * 4, 0);

        ParallelFor(resolution, [&](int z) {
            for (int x = 0; x < resolution; ++x)
            {
                const glm::vec2 uv((static_cast<float>(x) + 0.5f) / resolution, (static_cast<float>(z) + 0.5f) / resolution);
                std::array<float, MaxLayers> weights{};
                float painted = 0.0f;
                bool havePoint = false;
                TerrainPoint point;
                for (std::size_t biome = 0; biome < resolved.size(); ++biome)
                {
                    const float share = biomes.At(biome, x, z);
                    if (share <= 0.0f || resolved[biome].empty())
                        continue;
                    if (!havePoint)
                    {
                        point = surface.PointAt(uv * extent);
                        havePoint = true;
                    }
                    // Top to bottom: each layer takes its coverage of what the layers above left; the first gets the rest.
                    float remaining = 1.0f;
                    const auto &layers = resolved[biome];
                    for (std::size_t layer = layers.size(); layer-- > 1;)
                    {
                        const float taken = layers[layer].coverage->Evaluate(point, inputs) * remaining;
                        weights[layers[layer].index] += share * taken;
                        remaining -= taken;
                    }
                    weights[layers.front().index] += share * remaining;
                    painted += share;
                }
                // Whatever no biome paints (outside every biome, or a border fading into nothing) is layer 0, so
                // biomes fade into the base layer rather than ending at a line.
                if (painted < 1.0f)
                    weights[0] += 1.0f - painted;
                else
                    for (auto &weight : weights)
                        weight /= painted;

                // 8-bit weights that sum to exactly 255: round down, then hand the remainder to the largest fractions.
                std::array<int, MaxLayers> quantized{};
                std::array<float, MaxLayers> fractions{};
                int total = 0;
                for (std::size_t layer = 0; layer < MaxLayers; ++layer)
                {
                    const float scaled = std::clamp(weights[layer], 0.0f, 1.0f) * 255.0f;
                    quantized[layer] = static_cast<int>(scaled);
                    fractions[layer] = scaled - static_cast<float>(quantized[layer]);
                    total += quantized[layer];
                }
                while (total < 255)
                {
                    const auto largest = static_cast<std::size_t>(std::max_element(fractions.begin(), fractions.end()) - fractions.begin());
                    ++quantized[largest];
                    fractions[largest] = -1.0f;
                    ++total;
                }
                const std::size_t texel = (static_cast<std::size_t>(z) * resolution + x) * 4;
                for (std::size_t layer = 0; layer < MaxLayers; ++layer)
                    planes[layer / 4][texel + layer % 4] = static_cast<std::uint8_t>(std::min(quantized[layer], 255));
            }
        });

        TerrainWeightMap map(resolution);
        map.SetPlanes(resolution, planes);
        return map;
    }

    std::vector<std::vector<ScatterInstance>> Scatter(const ProceduralTerrainDefinition &definition, const BiomeWeights &biomes,
                                                      const TerrainSurface &surface, const ProceduralInputs &inputs,
                                                      const std::vector<std::string> &foliageTypes, std::vector<std::string> &warnings,
                                                      std::span<const WaterHabitat> habitats)
    {
        std::vector<std::vector<ScatterInstance>> result(foliageTypes.size());
        if (!surface.Valid())
            return result;

        struct Entry
        {
            std::size_t biome;
            std::size_t rule;
            std::size_t type;
        };
        std::vector<Entry> entries;
        for (std::size_t biome = 0; biome < definition.biomes.size(); ++biome)
        {
            if (!definition.biomes[biome].enabled)
                continue;
            const auto &rules = definition.biomes[biome].scatter;
            for (std::size_t rule = 0; rule < rules.size(); ++rule)
            {
                const auto found = std::find(foliageTypes.begin(), foliageTypes.end(), rules[rule].foliageType);
                if (found == foliageTypes.end())
                {
                    WarnOnce(warnings, "Biome \"" + definition.biomes[biome].name + "\" grows \"" + rules[rule].foliageType +
                                           "\", which is not one of the foliage component's types.");
                    continue;
                }
                CheckInputs(rules[rule].density, inputs, warnings);
                const auto type = static_cast<std::size_t>(found - foliageTypes.begin());
                if (type < habitats.size() && habitats[type].mode == WaterHabitatMode::Underwater && !inputs.HasWater())
                    WarnOnce(warnings, "\"" + rules[rule].foliageType + "\" grows only under water, and the scene has no water.");
                entries.push_back({biome, rule, type});
            }
        }
        // Larger plants first: they claim their ground, and smaller ones fill around them.
        std::stable_sort(entries.begin(), entries.end(), [&](const Entry &a, const Entry &b) {
            return definition.biomes[a.biome].scatter[a.rule].spacing > definition.biomes[b.biome].scatter[b.rule].spacing;
        });

        // Placed instances in a coarse spatial hash, for footprint exclusion.
        struct Placed
        {
            glm::vec2 local;
            float radius;
        };
        constexpr float ExclusionCell = 8.0f;
        const auto cellKey = [](int x, int z) { return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(z); };
        std::unordered_map<std::uint64_t, std::vector<Placed>> placed;
        float largestRadius = 0.0f;

        const glm::vec2 extent = surface.Extent();
        const glm::mat4 &toWorld = surface.LocalToWorld();
        const glm::mat4 toLocal = glm::inverse(toWorld);
        // The terrain's footprint in the world, to find the world grid cells over it.
        glm::vec2 worldLow(std::numeric_limits<float>::max()), worldHigh(std::numeric_limits<float>::lowest());
        for (const glm::vec2 corner : {glm::vec2(0.0f), glm::vec2(extent.x, 0.0f), glm::vec2(0.0f, extent.y), extent})
        {
            const glm::vec2 world = surface.LocalToWorldXZ(corner);
            worldLow = glm::min(worldLow, world);
            worldHigh = glm::max(worldHigh, world);
        }

        for (const auto &entry : entries)
        {
            const auto &biome = definition.biomes[entry.biome];
            const auto &rule = biome.scatter[entry.rule];
            const float spacing = std::max(rule.spacing, 0.25f);
            const float footprint = rule.footprint > 0.0f ? rule.footprint : spacing * 0.5f;
            largestRadius = std::max(largestRadius, footprint);
            const std::uint32_t ruleKey = Hash(definition.seed, HashName(biome.name), HashName(rule.foliageType),
                                               Hash(rule.seed, static_cast<std::uint32_t>(entry.rule)));
            const int x0 = static_cast<int>(std::floor(worldLow.x / spacing)), x1 = static_cast<int>(std::ceil(worldHigh.x / spacing));
            const int z0 = static_cast<int>(std::floor(worldLow.y / spacing)), z1 = static_cast<int>(std::ceil(worldHigh.y / spacing));
            auto &out = result[entry.type];
            // Why candidates in the biome failed, to explain a rule that places nothing.
            std::size_t inBiome = 0, placedHere = 0, byLimits = 0, byHabitat = 0, byDensity = 0;
            for (int gz = z0; gz <= z1; ++gz)
                for (int gx = x0; gx <= x1; ++gx)
                {
                    const auto cell = [&](std::uint32_t salt) { return Hash(ruleKey, static_cast<std::uint32_t>(gx), static_cast<std::uint32_t>(gz), salt); };
                    const glm::vec2 world((static_cast<float>(gx) + UnitFloat(cell(1))) * spacing, (static_cast<float>(gz) + UnitFloat(cell(2))) * spacing);
                    const glm::vec4 local4 = toLocal * glm::vec4(world.x, 0.0f, world.y, 1.0f);
                    const glm::vec2 local(local4.x, local4.z);
                    // Half-open on the far edges: a candidate on a shared tile edge belongs to one tile only.
                    if (local.x < 0.0f || local.y < 0.0f || local.x >= extent.x || local.y >= extent.y)
                        continue;
                    const glm::vec2 uv = local / extent;
                    float chance = biomes.Sample(entry.biome, uv) * rule.densityScale;
                    if (chance <= 0.0f || UnitFloat(cell(3)) >= chance)
                        continue;
                    ++inBiome;
                    const auto point = surface.PointAt(local);
                    if (point.slope < rule.slopeLimits.x || point.slope > rule.slopeLimits.y ||
                        point.height < rule.heightLimits.x || point.height > rule.heightLimits.y)
                    {
                        ++byLimits;
                        continue;
                    }
                    if (entry.type < habitats.size() && habitats[entry.type].NeedsWater() &&
                        !habitats[entry.type].Accepts(inputs.WaterDepth(point.world, point.height)))
                    {
                        ++byHabitat;
                        continue;
                    }
                    // The density stack last: it is the most expensive test.
                    chance *= rule.density.Evaluate(point, inputs);
                    if (UnitFloat(cell(3)) >= chance)
                    {
                        ++byDensity;
                        continue;
                    }

                    const int cx = static_cast<int>(std::floor(local.x / ExclusionCell));
                    const int cz = static_cast<int>(std::floor(local.y / ExclusionCell));
                    const int reach = static_cast<int>(std::ceil((footprint + largestRadius) / ExclusionCell));
                    bool blocked = false;
                    for (int z = cz - reach; z <= cz + reach && !blocked; ++z)
                        for (int x = cx - reach; x <= cx + reach && !blocked; ++x)
                            if (const auto found = placed.find(cellKey(x, z)); found != placed.end())
                                for (const auto &other : found->second)
                                    if (glm::distance(other.local, local) < other.radius + footprint)
                                    {
                                        blocked = true;
                                        break;
                                    }
                    if (blocked)
                        continue;
                    placed[cellKey(cx, cz)].push_back({local, footprint});

                    const float scale = rule.scaleRange.x + (rule.scaleRange.y - rule.scaleRange.x) * UnitFloat(cell(4));
                    const float yaw = rule.randomYaw ? UnitFloat(cell(5)) * 360.0f : 0.0f;
                    const glm::vec3 normal = surface.NormalAt(local);
                    const glm::vec3 up = glm::normalize(glm::mix(glm::vec3(0.0f, 1.0f, 0.0f), normal, std::clamp(rule.alignToSlope, 0.0f, 1.0f)));
                    const float height = surface.Height(local.x / surface.CellSize(), local.y / surface.CellSize());
                    ScatterInstance instance;
                    instance.id = ProceduralIdBit | (Hash64(ruleKey, static_cast<std::uint64_t>(static_cast<std::uint32_t>(gx)),
                                                            static_cast<std::uint64_t>(static_cast<std::uint32_t>(gz))) & ~ProceduralIdBit);
                    instance.position = {local.x, height - rule.sink * scale, local.y};
                    instance.rotationDegrees = RotationFor(up, yaw);
                    instance.scale = glm::vec3(scale);
                    out.push_back(instance);
                    ++placedHere;
                }
            // Candidates the biome offered, all turned down: say which test did it, so a rule that removes everything
            // (a Water Depth rule not inverted, a habitat at odds with the rules) is not silent.
            if (placedHere == 0 && inBiome >= 8)
            {
                const std::string name = "\"" + rule.foliageType + "\" in biome \"" + biome.name + "\"";
                if (byDensity >= byLimits && byDensity >= byHabitat)
                {
                    std::string hint;
                    if (rule.density.Uses(RuleInput::WaterDepth))
                        hint = inputs.HasWater() ? " (a Water Depth rule keeps only what it covers: Invert it to keep dry land)"
                                                 : " (its Water Depth rule found no water: everywhere reads as dry land)";
                    WarnOnce(warnings, name + " placed nothing: its density rules rejected all " + std::to_string(inBiome) +
                                           " candidates" + hint + ".");
                }
                else if (byHabitat >= byLimits)
                    WarnOnce(warnings, name + " placed nothing: the foliage type's Water habitat rejected " + std::to_string(byHabitat) +
                                           " of " + std::to_string(inBiome) + " candidates.");
                else
                    WarnOnce(warnings, name + " placed nothing: its slope and height limits rejected " + std::to_string(byLimits) +
                                           " of " + std::to_string(inBiome) + " candidates.");
            }
        }
        return result;
    }

    ProceduralGenerationResult Generate(const ProceduralGenerationInput &input)
    {
        ProceduralGenerationResult result;
        if (!input.definition || !input.surface || !input.inputs)
        {
            result.warnings.push_back("Nothing to generate from.");
            return result;
        }
        const auto &definition = *input.definition;
        for (const auto &biome : definition.biomes)
        {
            if (!biome.mask.empty() && !input.inputs->HasMask(biome.mask))
                WarnOnce(result.warnings, "Biome \"" + biome.name + "\" uses the mask \"" + biome.mask + "\", which did not load.");
            CheckInputs(biome.weight, *input.inputs, result.warnings);
        }
        const auto biomes = EvaluateBiomeWeights(definition, *input.surface, *input.inputs, input.weightResolution);
        const bool paints = std::any_of(definition.biomes.begin(), definition.biomes.end(),
                                        [](const ProceduralBiome &biome) { return !biome.layers.empty(); });
        if (input.texture && paints && !input.layerNames.empty())
            result.weights = BakeLayerWeights(definition, biomes, *input.surface, *input.inputs, input.layerNames, result.warnings);
        if (input.scatter)
            result.instances = Scatter(definition, biomes, *input.surface, *input.inputs, input.foliageTypes, result.warnings,
                                       input.foliageHabitats);
        return result;
    }
}
