#include "PlutoGE/render/CloudOcclusion.h"

#include "CloudNoiseVolume.h"
#include "PlutoGE/render/BasicRenderer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <tuple>

namespace PlutoGE::render
{
    namespace
    {
        // Mean of densityLocal's vertical profile in VolumetricCloud.slang:
        // smoothstep(0, 0.14, h) * (1 - smoothstep(0.62, 1, h)). The two
        // transitions do not overlap, so the mean is 1 - 0.14 / 2 - 0.38 / 2.
        constexpr float kCloudProfileMean = 0.74f;
        constexpr int kDensitySamples = 4096;
        constexpr float kInverseFourPi = 0.0795774715f;

        float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }

        // Mirrors valueNoise in CloudNoise.slang.
        float ValueNoise(glm::vec3 p)
        {
            const glm::vec3 cell = glm::floor(p);
            const glm::vec3 f = p - cell;
            const glm::vec3 w{Smooth(f.x), Smooth(f.y), Smooth(f.z)};
            const auto x = static_cast<std::int32_t>(cell.x);
            const auto y = static_cast<std::int32_t>(cell.y);
            const auto z = static_cast<std::int32_t>(cell.z);
            const auto corner = [&](int dx, int dy, int dz)
            { return CloudNoiseVolume::LatticeValue(x + dx, y + dy, z + dz); };
            const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
            return lerp(lerp(lerp(corner(0, 0, 0), corner(1, 0, 0), w.x), lerp(corner(0, 1, 0), corner(1, 1, 0), w.x), w.y),
                        lerp(lerp(corner(0, 0, 1), corner(1, 0, 1), w.x), lerp(corner(0, 1, 1), corner(1, 1, 1), w.x), w.y),
                        w.z);
        }

        // Mirrors baseCloudDensity and the detail erosion in densityLocal.
        float NoiseDensity(glm::vec3 p, float coverage, float erosion, float detailScaleRatio)
        {
            const float fbm = ValueNoise(p) * 0.55f + ValueNoise(p * 2.03f + glm::vec3(17.1f, 9.2f, 13.7f)) * 0.275f +
                              ValueNoise(p * 4.12f + 9.7f) * 0.1375f;
            float density = std::clamp((fbm - (1.0f - coverage)) / coverage, 0.0f, 1.0f);
            if (density > 0.001f && erosion > 0.001f)
                density = std::clamp(density - (1.0f - ValueNoise(p * detailScaleRatio + glm::vec3(31.4f, 7.8f, 19.2f))) *
                                                   erosion * (1.0f - density), 0.0f, 1.0f);
            return density;
        }

        float ComputeMeanNoiseDensity(float coverage, float erosion, float detailScaleRatio)
        {
            // An R3 low-discrepancy sequence over many lattice cells converges
            // far faster than random sampling for this smooth integrand.
            constexpr double g = 1.2207440846057596;
            const glm::dvec3 step{1.0 / g, 1.0 / (g * g), 1.0 / (g * g * g)};
            constexpr double span = CloudNoiseVolume::Size;
            double sum = 0.0;
            glm::dvec3 u{0.5};
            for (int sample = 0; sample < kDensitySamples; ++sample)
            {
                u = glm::fract(u + step);
                sum += NoiseDensity(glm::vec3(u * span), coverage, erosion, detailScaleRatio);
            }
            return static_cast<float>(sum / kDensitySamples);
        }

        // World-space vertical extent of the volume's unit box.
        float VolumeHeight(const glm::mat4 &worldToLocal)
        {
            const glm::mat4 localToWorld = glm::inverse(worldToLocal);
            return std::abs(localToWorld[0].y) + std::abs(localToWorld[1].y) + std::abs(localToWorld[2].y);
        }

        float SmoothStep(float edge0, float edge1, float x)
        {
            const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return Smooth(t);
        }
    }

    float EstimateMeanCloudNoiseDensity(float coverage, float detailErosion, float detailScaleRatio)
    {
        coverage = std::clamp(coverage, 0.001f, 1.0f);
        detailErosion = std::max(detailErosion, 0.0f);
        detailScaleRatio = std::max(detailScaleRatio, 0.0f);
        // Authored values change rarely; quantize so slider drags reuse entries.
        const auto key = std::make_tuple(std::lround(coverage * 1000.0f), std::lround(detailErosion * 1000.0f),
                                         std::lround(detailScaleRatio * 100.0f));
        static std::mutex mutex;
        static std::map<decltype(key), float> cache;
        {
            const std::lock_guard lock(mutex);
            if (const auto found = cache.find(key); found != cache.end())
                return found->second;
        }
        const float mean = ComputeMeanNoiseDensity(coverage, detailErosion, detailScaleRatio);
        const std::lock_guard lock(mutex);
        return cache.emplace(key, mean).first->second;
    }

    CloudOcclusionParameters BuildCloudOcclusion(std::span<const BasicPostProcessEffect> effects)
    {
        CloudOcclusionParameters result;
        std::size_t count = 0;
        for (auto effect = effects.rbegin(); effect != effects.rend() && count < result.volumes.size(); ++effect)
        {
            if (effect->type != BasicPostProcessEffectType::VolumetricCloud)
                continue;
            // Lanes follow BuildSceneAtmosphere's cloud packet.
            const auto &lanes = effect->parameters;
            const float coverage = lanes[0].w, density = lanes[1].w, extinction = lanes[2].w;
            const float baseScale = lanes[4].w, detailScale = lanes[5].x, erosion = lanes[5].y;
            if (coverage <= 0.0f || density <= 0.0f || extinction <= 0.0f || baseScale <= 0.0f)
                continue;
            const float meanExtinction = EstimateMeanCloudNoiseDensity(coverage, erosion, detailScale / baseScale) *
                                         kCloudProfileMean * density * extinction;
            if (meanExtinction <= 0.0f)
                continue;

            // Cloud-base radiance follows the cloud pass's lighting: its
            // ambient fill plus sunlight that reaches mid-cloud, scattered
            // isotropically because multiple scattering diffuses it.
            const glm::vec3 lightDirection = glm::dot(glm::vec3(lanes[2]), glm::vec3(lanes[2])) > 0.000001f
                                                 ? glm::normalize(glm::vec3(lanes[2])) : glm::vec3(0.0f, 1.0f, 0.0f);
            const float daylight = SmoothStep(-0.12f, 0.035f, lightDirection.y);
            const float ambientFill = lanes[4].z * (lanes[5].z > 0.5f ? 1.0f : daylight * daylight);
            const float sunDepth = meanExtinction * 0.5f * VolumeHeight(effect->worldToLocal) /
                                   std::max(lightDirection.y, 0.1f);
            const glm::vec3 sunlight = glm::max(glm::vec3(lanes[3]), glm::vec3(0.0f)) * std::max(lanes[3].w, 0.0f) *
                                       kInverseFourPi * std::exp(-sunDepth);
            const glm::vec3 baseRadiance = glm::max(glm::vec3(lanes[0]), glm::vec3(0.0f)) *
                                           std::max(lanes[4].x, 0.0f) * (glm::vec3(ambientFill) + sunlight);

            result.worldToLocal[count] = effect->worldToLocal;
            result.volumes[count] = glm::vec4(baseRadiance, meanExtinction);
            ++count;
        }
        return result;
    }
}
