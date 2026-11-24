#include "PlutoGE/ui/quality/QualityTierFields.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace PlutoGE::ui
{
    namespace
    {
        using Q = render::GraphicsQuality;
        using enum QualityFieldGroup;
        using enum QualityFieldKind;

        constexpr QualityTierField Allow(std::string_view id, std::string_view label, std::string_view description,
                                          QualityFieldGroup group, bool Q::*member,
                                          QualityCost cost = QualityCost::RisesWithValue)
        {
            return {id, label, description, group, QualityFieldKind::Toggle, member, 0.0, 1.0, cost};
        }

        const std::array Fields{
            QualityTierField{"shadowResolution", "Resolution ceiling",
                "Caps cascaded directional shadow-map resolution. Virtual-shadow budgets and local shadows are unchanged.",
                Shadows, Resolution, &Q::shadowResolution, double(Q::MinShadowResolution), double(Q::MaxShadowResolution)},
            QualityTierField{"shadowCascades", "Cascade ceiling", "Caps the authored cascade count.",
                Shadows, Count, &Q::shadowCascades, double(Q::MinShadowCascades), double(Q::MaxShadowCascades)},
            QualityTierField{"shadowDistance", "Distance ceiling",
                "Caps directional shadow distance and any authored positive caster-distance limit, in world units.",
                Shadows, Distance, &Q::shadowDistance, Q::MinShadowDistance, Q::MaxShadowDistance},
            Allow("cascadedShadows", "Force cascaded shadows",
                "On: selects cascaded directional shadows, usually cheaper than virtual shadow maps. Off: keeps the "
                "scene's authored shadow method.",
                Shadows, &Q::cascadedShadows, QualityCost::FallsWithValue),
            Allow("contactShadows", "Contact shadows",
                "Allows screen-space contact shadows from the sun and local lights.", Shadows, &Q::contactShadows),
            Allow("tracedShadows", "Traced shadows",
                "Allows local lights without a shadow map to be traced against the world, so their light stops at walls, "
                "floors and terrain.", Shadows, &Q::tracedShadows),
            Allow("foliageShadowSway", "Foliage shadow sway",
                "Lets swaying foliage sway in the sun's virtual shadow maps. Off, those shadows hold the rest pose and "
                "stay cached, which is much cheaper under a dense canopy; the plants still sway.", Shadows,
                &Q::foliageShadowSway),
            Allow("ambientOcclusion", "Ambient occlusion", "Allows SSAO.", Lighting, &Q::ambientOcclusion),
            Allow("globalIllumination", "Global illumination",
                "Allows SSGI, DDGI probes and voxel cone tracing GI. Baked ambient volumes stay on.", Lighting,
                &Q::globalIllumination),
            Allow("reflections", "Screen-space reflections",
                "Allows SSR; material environment reflections and reflection captures remain.", Lighting, &Q::reflections),
            Allow("volumetrics", "Volumetrics", "Allows volumetric fog and volumetric clouds.", Lighting, &Q::volumetrics),
            Allow("bloom", "Bloom and lens flare", "Allows bloom and lens flare.", PostProcessing, &Q::bloom),
            Allow("depthOfField", "Depth of field", "Allows the depth-of-field effect.", PostProcessing, &Q::depthOfField),
            Allow("motionBlur", "Motion blur", "Allows the motion-blur effect.", PostProcessing, &Q::motionBlur),
            Allow("terrainAntiTiling", "Terrain anti-tiling",
                "Lets terrain layers tile stochastically, which hides repeats but fetches each layer map twice. Off, "
                "they tile plainly.", Vegetation, &Q::terrainAntiTiling),
            QualityTierField{"foliageDetail", "Foliage detail",
                "Caps instanced foliage detail. Below 100%, plants switch to coarser LODs, and trees to their impostors, "
                "from nearer: 50% does so at twice the screen size.",
                Vegetation, Scale, &Q::foliageDetail, render::MinFoliageDetail, render::MaxFoliageDetail},
            QualityTierField{"grassDensity", "Grass density",
                "Share of each detail grass layer's authored clumps per square metre.",
                Vegetation, Scale, &Q::grassDensity, Q::MinGrassDensity, 1.0},
            QualityTierField{"grassDistance", "Grass distance",
                "Share of each detail grass layer's authored draw, full-density and LOD distances.",
                Vegetation, Scale, &Q::grassDistance, Q::MinGrassDistance, 1.0},
        };
    }

    std::string_view QualityTierName(std::size_t tier)
    {
        constexpr std::array<std::string_view, QualityTierCount> names{"Low", "Medium", "High", "Ultra"};
        return tier < names.size() ? names[tier] : "Unknown";
    }

    std::string_view QualityFieldGroupName(QualityFieldGroup group)
    {
        switch (group)
        {
        case Shadows: return "Shadows";
        case Lighting: return "Lighting";
        case PostProcessing: return "Post-processing";
        case Vegetation: return "Vegetation";
        }
        return "Other";
    }

    double QualityTierField::Get(const render::GraphicsQuality &quality) const
    {
        return std::visit([&](auto pointer) { return static_cast<double>(quality.*pointer); }, member);
    }

    void QualityTierField::Set(render::GraphicsQuality &quality, double value) const
    {
        if (!std::isfinite(value)) return;
        value = std::clamp(value, min, max);
        if (kind == Resolution) value = std::clamp(std::exp2(std::round(std::log2(value))), min, max);
        std::visit([&](auto pointer) {
            using Value = std::remove_reference_t<decltype(quality.*pointer)>;
            if constexpr (std::is_same_v<Value, bool>) quality.*pointer = value >= 0.5;
            else if constexpr (std::is_integral_v<Value>) quality.*pointer = static_cast<Value>(std::lround(value));
            else quality.*pointer = static_cast<Value>(value);
        }, member);
    }

    bool QualityTierField::Equal(const render::GraphicsQuality &a, const render::GraphicsQuality &b) const
    {
        return std::visit([&](auto pointer) { return a.*pointer == b.*pointer; }, member);
    }

    std::string QualityTierField::Format(double value) const
    {
        char text[32];
        switch (kind)
        {
        case Toggle: return value >= 0.5 ? (cost == QualityCost::FallsWithValue ? "Forced" : "On") : (cost == QualityCost::FallsWithValue ? "Authored" : "Off");
        case Count:
        case Resolution: std::snprintf(text, sizeof(text), "%.0f", value); break;
        case Distance: std::snprintf(text, sizeof(text), value < 10.0 ? "%.1f m" : "%.0f m", value); break;
        case Scale: std::snprintf(text, sizeof(text), "%.0f%%", value * 100.0); break;
        }
        return text;
    }

    std::span<const QualityTierField> GetQualityTierFields() { return Fields; }

    const QualityTierField *FindQualityTierField(std::string_view id)
    {
        const auto found = std::ranges::find(Fields, id, &QualityTierField::id);
        return found == Fields.end() ? nullptr : &*found;
    }
}
