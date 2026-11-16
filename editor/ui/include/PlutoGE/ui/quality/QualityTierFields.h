#pragma once

#include "PlutoGE/render/GraphicsQuality.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace PlutoGE::ui
{
    inline constexpr std::size_t QualityTierCount = 4;
    [[nodiscard]] std::string_view QualityTierName(std::size_t tier);

    enum class QualityFieldGroup
    {
        Shadows,
        Lighting,
        PostProcessing,
        Vegetation,
    };
    inline constexpr std::size_t QualityFieldGroupCount = 4;
    [[nodiscard]] std::string_view QualityFieldGroupName(QualityFieldGroup group);

    enum class QualityFieldKind
    {
        Toggle,     // allow/skip; stored as 0 or 1
        Count,      // small whole number
        Resolution, // power of two
        Distance,   // world units, edited logarithmically
        Scale,      // multiplier of authored values
    };

    // Which way a field's value moves its GPU cost, so tiers can be checked for order: a lower tier should never
    // cost more than a higher one.
    enum class QualityCost
    {
        RisesWithValue,
        FallsWithValue,
    };

    // One editable ceiling of render::GraphicsQuality (every field but `enabled`, which is the tier's own switch).
    // The editor builds its rows, menus and checks from this table, so a new ceiling needs only an entry here.
    struct QualityTierField
    {
        using Quality = render::GraphicsQuality;
        using BoolMember = bool Quality::*;
        using CountMember = std::uint32_t Quality::*;
        using FloatMember = float Quality::*;
        using Member = std::variant<BoolMember, CountMember, FloatMember>;

        std::string_view id; // stable: UI ids and tests
        std::string_view label;
        std::string_view description;
        QualityFieldGroup group;
        QualityFieldKind kind;
        Member member;
        double min = 0.0;
        double max = 1.0;
        QualityCost cost = QualityCost::RisesWithValue;

        [[nodiscard]] double Get(const render::GraphicsQuality &quality) const;
        // Clamped to [min, max]; resolutions round to a power of two and counts to whole numbers.
        void Set(render::GraphicsQuality &quality, double value) const;
        [[nodiscard]] bool Equal(const render::GraphicsQuality &a, const render::GraphicsQuality &b) const;
        // The value as shown to the user ("On", "2048", "150 m", "75%").
        [[nodiscard]] std::string Format(double value) const;
        // Value scaled so a larger number always costs more.
        [[nodiscard]] double Cost(const render::GraphicsQuality &quality) const
        { return cost == QualityCost::RisesWithValue ? Get(quality) : -Get(quality); }
    };

    [[nodiscard]] std::span<const QualityTierField> GetQualityTierFields();
    [[nodiscard]] const QualityTierField *FindQualityTierField(std::string_view id);
}
