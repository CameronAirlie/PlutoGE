#pragma once

#include "PlutoGE/scene/TerrainWeightMap.h"
#include "PlutoGE/scene/procedural/ProceduralInputs.h"
#include "PlutoGE/scene/procedural/ProceduralTerrainDefinition.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace PlutoGE::scene::procedural
{
    // Runs body(0..count-1) on worker threads. Deterministic as long as each call writes only its own outputs.
    void ParallelFor(int count, const std::function<void(int)> &body);

    // Each biome's share of every texel over a terrain (0..1, summing to 1 wherever any biome is present), blurred at
    // the borders by the definition's blend distance.
    class BiomeWeights
    {
    public:
        BiomeWeights() = default;
        BiomeWeights(int resolution, std::size_t biomes) : m_resolution(resolution), m_biomes(biomes),
              m_weights(static_cast<std::size_t>(resolution) * resolution * biomes, 0.0f) {}

        [[nodiscard]] int Resolution() const { return m_resolution; }
        [[nodiscard]] std::size_t Count() const { return m_biomes; }
        [[nodiscard]] float At(std::size_t biome, int x, int z) const
        {
            return m_weights[(static_cast<std::size_t>(z) * m_resolution + x) * m_biomes + biome];
        }
        float &At(std::size_t biome, int x, int z) { return m_weights[(static_cast<std::size_t>(z) * m_resolution + x) * m_biomes + biome]; }
        // Bilinear at a terrain UV (texel centres at (i + 0.5) / resolution, like the layer weight maps).
        [[nodiscard]] float Sample(std::size_t biome, glm::vec2 uv) const;

    private:
        int m_resolution = 0;
        std::size_t m_biomes = 0;
        std::vector<float> m_weights;
    };

    // One generated foliage instance, in the terrain's local space (where a FoliageComponent on the terrain's entity
    // keeps its instances). IDs have ProceduralFoliageInstanceIdBit set.
    struct ScatterInstance
    {
        std::uint64_t id = 0;
        glm::vec3 position{0.0f};
        glm::vec3 rotationDegrees{0.0f}; // the foliage convention: applied as Y * X * Z
        glm::vec3 scale{1.0f};
        bool operator==(const ScatterInstance &) const = default;
    };

    struct ProceduralGenerationInput
    {
        const ProceduralTerrainDefinition *definition = nullptr;
        const TerrainSurface *surface = nullptr;
        const ProceduralInputs *inputs = nullptr;
        std::vector<std::string> layerNames;   // the terrain's material layers, by index
        std::vector<std::string> foliageTypes; // the foliage component's types, by index (empty: no scatter)
        int weightResolution = 256;
        bool texture = true;
        bool scatter = true;
    };

    struct ProceduralGenerationResult
    {
        // Set when texturing ran and some biome paints a layer the terrain has.
        std::optional<TerrainWeightMap> weights;
        // Per foliage type index: the procedural instances only.
        std::vector<std::vector<ScatterInstance>> instances;
        std::vector<std::string> warnings;
        [[nodiscard]] std::size_t InstanceCount() const;
    };

    [[nodiscard]] BiomeWeights EvaluateBiomeWeights(const ProceduralTerrainDefinition &definition, const TerrainSurface &surface,
                                                    const ProceduralInputs &inputs, int resolution);
    // Layer weights from the biomes' layer rules. Painted layers the biomes do not name get no weight; texels no
    // biome paints go to layer 0.
    [[nodiscard]] TerrainWeightMap BakeLayerWeights(const ProceduralTerrainDefinition &definition, const BiomeWeights &biomes,
                                                    const TerrainSurface &surface, const ProceduralInputs &inputs,
                                                    const std::vector<std::string> &layerNames, std::vector<std::string> &warnings);
    // Foliage from the biomes' scatter rules. Candidates sit on a grid aligned to the world, so neighbouring terrain
    // tiles continue each other's pattern and an instance belongs to the one tile it stands on.
    [[nodiscard]] std::vector<std::vector<ScatterInstance>> Scatter(const ProceduralTerrainDefinition &definition, const BiomeWeights &biomes,
                                                                    const TerrainSurface &surface, const ProceduralInputs &inputs,
                                                                    const std::vector<std::string> &foliageTypes, std::vector<std::string> &warnings);
    // Everything: biome weights, then layer weights and scatter. Pure: safe to run on a worker thread.
    [[nodiscard]] ProceduralGenerationResult Generate(const ProceduralGenerationInput &input);
}
