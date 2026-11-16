#include "PlutoGE/scene/procedural/TerrainRules.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace PlutoGE::scene::procedural
{
    namespace
    {
        float SmoothStep(float edge0, float edge1, float x)
        {
            if (edge1 <= edge0)
                return x >= edge1 ? 1.0f : 0.0f;
            const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        template <typename Enum, std::size_t Count>
        std::optional<Enum> Parse(std::string_view text, const std::array<std::string_view, Count> &names)
        {
            for (std::size_t index = 0; index < Count; ++index)
                if (names[index] == text)
                    return static_cast<Enum>(index);
            return std::nullopt;
        }

        constexpr std::array<std::string_view, 8> InputNames{"Constant", "Height", "Slope", "Curvature", "Noise", "Mask", "Distance", "WaterDepth"};
        constexpr std::array<std::string_view, 2> RemapNames{"Band", "Linear"};
        constexpr std::array<std::string_view, 6> BlendNames{"Multiply", "Add", "Subtract", "Min", "Max", "Replace"};
        constexpr std::array<std::string_view, 3> NoiseNames{"Value", "Fbm", "Ridged"};
    }

    TerrainRule TerrainRule::Make(RuleInput input)
    {
        TerrainRule rule;
        rule.input = input;
        switch (input)
        {
        case RuleInput::Height:
            rule.min = 0.0f;
            rule.max = 100.0f;
            rule.falloff = 10.0f;
            break;
        case RuleInput::Slope:
            rule.min = 30.0f;
            rule.max = 90.0f;
            rule.falloff = 5.0f;
            break;
        case RuleInput::Curvature:
            rule.min = 0.02f;
            rule.max = 10.0f;
            rule.falloff = 0.02f;
            break;
        case RuleInput::Noise:
        case RuleInput::Mask:
            rule.remap = RuleRemap::Linear;
            rule.min = 0.0f;
            rule.max = 1.0f;
            break;
        case RuleInput::Distance:
            rule.min = 0.0f;
            rule.max = 5.0f;
            rule.falloff = 5.0f;
            break;
        case RuleInput::WaterDepth:
            // Under water, easing in over the first 25 cm.
            rule.min = 0.25f;
            rule.max = 1000.0f;
            rule.falloff = 0.25f;
            break;
        case RuleInput::Constant:
            break;
        }
        return rule;
    }

    float TerrainRule::DistanceLimit() const
    {
        return remap == RuleRemap::Band ? std::max(max, 0.0f) + std::max(falloff, 0.0f) : std::max(max, 0.0f);
    }

    float TerrainRule::Evaluate(const TerrainPoint &point, const IRuleInputs &inputs) const
    {
        if (input == RuleInput::Constant)
            return std::clamp(invert ? 1.0f - constant : constant, 0.0f, 1.0f);

        float value = 0.0f;
        switch (input)
        {
        case RuleInput::Height: value = point.height; break;
        case RuleInput::Slope: value = point.slope; break;
        case RuleInput::Curvature: value = point.curvature; break;
        case RuleInput::Noise: value = EvaluateNoise(noise, point.world, inputs.Seed()); break;
        case RuleInput::Mask: value = inputs.Mask(source, point.world); break;
        case RuleInput::Distance: value = inputs.Distance(source, point.world, DistanceLimit()); break;
        case RuleInput::WaterDepth: value = inputs.WaterDepth(point.world, point.height).value_or(NoWaterDepth); break;
        case RuleInput::Constant: break;
        }

        float result = 0.0f;
        if (remap == RuleRemap::Linear)
            result = max != min ? std::clamp((value - min) / (max - min), 0.0f, 1.0f) : (value >= max ? 1.0f : 0.0f);
        else if (falloff > 0.0f)
            result = SmoothStep(min - falloff, min, value) * (1.0f - SmoothStep(max, max + falloff, value));
        else
            result = value >= min && value <= max ? 1.0f : 0.0f;
        return invert ? 1.0f - result : result;
    }

    float TerrainRuleStack::Evaluate(const TerrainPoint &point, const IRuleInputs &inputs) const
    {
        float value = 1.0f;
        for (const auto &rule : rules)
        {
            if (!rule.enabled || rule.opacity <= 0.0f)
                continue;
            const float sample = rule.Evaluate(point, inputs);
            float combined = sample;
            switch (rule.blend)
            {
            case RuleBlend::Multiply: combined = value * sample; break;
            case RuleBlend::Add: combined = value + sample; break;
            case RuleBlend::Subtract: combined = value - sample; break;
            case RuleBlend::Min: combined = std::min(value, sample); break;
            case RuleBlend::Max: combined = std::max(value, sample); break;
            case RuleBlend::Replace: combined = sample; break;
            }
            const float opacity = std::min(rule.opacity, 1.0f);
            value = std::clamp(value + (std::clamp(combined, 0.0f, 1.0f) - value) * opacity, 0.0f, 1.0f);
        }
        return value;
    }

    bool TerrainRuleStack::Uses(RuleInput input) const
    {
        return std::any_of(rules.begin(), rules.end(), [input](const TerrainRule &rule) { return rule.enabled && rule.input == input; });
    }

    std::string_view ToString(RuleInput input) { return InputNames[static_cast<std::size_t>(input)]; }
    std::string_view ToString(RuleRemap remap) { return RemapNames[static_cast<std::size_t>(remap)]; }
    std::string_view ToString(RuleBlend blend) { return BlendNames[static_cast<std::size_t>(blend)]; }
    std::string_view ToString(NoiseType type) { return NoiseNames[static_cast<std::size_t>(type)]; }
    std::optional<RuleInput> ParseRuleInput(std::string_view text) { return Parse<RuleInput>(text, InputNames); }
    std::optional<RuleRemap> ParseRuleRemap(std::string_view text) { return Parse<RuleRemap>(text, RemapNames); }
    std::optional<RuleBlend> ParseRuleBlend(std::string_view text) { return Parse<RuleBlend>(text, BlendNames); }
    std::optional<NoiseType> ParseNoiseType(std::string_view text) { return Parse<NoiseType>(text, NoiseNames); }
}
