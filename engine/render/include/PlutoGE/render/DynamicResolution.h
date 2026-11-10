#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// Automatic render resolution from measured GPU time.
//
// A frame's GPU cost on a pixel-bound GPU scales with the pixels it shades, so the cheapest way to hold a frame rate
// is to shade fewer of them and let the temporal upscaler (or the display scale) restore the output size. The
// controller here only decides a scale; RhiSceneRenderer applies it to the render size. It is a pure object so the
// policy can be tested, tuned or replaced (IDynamicResolutionPolicy) without a device.
namespace PlutoGE::render
{
    struct DynamicResolutionSettings
    {
        bool enabled = false;
        // GPU time per frame to hold (16.6 ms for 60 FPS).
        float targetGpuMs = 16.6f;
        // Scale range applied to both axes of the render size the renderer would otherwise use (native, or the
        // temporal upscaler's quality mode). 1 never renders above it.
        float minimumScale = 0.5f;
        float maximumScale = 1.0f;
        // Scales are quantised to this step, so render targets are rebuilt at most once per change of level.
        float scaleStep = 0.05f;
        // Raise the scale only when the predicted cost at the next level stays below this fraction of the target.
        float raiseHeadroom = 0.85f;
        // Observations above the target needed before lowering, below the headroom before raising, and to wait
        // after any change (render targets and history rebuild on a change, so changes must be rare).
        std::uint32_t lowerAfter = 4;
        std::uint32_t raiseAfter = 45;
        std::uint32_t cooldown = 30;

        auto operator<=>(const DynamicResolutionSettings &) const = default;
    };

    // Decides the next scale from GPU time observations. Implementations must be deterministic for a sequence of
    // observations, so behaviour can be tested and reproduced from captured timings.
    class IDynamicResolutionPolicy
    {
    public:
        virtual ~IDynamicResolutionPolicy() = default;
        // The scale to render the next frame at, after observing `gpuMs` for a frame rendered at `renderedScale`.
        [[nodiscard]] virtual float Observe(float gpuMs, float renderedScale) = 0;
        [[nodiscard]] virtual float Scale() const noexcept = 0;
        virtual void Reset(float scale) = 0;
    };

    // The default policy: a smoothed cost estimate per pixel, predicted at each quantised level.
    //  - Over the target for `lowerAfter` observations: jump straight to the largest level predicted to fit.
    //  - Predicted to fit the next level up with `raiseHeadroom` for `raiseAfter` observations: raise one level.
    //  - Either change starts a `cooldown` during which observations only update the estimate.
    // Cost is modelled as fixed + per-pixel, with the per-pixel share estimated from observations at different
    // scales; until two scales have been seen it assumes all cost scales with pixels.
    class DynamicResolutionController final : public IDynamicResolutionPolicy
    {
    public:
        explicit DynamicResolutionController(DynamicResolutionSettings settings = {});

        [[nodiscard]] float Observe(float gpuMs, float renderedScale) override;
        [[nodiscard]] float Scale() const noexcept override { return m_scale; }
        void Reset(float scale) override;
        [[nodiscard]] const DynamicResolutionSettings &Settings() const noexcept { return m_settings; }
        // Predicted GPU time at `scale` from the current estimate (for diagnostics and tests).
        [[nodiscard]] float PredictGpuMs(float scale) const noexcept;

    private:
        [[nodiscard]] float Quantise(float scale) const noexcept;

        DynamicResolutionSettings m_settings;
        float m_scale = 1.0f;
        // Smoothed observations: cost at the scale the frame was rendered at.
        std::optional<float> m_smoothedMs;
        float m_smoothedScale = 1.0f;
        // Fraction of cost that scales with pixels, learnt from observations at two or more scales.
        float m_pixelShare = 1.0f;
        std::optional<std::pair<float, float>> m_otherLevel; // (scale, smoothed ms) seen before the last change
        std::uint32_t m_over = 0, m_under = 0, m_cooldown = 0;
    };

    // Quantises a render extent so sizes are multiples of `alignment` pixels and never exceed `maximum`.
    [[nodiscard]] std::uint32_t ScaledRenderExtent(std::uint32_t extent, float scale, std::uint32_t alignment = 8) noexcept;
}
