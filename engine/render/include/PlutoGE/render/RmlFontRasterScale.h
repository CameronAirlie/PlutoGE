#pragma once

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    // Chooses the runtime UI's integer font atlas density (1-4x). Changing it
    // releases every font atlas and restyles every document, which costs tens
    // of milliseconds, so the requirement (the largest drawn canvas scale)
    // must not toggle a bucket while, for example, a distance-scaled world
    // label crosses a threshold. Density rises once text would be visibly
    // magnified, and falls only after the lower requirement has persisted.
    class RmlFontRasterScale
    {
    public:
        static constexpr int kMaximum = 4;
        // Glyphs may be magnified this much before the density rises.
        static constexpr float kRiseTolerance = 1.1f;
        static constexpr float kFallDelaySeconds = 3.0f;

        // Returns the density to apply for this frame's requirement.
        int Update(float required, float elapsedSeconds)
        {
            required = std::isfinite(required) ? std::clamp(required, 1.0f, float(kMaximum)) : 1.0f;
            const int needed = static_cast<int>(std::ceil(required));
            if (m_current == 0 || required > m_current * kRiseTolerance)
            {
                m_current = std::max(m_current, needed);
                m_lowerSeconds = 0.0f;
            }
            else if (needed < m_current)
            {
                m_lowerSeconds += std::max(elapsedSeconds, 0.0f);
                if (m_lowerSeconds >= kFallDelaySeconds)
                {
                    m_current = needed;
                    m_lowerSeconds = 0.0f;
                }
            }
            else
                m_lowerSeconds = 0.0f;
            return m_current;
        }

        int Current() const { return m_current; }

    private:
        int m_current = 0; // 0 until the first requirement.
        float m_lowerSeconds = 0.0f;
    };
}
