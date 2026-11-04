#include "PlutoGE/render/postprocess/VoxelConeTracingEffect.h"

#include "PlutoGE/render/VctProbeCache.h"

#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        bool ParseBool(const std::string &value) { return value == "true" || value == "1"; }
    }

    std::vector<PostProcessParameter> VoxelConeTracingEffect::GetParameters() const
    {
        return {
            {"Resolution", PostProcessParameterType::Enum, std::to_string(m_resolution == 64 ? 0 : 1), {"64", "128"}},
            {"Cascade Count", PostProcessParameterType::Int, std::to_string(m_requestedCascadeCount)},
            {"Volume Size", PostProcessParameterType::Float, std::to_string(m_volumeSize)},
            {"World Cache", PostProcessParameterType::Bool, m_worldCache ? "true" : "false"},
            {"Cache Size", PostProcessParameterType::Float, std::to_string(m_cacheSize)},
            {"Update Speed", PostProcessParameterType::Float, std::to_string(m_updateSpeed)},
            {"Cache Updates", PostProcessParameterType::Int, std::to_string(m_cacheUpdates)},
            {"Intensity", PostProcessParameterType::Float, std::to_string(m_intensity)},
            {"Cone Count", PostProcessParameterType::Int, std::to_string(m_coneCount)},
            {"Trace Quality", PostProcessParameterType::Enum, std::to_string(m_traceResolutionDivisor == 4 ? 0 : 1), {"Balanced", "High"}},
            {"Inject Local Lights", PostProcessParameterType::Bool, m_injectLocalLights ? "true" : "false"},
            {"Local Light Bounce", PostProcessParameterType::Float, std::to_string(m_localLightBounce)},
            {"Secondary Bounce", PostProcessParameterType::Float, std::to_string(m_secondaryBounce)},
            {"Voxelization LOD Bias", PostProcessParameterType::Int, std::to_string(m_voxelizationLodBias)},
            {"Voxelization Command Budget", PostProcessParameterType::Int, std::to_string(m_voxelizationCommandBudget)},
            {"Cone Aperture", PostProcessParameterType::Float, std::to_string(m_aperture)},
            {"Max Distance", PostProcessParameterType::Float, std::to_string(m_maxDistance)},
            {"Normal Bias", PostProcessParameterType::Float, std::to_string(m_normalBias)},
            {"Refresh Interval (frames)", PostProcessParameterType::Int, std::to_string(m_updateInterval)},
            {"Temporal Blend", PostProcessParameterType::Float, std::to_string(m_temporalBlend)},
            {"History Depth Threshold", PostProcessParameterType::Float, std::to_string(m_historyDepthThreshold)},
            {"History Normal Threshold", PostProcessParameterType::Float, std::to_string(m_historyNormalThreshold)},
            {"Debug View", PostProcessParameterType::Enum, std::to_string(m_debugView), {"Final Composite", "Raw Cone Trace", "Temporal Result", "History Validation", "Voxel Radiance", "Voxel Opacity", "Upsample Classification", "Voxel Sample Count", "Cascade Coverage"}},
            {"Indirect Only", PostProcessParameterType::Bool, m_indirectOnly ? "true" : "false"},
        };
    }

    VoxelConeTracingEffect::Settings VoxelConeTracingEffect::GetSettings() const noexcept
    {
        return {.resolution = m_resolution,
                .cascadeCount = m_requestedCascadeCount,
                .coneCount = m_coneCount,
                .traceResolutionDivisor = m_traceResolutionDivisor,
                .updateInterval = m_updateInterval,
                .updateSpeed = m_updateSpeed,
                .voxelizationCommandBudget = m_voxelizationCommandBudget,
                .voxelizationLodBias = m_voxelizationLodBias,
                .debugView = m_debugView,
                .volumeSize = m_volumeSize,
                .intensity = m_intensity,
                .aperture = m_aperture,
                .maxDistance = m_maxDistance,
                .normalBias = m_normalBias,
                .temporalBlend = m_temporalBlend,
                .historyDepthThreshold = m_historyDepthThreshold,
                .historyNormalThreshold = m_historyNormalThreshold,
                .injectLocalLights = m_injectLocalLights,
                .localLightBounce = m_localLightBounce,
                .secondaryBounce = m_secondaryBounce,
                .indirectOnly = m_indirectOnly,
                .worldCache = m_worldCache, .cacheSize = m_cacheSize, .cacheUpdates = m_cacheUpdates};
    }

    void VoxelConeTracingEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &p : parameters)
        {
            if (p.name == "Resolution")
            {
                const int values[] = {64, 128};
                m_resolution = values[std::clamp(std::stoi(p.value), 0, 1)];
            }
            else if (p.name == "Cascade Count")
                m_requestedCascadeCount = std::clamp(std::stoi(p.value), 1, static_cast<int>(kCascadeCount));
            else if (p.name == "Volume Size")
                m_volumeSize = std::clamp(std::stof(p.value), 4.0f, 512.0f);
            else if (p.name == "World Cache")
                m_worldCache = ParseBool(p.value);
            else if (p.name == "Cache Size")
                m_cacheSize = std::clamp(std::stof(p.value), 16.0f, 4096.0f);
            else if (p.name == "Update Speed")
                m_updateSpeed = VctUpdateSpeed(std::stof(p.value));
            else if (p.name == "Cache Updates")
                m_cacheUpdates = std::clamp(std::stoi(p.value), 1, 256);
            else if (p.name == "Intensity")
                m_intensity = std::clamp(std::stof(p.value), 0.0f, 8.0f);
            else if (p.name == "Cone Count")
                m_coneCount = std::clamp(std::stoi(p.value), 1, 6);
            else if (p.name == "Trace Quality")
                m_traceResolutionDivisor = std::clamp(std::stoi(p.value), 0, 1) == 0 ? 4 : 2;
            else if (p.name == "Secondary Bounce")
                m_secondaryBounce = std::clamp(std::stof(p.value), 0.0f, 1.0f);
            else if (p.name == "Local Light Bounce")
                m_localLightBounce = std::clamp(std::stof(p.value), 0.0f, 16.0f);
            else if (p.name == "Inject Local Lights")
                m_injectLocalLights = ParseBool(p.value);
            else if (p.name == "Voxelization LOD Bias")
                m_voxelizationLodBias = std::clamp(std::stoi(p.value), 0, 4);
            else if (p.name == "Voxelization Command Budget")
                m_voxelizationCommandBudget = std::clamp(std::stoi(p.value), 1, 256);
            else if (p.name == "Cone Aperture")
                m_aperture = std::clamp(std::stof(p.value), 0.1f, 1.2f);
            else if (p.name == "Max Distance")
                m_maxDistance = std::clamp(std::stof(p.value), 1.0f, 256.0f);
            else if (p.name == "Normal Bias")
            {
                const float storedBias = std::stof(p.value);
                // Values above one voxel use the old cone-start convention and
                // can skip narrow gaps entirely. Migrate them to the new default.
                m_normalBias = storedBias > 1.0f ? 0.35f : std::clamp(storedBias, 0.0f, 1.0f);
            }
            else if (p.name == "Update Interval" || p.name == "Refresh Interval (frames)")
                m_updateInterval = std::clamp(std::stoi(p.value), 1, 16);
            else if (p.name == "Temporal Blend")
                m_temporalBlend = std::clamp(std::stof(p.value), 0.0f, 0.98f);
            else if (p.name == "History Depth Threshold")
                m_historyDepthThreshold = std::clamp(std::stof(p.value), 0.001f, 10.0f);
            else if (p.name == "History Normal Threshold")
                m_historyNormalThreshold = std::clamp(std::stof(p.value), 0.0f, 0.999f);
            else if (p.name == "Debug View")
                m_debugView = std::clamp(std::stoi(p.value), 0, 8);
            else if (p.name == "Indirect Only")
                m_indirectOnly = ParseBool(p.value);
        }
    }
}
