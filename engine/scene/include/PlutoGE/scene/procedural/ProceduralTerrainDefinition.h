#pragma once

#include "PlutoGE/scene/procedural/TerrainRules.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene::procedural
{
    // Where a biome paints a terrain material layer: the layer (by the terrain's layer name) and its coverage, 0..1.
    struct ProceduralLayerRule
    {
        std::string layer;
        TerrainRuleStack coverage;
        bool operator==(const ProceduralLayerRule &) const = default;
    };

    // Where a biome grows one foliage type, and how.
    struct ProceduralScatterRule
    {
        std::string foliageType;         // the FoliageComponent type's name
        float spacing = 4.0f;            // metres between candidates (one per cell of a jittered grid)
        float footprint = 0.0f;          // radius kept clear of other instances; 0 uses half the spacing
        float densityScale = 1.0f;       // multiplies the density stack
        TerrainRuleStack density;        // 0..1 chance a candidate grows
        glm::vec2 slopeLimits{0.0f, 90.0f};
        glm::vec2 heightLimits{-1.0e6f, 1.0e6f};
        glm::vec2 scaleRange{0.8f, 1.2f};
        float alignToSlope = 0.0f;       // 0 grows upright, 1 along the surface normal
        float sink = 0.0f;               // metres below the surface, times the instance's scale
        bool randomYaw = true;
        std::uint32_t seed = 0;
        bool operator==(const ProceduralScatterRule &) const = default;
    };

    // A region of the world with its own look: where it is (a mask and/or rules), how it paints the terrain, and
    // what grows in it. Biomes overlap; their weights are normalized per point and softened at the borders.
    struct ProceduralBiome
    {
        std::string name = "Biome";
        bool enabled = true;
        std::string mask;        // a mask name; empty is everywhere
        TerrainRuleStack weight; // multiplies the mask
        // Bottom to top: later layers paint over earlier ones (each takes its coverage of what the ones above left),
        // and the first takes whatever remains.
        std::vector<ProceduralLayerRule> layers;
        std::vector<ProceduralScatterRule> scatter;
        bool operator==(const ProceduralBiome &) const = default;
    };

    // A mask image placed over the world. A zero size spans the terrain the definition is generated on.
    struct ProceduralMaskSource
    {
        std::string name;
        std::string image; // texture asset reference or path
        int channel = 0;   // 0 red .. 3 alpha
        glm::vec2 worldMin{0.0f};
        glm::vec2 worldSize{0.0f};
        bool operator==(const ProceduralMaskSource &) const = default;
    };

    // Everything a procedural terrain is generated from. Plain data with a versioned text encoding, so it can live in
    // a component property today and in its own asset later.
    struct ProceduralTerrainDefinition
    {
        static constexpr int Version = 1;
        std::uint32_t seed = 1;
        float biomeBlend = 8.0f; // metres over which neighbouring biomes blend
        std::vector<ProceduralMaskSource> masks;
        std::vector<ProceduralBiome> biomes;

        // Line records of space-separated key=value fields (values percent-escaped). Unknown keys and records are
        // ignored, so newer fields can be added without breaking older readers.
        [[nodiscard]] std::string Encode() const;
        [[nodiscard]] static std::optional<ProceduralTerrainDefinition> Decode(std::string_view text, std::string *error = nullptr);
        // Every terrain layer name the biomes paint, and every foliage type they grow (for validation).
        [[nodiscard]] std::vector<std::string> ReferencedLayers() const;
        [[nodiscard]] std::vector<std::string> ReferencedFoliageTypes() const;
        bool operator==(const ProceduralTerrainDefinition &) const = default;
    };
}
