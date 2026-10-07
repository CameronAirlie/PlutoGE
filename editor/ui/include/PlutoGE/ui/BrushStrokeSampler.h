#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace PlutoGE::ui
{
    // A stroke follows the sampled polyline in surface-local X/Z units. The
    // caller resets it on release, target/tool changes, or an invalid hit.
    // Stationary input intentionally paints only the initial dab.
    class BrushStrokeSampler
    {
    public:
        void Reset() { m_active = false; m_remaining = 0.0; }

        template <typename Emit>
        void Sample(glm::vec2 position, float spacing, Emit &&emit)
        {
            if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
                !std::isfinite(spacing) || spacing <= 0.0f)
            {
                Reset();
                return;
            }
            if (!m_active || spacing != m_spacing)
            {
                m_active = true;
                m_spacing = spacing;
                m_previous = position;
                m_remaining = spacing;
                emit(position);
                return;
            }

            const glm::dvec2 start(m_previous);
            const glm::dvec2 delta = glm::dvec2(position) - start;
            const double distance = glm::length(delta);
            m_previous = position;
            if (distance == 0.0) return;

            // A large discontinuity must not cause an unbounded editor stall.
            // Start a new segment instead of partially painting a long bridge.
            constexpr unsigned maxDabsPerSample = 256;
            if (distance / spacing > maxDabsPerSample)
            {
                m_remaining = spacing;
                emit(position);
                return;
            }
            double next = m_remaining;
            while (next <= distance)
            {
                emit(glm::vec2(start + delta * (next / distance)));
                next += spacing;
            }
            m_remaining = next - distance;
        }

    private:
        bool m_active = false;
        float m_spacing = 0.0f;
        glm::vec2 m_previous{0.0f};
        double m_remaining = 0.0;
    };
}
