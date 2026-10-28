#include "PlutoGE/render/postprocess/DDGIEffect.h"
#include "PlutoGE/render/DdgiConfig.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace PlutoGE::render
{
    namespace
    {
        constexpr std::size_t CustomQuality = DdgiQualityNames.size();

        float ParseFloat(const std::string &value, float fallback)
        {
            try { return std::stof(value); }
            catch (...) { return fallback; }
        }

        int ParseInt(const std::string &value, int fallback)
        {
            try { return std::stoi(value); }
            catch (...) { return fallback; }
        }

        bool ParseBool(const std::string &value) { return value == "true" || value == "1"; }
        std::string Bool(bool value) { return value ? "true" : "false"; }

        std::string Grouped(std::uint64_t value)
        {
            auto digits = std::to_string(value);
            for (int position = static_cast<int>(digits.size()) - 3; position > 0; position -= 3)
                digits.insert(static_cast<std::size_t>(position), ",");
            return digits;
        }

        std::string Format(const char *format, double a, double b = 0.0, double c = 0.0)
        {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), format, a, b, c);
            return buffer;
        }
    }

    std::optional<DdgiQuality> DDGIEffect::GetQuality() const
    {
        for (std::size_t index = 0; index < DdgiQualityNames.size(); ++index)
            if (MatchesDdgiQuality(m_settings, static_cast<DdgiQuality>(index)))
                return static_cast<DdgiQuality>(index);
        return std::nullopt;
    }

    std::vector<PostProcessParameter> DDGIEffect::GetParameters() const
    {
        const auto &s = m_settings;
        const auto quality = GetQuality();
        std::vector<std::string> qualities(DdgiQualityNames.begin(), DdgiQualityNames.end());
        qualities.emplace_back("Custom");
        return {
            {"Quality", PostProcessParameterType::Enum,
             std::to_string(quality ? static_cast<std::size_t>(*quality) : CustomQuality), qualities},
            {"Intensity", PostProcessParameterType::Float, std::to_string(s.intensity)},
            {"Probe Count X", PostProcessParameterType::Int, std::to_string(s.grid.counts.x)},
            {"Probe Count Y", PostProcessParameterType::Int, std::to_string(s.grid.counts.y)},
            {"Probe Count Z", PostProcessParameterType::Int, std::to_string(s.grid.counts.z)},
            {"Probe Spacing", PostProcessParameterType::Float, std::to_string(s.grid.spacing)},
            {"Probe Cascades", PostProcessParameterType::Int, std::to_string(s.grid.cascades)},
            {"Rays Per Probe", PostProcessParameterType::Int, std::to_string(s.grid.blendedRays)},
            {"Probes Per Frame", PostProcessParameterType::Int, std::to_string(s.grid.probesPerFrame)},
            {"Hysteresis", PostProcessParameterType::Float, std::to_string(s.hysteresis)},
            {"Normal Bias", PostProcessParameterType::Float, std::to_string(s.normalBias)},
            {"View Bias", PostProcessParameterType::Float, std::to_string(s.viewBias)},
            {"Probe Relocation", PostProcessParameterType::Bool, Bool(s.relocation)},
            {"Probe Classification", PostProcessParameterType::Bool, Bool(s.classification)},
            {"Multiple Bounces", PostProcessParameterType::Bool, Bool(s.multipleBounces)},
            {"Brightness Clamp", PostProcessParameterType::Float, std::to_string(s.brightnessClamp)},
            {"Reflection Occlusion", PostProcessParameterType::Float, std::to_string(s.reflectionOcclusion)},
            {"Voxel Resolution", PostProcessParameterType::Enum, s.voxels.resolution == 128 ? "1" : "0", {"64", "128"}},
            {"Voxel Cascades", PostProcessParameterType::Int, std::to_string(s.voxels.cascades)},
            {"Voxel Volume Size", PostProcessParameterType::Float, std::to_string(s.voxels.volumeSize)},
            {"Voxel Update Speed", PostProcessParameterType::Float, std::to_string(s.voxels.updateSpeed)},
            {"Inject Local Lights", PostProcessParameterType::Bool, Bool(s.voxels.injectLocalLights)},
            {"Local Light Bounce", PostProcessParameterType::Float, std::to_string(s.voxels.localLightBounce)},
            {"Secondary Bounce", PostProcessParameterType::Float, std::to_string(s.voxels.secondaryBounce)},
            {"Voxelization Budget", PostProcessParameterType::Int, std::to_string(s.voxels.commandBudget)},
            {"Debug View", PostProcessParameterType::Enum, std::to_string(s.debugView),
             {"None", "Irradiance", "Indirect Lighting", "Probes", "Sky Visibility", "Reflections"}},
            {"Probe Debug Radius", PostProcessParameterType::Float, std::to_string(s.probeRadius)},
        };
    }

    void DDGIEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        const auto previousQuality = GetQuality();
        std::optional<DdgiQuality> requestedQuality;
        auto s = m_settings;
        for (const auto &p : parameters)
        {
            if (p.name == "Quality")
            {
                // A list carries every parameter; only a changed quality applies its level over the others.
                const auto index = static_cast<std::size_t>(std::max(ParseInt(p.value, 0), 0));
                if (index < CustomQuality && (!previousQuality || static_cast<std::size_t>(*previousQuality) != index))
                    requestedQuality = static_cast<DdgiQuality>(index);
            }
            else if (p.name == "Intensity") s.intensity = ParseFloat(p.value, s.intensity);
            else if (p.name == "Probe Count X") s.grid.counts.x = ParseInt(p.value, s.grid.counts.x);
            else if (p.name == "Probe Count Y") s.grid.counts.y = ParseInt(p.value, s.grid.counts.y);
            else if (p.name == "Probe Count Z") s.grid.counts.z = ParseInt(p.value, s.grid.counts.z);
            else if (p.name == "Probe Spacing") s.grid.spacing = ParseFloat(p.value, s.grid.spacing);
            else if (p.name == "Probe Cascades") s.grid.cascades = ParseInt(p.value, s.grid.cascades);
            else if (p.name == "Rays Per Probe")
                s.grid.blendedRays = static_cast<std::uint32_t>(std::max(ParseInt(p.value, 96), 0));
            else if (p.name == "Probes Per Frame")
                s.grid.probesPerFrame = static_cast<std::uint32_t>(std::max(ParseInt(p.value, 1152), 0));
            else if (p.name == "Hysteresis") s.hysteresis = ParseFloat(p.value, s.hysteresis);
            else if (p.name == "Normal Bias") s.normalBias = ParseFloat(p.value, s.normalBias);
            else if (p.name == "View Bias") s.viewBias = ParseFloat(p.value, s.viewBias);
            else if (p.name == "Probe Relocation") s.relocation = ParseBool(p.value);
            else if (p.name == "Probe Classification") s.classification = ParseBool(p.value);
            else if (p.name == "Multiple Bounces") s.multipleBounces = ParseBool(p.value);
            else if (p.name == "Brightness Clamp") s.brightnessClamp = ParseFloat(p.value, s.brightnessClamp);
            else if (p.name == "Reflection Occlusion")
                s.reflectionOcclusion = ParseFloat(p.value, s.reflectionOcclusion);
            else if (p.name == "Voxel Resolution") s.voxels.resolution = ParseInt(p.value, 0) == 1 ? 128 : 64;
            else if (p.name == "Voxel Cascades") s.voxels.cascades = ParseInt(p.value, s.voxels.cascades);
            else if (p.name == "Voxel Volume Size") s.voxels.volumeSize = ParseFloat(p.value, s.voxels.volumeSize);
            else if (p.name == "Voxel Update Speed") s.voxels.updateSpeed = ParseFloat(p.value, s.voxels.updateSpeed);
            else if (p.name == "Inject Local Lights") s.voxels.injectLocalLights = ParseBool(p.value);
            else if (p.name == "Local Light Bounce")
                s.voxels.localLightBounce = ParseFloat(p.value, s.voxels.localLightBounce);
            else if (p.name == "Secondary Bounce") s.voxels.secondaryBounce = ParseFloat(p.value, s.voxels.secondaryBounce);
            else if (p.name == "Voxelization Budget") s.voxels.commandBudget = ParseInt(p.value, s.voxels.commandBudget);
            else if (p.name == "Debug View") s.debugView = ParseInt(p.value, 0);
            else if (p.name == "Probe Debug Radius") s.probeRadius = ParseFloat(p.value, s.probeRadius);
        }
        if (requestedQuality)
            s = ApplyDdgiQuality(s, *requestedQuality);
        m_settings = s.Sanitized();
    }

    std::vector<std::string> DDGIEffect::GetSummary() const
    {
        const auto &grid = m_settings.grid;
        const auto finest = grid.Extent(0), widest = grid.Extent(grid.cascades - 1);
        std::string refresh;
        for (int cascade = 0; cascade < grid.cascades; ++cascade)
            refresh += (cascade ? ", " : "") + std::to_string(grid.FramesPerSweep(cascade));
        return {
            Grouped(grid.TotalProbes()) + " probes in " + std::to_string(grid.cascades) +
                Format(" cascades: %.0f x %.0f x %.0f units around the camera", finest.x, finest.y, finest.z) +
                (grid.cascades > 1 ? Format(", reaching %.0f x %.0f x %.0f", widest.x, widest.y, widest.z) : ""),
            Grouped(grid.FrameBudget()) + " probes per frame at " + std::to_string(grid.blendedRays) + " rays (+" +
                std::to_string(PLUTO_DDGI_FIXED_RAYS) + " for classification); cascades refresh every " + refresh +
                " frames",
            Format("Probe data %.1f MiB on the GPU", static_cast<double>(grid.MemoryBytes()) / (1024.0 * 1024.0)),
            "Rays march the voxel scene. With Voxel Cone Traced GI on the same camera, its voxel settings apply; use one "
            "or the other for diffuse bounce, not both.",
            "Relights opaque lit surfaces; transparent and unlit ones keep their ambient light.",
            m_settings.reflectionOcclusion > 0.0f
                ? "Sky reflections fade where the probes see no sky, so interiors reflect their surroundings."
                : "Reflection Occlusion is off: surfaces reflect the sky wherever SSR finds no hit.",
        };
    }
}
