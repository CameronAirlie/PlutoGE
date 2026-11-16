#pragma once

#include "PlutoGE/scene/procedural/ProceduralNoise.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene::procedural
{
    // What a rule can read at one point of a terrain.
    struct TerrainPoint
    {
        glm::vec2 world{0.0f};  // world x, z
        float height = 0.0f;    // world height of the surface
        float slope = 0.0f;     // degrees from horizontal
        float curvature = 0.0f; // Laplacian of the surface, 1/m: positive in hollows and valleys, negative on ridges
    };

    // Lookups beyond the point, by name: painted or imported masks, and distances to sources (rivers, roads, trails).
    class IRuleInputs
    {
    public:
        virtual ~IRuleInputs() = default;
        // 0..1; 0 for a mask that does not exist.
        [[nodiscard]] virtual float Mask(std::string_view name, glm::vec2 world) const = 0;
        // Distance in world units to the nearest source with this name, never more than `limit` (returned when none
        // is nearer).
        [[nodiscard]] virtual float Distance(std::string_view source, glm::vec2 world, float limit) const = 0;
        [[nodiscard]] virtual std::uint32_t Seed() const = 0;
        // Still water surface minus `groundHeight` at a world (x, z): positive under water, negative above it, nothing
        // where no water body covers the point (water::WaterSurfaceSnapshot::StillDepth). No water by default.
        [[nodiscard]] virtual std::optional<float> WaterDepth(glm::vec2 world, float groundHeight) const
        {
            (void)world, (void)groundHeight;
            return std::nullopt;
        }
    };

    // What the Water Depth input reads where no water covers a point: far above any water, so Band ranges over
    // depths (0.5..3 m) exclude dry land, and an inverted one keeps it.
    inline constexpr float NoWaterDepth = -1.0e6f;

    enum class RuleInput : std::uint8_t
    {
        Constant,
        Height,
        Slope,
        Curvature,
        Noise,
        Mask,
        Distance,
        WaterDepth, // metres of still water above the ground (negative above the water's level); NoWaterDepth on dry land
    };

    enum class RuleRemap : std::uint8_t
    {
        Band,   // 1 between Min and Max, easing to 0 over Falloff outside them (heights, slopes, distances)
        Linear, // Min..Max maps to 0..1 (noise and masks, which are 0..1 already)
    };

    enum class RuleBlend : std::uint8_t
    {
        Multiply, // a filter: keeps the stack where this rule is 1
        Add,
        Subtract,
        Min,
        Max,
        Replace,
    };

    // One filter of a rule stack: an input, remapped to 0..1, combined into the stack's running value.
    struct TerrainRule
    {
        bool enabled = true;
        RuleInput input = RuleInput::Constant;
        float constant = 1.0f; // the Constant input's value
        RuleRemap remap = RuleRemap::Band;
        float min = 0.0f;
        float max = 1.0f;
        float falloff = 0.0f;
        bool invert = false;
        NoiseSettings noise; // the Noise input
        std::string source;  // the Mask input's mask, or the Distance input's source
        RuleBlend blend = RuleBlend::Multiply;
        float opacity = 1.0f;

        // A rule of this input with defaults that mean something for it (slopes in degrees, a noise in 0..1).
        [[nodiscard]] static TerrainRule Make(RuleInput input);
        // The input at the point, remapped and inverted: 0..1.
        [[nodiscard]] float Evaluate(const TerrainPoint &point, const IRuleInputs &inputs) const;
        // The farthest distance the remap can tell apart (Distance inputs search no further).
        [[nodiscard]] float DistanceLimit() const;
        bool operator==(const TerrainRule &) const = default;
    };

    // An ordered list of rules giving one 0..1 value per point. The running value starts at 1 and each enabled rule
    // combines into it (clamped to 0..1); an empty stack is 1 everywhere. The evaluator is a flat list, so a node
    // graph front end can compile to it later.
    struct TerrainRuleStack
    {
        std::vector<TerrainRule> rules;

        [[nodiscard]] float Evaluate(const TerrainPoint &point, const IRuleInputs &inputs) const;
        [[nodiscard]] bool Uses(RuleInput input) const;
        bool operator==(const TerrainRuleStack &) const = default;
    };

    [[nodiscard]] std::string_view ToString(RuleInput input);
    [[nodiscard]] std::string_view ToString(RuleRemap remap);
    [[nodiscard]] std::string_view ToString(RuleBlend blend);
    [[nodiscard]] std::string_view ToString(NoiseType type);
    [[nodiscard]] std::optional<RuleInput> ParseRuleInput(std::string_view text);
    [[nodiscard]] std::optional<RuleRemap> ParseRuleRemap(std::string_view text);
    [[nodiscard]] std::optional<RuleBlend> ParseRuleBlend(std::string_view text);
    [[nodiscard]] std::optional<NoiseType> ParseNoiseType(std::string_view text);
}
