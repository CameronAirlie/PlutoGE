#include "PlutoGE/render/postprocess/VolumetricFogEffect.h"

#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kMinStepCount = 16;
        constexpr int kMaxStepCount = 64;
        constexpr int kMinShadowStepStride = 1;
        constexpr int kMaxShadowStepStride = 4;

        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "1";
        }
    }

    std::vector<PostProcessParameter> VolumetricFogEffect::GetParameters() const
    {
        // Listed in the order of GetParameterInfo's groups.
        const auto real = [](const char *name, float value)
        { return PostProcessParameter{.name = name, .type = PostProcessParameterType::Float, .value = std::to_string(value)}; };
        const auto integer = [](const char *name, int value)
        { return PostProcessParameter{.name = name, .type = PostProcessParameterType::Int, .value = std::to_string(value)}; };
        return {
            real("Density", m_density),
            real("Height Falloff", m_heightFalloff),
            real("Height Offset", m_heightOffset),
            real("Max Opacity", m_maxOpacity),
            real("Color R", m_fogColor.r),
            real("Color G", m_fogColor.g),
            real("Color B", m_fogColor.b),
            real("Scattering", m_scattering),
            real("Anisotropy", m_anisotropy),
            real("Ambient Contribution", m_ambientContribution),
            real("Directional Contribution", m_directionalContribution),
            real("Horizon Haze", m_horizonHaze),
            real("Haze Width", m_hazeWidth),
            real("Haze Distance", m_hazeDistance),
            real("Noise Strength", m_noiseStrength),
            real("Noise Scale", m_noiseScale),
            real("Noise Wind X", m_noiseWind.x),
            real("Noise Wind Z", m_noiseWind.y),
            real("Shadow Detail Distance", m_shadowDetailDistance),
            integer("Step Count", m_stepCount),
            integer("Shadow Step Stride", m_shadowStepStride),
            PostProcessParameter{.name = "Half Resolution", .type = PostProcessParameterType::Bool,
                                 .value = m_halfResolution ? "true" : "false"},
        };
    }

    std::vector<PostProcessParameterInfo> VolumetricFogEffect::GetParameterInfo() const
    {
        constexpr const char *height = "Height Fog";
        constexpr const char *look = "Colour (height fog and haze)";
        constexpr const char *light = "Height Fog Lighting";
        constexpr const char *haze = "Horizon Haze";
        constexpr const char *noise = "Density Noise";
        constexpr const char *quality = "Quality";
        return {
            {"Density", height, "Fog per metre at and below Height Offset. 0 leaves only the horizon haze."},
            {"Height Falloff", height, "How fast the fog thins above Height Offset: density divides by e every 1/falloff metres "
                                       "(0.12 = every 8 m). 0 makes the fog the same at every height."},
            {"Height Offset", height, "World Y at which the fog has its full Density. Set it near the ground: the fog all but "
                                      "disappears a few multiples of 1/Height Falloff above it."},
            {"Max Opacity", height, "Scales how much the height fog hides the scene. 1 is physically based. Does not "
                                    "limit the horizon haze, which must stay whole to hide the fog's edge at the horizon."},
            {"Color R", look, "Tints the fog's scattered light and the horizon haze. White leaves them untinted."},
            {"Color G", look, "Tints the fog's scattered light and the horizon haze. White leaves them untinted."},
            {"Color B", look, "Tints the fog's scattered light and the horizon haze. White leaves them untinted."},
            {"Scattering", light, "How much light the height fog scatters toward the camera. 0 makes it dark, absorbing fog."},
            {"Anisotropy", light, "Forward scattering: above 0 the fog glows brighter looking toward the sun."},
            {"Ambient Contribution", light, "Sky light the height fog scatters. With Directional Contribution at 0 too, the fog is black."},
            {"Directional Contribution", light, "Sunlight the height fog scatters, including light shafts."},
            {"Horizon Haze", haze, "Strength of the distant aerial haze, lit by the sky and tinted by Color. Independent of Density and Max Opacity; "
                                  "0 turns it off."},
            {"Haze Width", haze, "Angular height of the haze band above the horizon, in degrees."},
            {"Haze Distance", haze, "Distance in metres over which the haze builds up."},
            {"Noise Strength", noise, "Patchiness of the height fog. The average density stays the same."},
            {"Noise Scale", noise, "Size of the patches in metres."},
            {"Noise Wind X", noise, "Drift of the patches along X, in metres per second."},
            {"Noise Wind Z", noise, "Drift of the patches along Z, in metres per second."},
            {"Shadow Detail Distance", quality, "Distance over which shadows and noise shape the fog. Beyond it the fog is smooth."},
            {"Step Count", quality, "Samples along each ray within Shadow Detail Distance. More is smoother and costlier."},
            {"Shadow Step Stride", quality, "Evaluate shadows every Nth step. Higher is cheaper with coarser shafts."},
            {"Half Resolution", quality, "Render the fog at half resolution and upscale. Much cheaper."},
        };
    }

    void VolumetricFogEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Density")
            {
                m_density = std::max(std::stof(parameter.value), 0.0f);
            }
            else if (parameter.name == "Height Falloff")
            {
                m_heightFalloff = std::max(std::stof(parameter.value), 0.0f);
            }
            else if (parameter.name == "Height Offset")
            {
                m_heightOffset = std::stof(parameter.value);
            }
            else if ((parameter.name == "Shadow Detail Distance" || parameter.name == "Max Distance"))
            {
                m_shadowDetailDistance = std::max(std::stof(parameter.value), 0.1f);
            }
            else if (parameter.name == "Horizon Haze")
                m_horizonHaze = std::clamp(std::stof(parameter.value), 0.0f, 4.0f);
            else if (parameter.name == "Haze Width")
                m_hazeWidth = std::clamp(std::stof(parameter.value), 0.1f, 45.0f);
            else if (parameter.name == "Haze Distance")
                m_hazeDistance = std::clamp(std::stof(parameter.value), 1.0f, 1000000.0f);
            else if (parameter.name == "Noise Strength")
                m_noiseStrength = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            else if (parameter.name == "Noise Scale")
                m_noiseScale = std::clamp(std::stof(parameter.value), 1.0f, 10000.0f);
            else if (parameter.name == "Noise Wind X")
                m_noiseWind.x = std::clamp(std::stof(parameter.value), -100.0f, 100.0f);
            else if (parameter.name == "Noise Wind Z")
                m_noiseWind.y = std::clamp(std::stof(parameter.value), -100.0f, 100.0f);
            else if (parameter.name == "Scattering")
            {
                m_scattering = glm::clamp(std::stof(parameter.value), 0.0f, 2.0f);
            }
            else if (parameter.name == "Anisotropy")
            {
                m_anisotropy = glm::clamp(std::stof(parameter.value), -0.85f, 0.85f);
            }
            else if (parameter.name == "Ambient Contribution")
            {
                m_ambientContribution = glm::clamp(std::stof(parameter.value), 0.0f, 4.0f);
            }
            else if (parameter.name == "Directional Contribution")
            {
                m_directionalContribution = glm::clamp(std::stof(parameter.value), 0.0f, 32.0f);
            }
            else if (parameter.name == "Max Opacity")
            {
                m_maxOpacity = glm::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Step Count")
            {
                m_stepCount = std::clamp(std::stoi(parameter.value), kMinStepCount, kMaxStepCount);
            }
            else if (parameter.name == "Shadow Step Stride")
            {
                m_shadowStepStride = std::clamp(std::stoi(parameter.value), kMinShadowStepStride, kMaxShadowStepStride);
            }
            else if (parameter.name == "Half Resolution")
            {
                m_halfResolution = ParseBool(parameter.value);
            }
            else if (parameter.name == "Color R")
            {
                m_fogColor.r = glm::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Color G")
            {
                m_fogColor.g = glm::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Color B")
            {
                m_fogColor.b = glm::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
        }
    }
}
