#include "PlutoGE/render/DynamicResolution.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        constexpr float kSmoothing = 0.2f;
        constexpr float kSameScale = 1.0e-4f;
        // The pixel share is never trusted below this: some cost always scales with pixels on a pixel-bound GPU,
        // and a noisy estimate near zero would predict no gain from lowering the scale.
        constexpr float kMinimumPixelShare = 0.25f;
    }

    DynamicResolutionController::DynamicResolutionController(DynamicResolutionSettings settings) : m_settings(settings)
    {
        m_settings.minimumScale = std::clamp(m_settings.minimumScale, 0.1f, 1.0f);
        m_settings.maximumScale = std::clamp(m_settings.maximumScale, m_settings.minimumScale, 1.0f);
        m_settings.scaleStep = std::clamp(m_settings.scaleStep, 0.01f, 0.5f);
        m_scale = m_settings.maximumScale;
        m_smoothedScale = m_scale;
    }

    float DynamicResolutionController::Quantise(float scale) const noexcept
    {
        const float steps = std::round((m_settings.maximumScale - scale) / m_settings.scaleStep);
        return std::clamp(m_settings.maximumScale - steps * m_settings.scaleStep, m_settings.minimumScale,
                          m_settings.maximumScale);
    }

    float DynamicResolutionController::PredictGpuMs(float scale) const noexcept
    {
        if (!m_smoothedMs)
            return 0.0f;
        const float pixels = (scale * scale) / (m_smoothedScale * m_smoothedScale);
        return *m_smoothedMs * ((1.0f - m_pixelShare) + m_pixelShare * pixels);
    }

    void DynamicResolutionController::Reset(float scale)
    {
        m_scale = Quantise(scale);
        m_smoothedMs.reset();
        m_smoothedScale = m_scale;
        m_otherLevel.reset();
        m_pixelShare = 1.0f;
        m_over = m_under = m_cooldown = 0;
    }

    float DynamicResolutionController::Observe(float gpuMs, float renderedScale)
    {
        if (!m_settings.enabled || !std::isfinite(gpuMs) || gpuMs <= 0.0f || !std::isfinite(renderedScale) ||
            renderedScale <= 0.0f)
            return m_scale;

        // Results arrive a frame or two after the scale changed: a frame at another scale starts a new estimate,
        // and the old one becomes the second point of the fixed + per-pixel cost model.
        if (!m_smoothedMs || std::abs(renderedScale - m_smoothedScale) > kSameScale)
        {
            if (m_smoothedMs)
                m_otherLevel = std::pair{m_smoothedScale, *m_smoothedMs};
            m_smoothedMs = gpuMs;
            m_smoothedScale = renderedScale;
        }
        else
            m_smoothedMs = *m_smoothedMs + (gpuMs - *m_smoothedMs) * kSmoothing;
        if (m_otherLevel && std::abs(m_otherLevel->first - m_smoothedScale) > kSameScale)
        {
            const bool currentHigher = m_smoothedScale > m_otherLevel->first;
            const float highScale = currentHigher ? m_smoothedScale : m_otherLevel->first;
            const float lowScale = currentHigher ? m_otherLevel->first : m_smoothedScale;
            const float highMs = currentHigher ? *m_smoothedMs : m_otherLevel->second;
            const float lowMs = currentHigher ? m_otherLevel->second : *m_smoothedMs;
            if (highMs > lowMs)
            {
                const float perUnitPixels = (highMs - lowMs) / (highScale * highScale - lowScale * lowScale);
                m_pixelShare = std::clamp(perUnitPixels * m_smoothedScale * m_smoothedScale / *m_smoothedMs,
                                          kMinimumPixelShare, 1.0f);
            }
        }

        if (m_cooldown > 0)
        {
            --m_cooldown;
            return m_scale;
        }
        // Decide only from frames rendered at the scale currently requested.
        if (std::abs(renderedScale - m_scale) > kSameScale)
            return m_scale;

        const float target = std::max(m_settings.targetGpuMs, 0.1f);
        m_over = *m_smoothedMs > target ? m_over + 1 : 0;
        if (m_over >= std::max(m_settings.lowerAfter, 1u) && m_scale > m_settings.minimumScale)
        {
            // The largest level predicted to fit, or the minimum.
            float next = m_settings.minimumScale;
            for (float candidate = Quantise(m_scale - m_settings.scaleStep); candidate >= m_settings.minimumScale;
                 candidate = Quantise(candidate - m_settings.scaleStep))
            {
                if (PredictGpuMs(candidate) <= target)
                {
                    next = candidate;
                    break;
                }
                if (candidate <= m_settings.minimumScale)
                    break;
            }
            m_scale = next;
            m_over = m_under = 0;
            m_cooldown = m_settings.cooldown;
            return m_scale;
        }
        const float raised = Quantise(m_scale + m_settings.scaleStep);
        m_under = raised > m_scale && PredictGpuMs(raised) <= target * m_settings.raiseHeadroom ? m_under + 1 : 0;
        if (m_under >= std::max(m_settings.raiseAfter, 1u))
        {
            m_scale = raised;
            m_over = m_under = 0;
            m_cooldown = m_settings.cooldown;
        }
        return m_scale;
    }

    std::uint32_t ScaledRenderExtent(std::uint32_t extent, float scale, std::uint32_t alignment) noexcept
    {
        alignment = std::max(alignment, 1u);
        if (extent <= alignment || !(scale < 1.0f))
            return extent;
        const auto scaled = static_cast<std::uint32_t>(std::lround(extent * std::max(scale, 0.0f) / alignment)) * alignment;
        return std::clamp(scaled, alignment, extent);
    }
}
