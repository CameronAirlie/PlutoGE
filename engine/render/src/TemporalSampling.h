#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace PlutoGE::render
{
    // Sampling policy for temporal reconstruction (native TAA and SDK
    // upscalers): how sharp the jittered source must be, and how much each
    // jittered sample should count. Pure functions, kept apart from the passes
    // so the policy can be tested without a device.

    // Material texture LOD bias for an SDK upscaler: AMD's recommended
    // log2(render / display) - 1, so reconstruction receives display-rate detail.
    [[nodiscard]] inline float UpscalerMipLodBias(std::uint32_t renderWidth, std::uint32_t outputWidth) noexcept
    {
        if (renderWidth == 0 || outputWidth == 0)
            return 0.0f;
        return std::clamp(std::log2(static_cast<float>(renderWidth) / static_cast<float>(outputWidth)) - 1.0f,
                          -4.0f, 0.0f);
    }

    // Material texture LOD bias for native TAA. Accumulating samples jittered
    // across the pixel integrates a pixel-sized box over textures that mip
    // selection has already filtered for that footprint; half a level restores
    // the detail the second filter removes. No jitter, no supersampling, no bias.
    [[nodiscard]] inline float NativeTaaMipLodBias(float jitterStrength) noexcept
    {
        return -0.5f * std::clamp(jitterStrength, 0.0f, 1.0f);
    }

    // Gaussian fit to the Blackman-Harris reconstruction filter, in pixels^-2.
    inline constexpr float TaaSampleFilterSharpness = 2.29f;

    // How much this frame's sample should count in native TAA, relative to an
    // average frame. A jittered sample lies off the output pixel's centre;
    // weighting it by its distance gives a reconstruction filter narrower than
    // the pixel-sized box that equal weights converge to. The weight is
    // normalised to a mean of one over the jitter sequence (Halton, uniform
    // over [-strength/2, strength/2]^2), so convergence and ghosting behave as
    // with equal weights on average.
    [[nodiscard]] inline float TaaCurrentSampleWeight(glm::vec2 jitterPixels, float jitterStrength) noexcept
    {
        const float a = TaaSampleFilterSharpness;
        const float halfExtent = 0.5f * std::max(jitterStrength, 0.0f);
        // Mean of exp(-a x^2) over x uniform in [-h, h]: sqrt(pi / a) erf(h sqrt(a)) / 2h.
        const float axisMean = halfExtent > 1.0e-4f
            ? std::sqrt(std::numbers::pi_v<float> / a) * std::erf(halfExtent * std::sqrt(a)) / (2.0f * halfExtent)
            : 1.0f;
        return std::exp(-a * glm::dot(jitterPixels, jitterPixels)) / (axisMean * axisMean);
    }
}
