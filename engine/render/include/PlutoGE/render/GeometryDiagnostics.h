#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace PlutoGE::render
{
    // Geometry-pass shading diagnostics. Each mode removes or replaces one
    // shading feature so its GPU cost can be measured in isolation. The value
    // reaches BasicLit.slang as pointParameters.w: append, never renumber.
    enum class GeometryDiagnosticMode : std::uint8_t
    {
        None, ReferenceSky, BypassDirectionalShadows, ReferenceDirectionalShadows,
        MaterialOnly, BypassDetailTextures, BypassPointLights, BypassSkyLighting,
        AutomaticSweep, DrawRanges, MinimalShading, BypassReflectionCaptures, BypassProbeGrid, ReferenceProbeGrid
    };

    struct GeometryDiagnosticInfo
    {
        GeometryDiagnosticMode mode;
        std::string_view token; // Stable command-line/automation name.
        std::string_view name;  // Human-readable label.
    };

    inline constexpr std::array GeometryDiagnostics{
        GeometryDiagnosticInfo{GeometryDiagnosticMode::None, "none", "Normal rendering"},
        GeometryDiagnosticInfo{GeometryDiagnosticMode::ReferenceSky, "reference-sky", "Original sky evaluation"},
        GeometryDiagnosticInfo{GeometryDiagnosticMode::BypassDirectionalShadows, "bypass-directional-shadows", "Bypass directional shadow sampling"},
        GeometryDiagnosticInfo{GeometryDiagnosticMode::ReferenceDirectionalShadows, "scalar-shadows", "Scalar directional shadow filter"},
        GeometryDiagnosticInfo{GeometryDiagnosticMode::MaterialOnly, "material-only", "Material only (no lighting)"},
        GeometryDiagnosticInfo{GeometryDiagnosticMode::BypassDetailTextures, "bypass-detail-textures", "Bypass standard detail textures"},
        GeometryDiagnosticInfo{GeometryDiagnosticMode::BypassPointLights, "bypass-point-lights", "Bypass point lights"},
        GeometryDiagnosticInfo{GeometryDiagnosticMode::BypassSkyLighting, "bypass-sky-lighting", "Bypass sky lighting"},
        GeometryDiagnosticInfo{GeometryDiagnosticMode::AutomaticSweep, "sweep", "Automatic shading sweep"},
        GeometryDiagnosticInfo{GeometryDiagnosticMode::DrawRanges, "draw-ranges", "Draw range timings"},
        GeometryDiagnosticInfo{GeometryDiagnosticMode::MinimalShading, "minimal-shading", "Minimal shading (coverage preserved)"},
        GeometryDiagnosticInfo{GeometryDiagnosticMode::BypassReflectionCaptures, "bypass-reflection-captures", "Bypass reflection captures"},
        GeometryDiagnosticInfo{GeometryDiagnosticMode::BypassProbeGrid, "bypass-probe-grid", "Bypass diffuse probe grid"},
        GeometryDiagnosticInfo{GeometryDiagnosticMode::ReferenceProbeGrid, "reference-probe-grid", "Forward probe grid (no deferred resolve)"},
    };

    // Variants AutomaticSweep cycles through, each held for SweepFramesPerMode frames.
    inline constexpr std::array GeometrySweepModes{
        GeometryDiagnosticMode::None, GeometryDiagnosticMode::MinimalShading, GeometryDiagnosticMode::MaterialOnly,
        GeometryDiagnosticMode::BypassDirectionalShadows, GeometryDiagnosticMode::BypassDetailTextures,
        GeometryDiagnosticMode::BypassPointLights, GeometryDiagnosticMode::BypassSkyLighting,
        GeometryDiagnosticMode::BypassReflectionCaptures, GeometryDiagnosticMode::BypassProbeGrid};
    inline constexpr std::size_t SweepFramesPerMode = 32;

    constexpr const char *GeometryDiagnosticName(GeometryDiagnosticMode mode) noexcept
    {
        for (const auto &info : GeometryDiagnostics)
            if (info.mode == mode) return info.name.data();
        return GeometryDiagnostics.front().name.data();
    }

    constexpr std::optional<GeometryDiagnosticMode> ParseGeometryDiagnostic(std::string_view token) noexcept
    {
        for (const auto &info : GeometryDiagnostics)
            if (info.token == token) return info.mode;
        return std::nullopt;
    }
}
