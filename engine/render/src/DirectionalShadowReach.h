#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <optional>

namespace PlutoGE::render
{
    // Conservative region in which a directional-light caster can shadow a
    // receiver this camera shades. Receivers lie within the view frustum up to
    // the shadow distance, bounded by a sphere through its far corners. Casters
    // lie upstream of a receiver by at most the caster distance, so the region
    // is a capsule from the camera towards the light.
    class DirectionalShadowReach
    {
    public:
        // Empty when no bound is safe: orthographic or degenerate projections,
        // non-finite inputs or an undefined light direction.
        [[nodiscard]] static std::optional<DirectionalShadowReach> Create(
            const glm::vec3 &cameraPosition, const glm::mat4 &projection, const glm::vec3 &lightDirection,
            float shadowDistance, float casterDistance)
        {
            if (std::abs(projection[3][3]) > 0.5f || projection[0][0] == 0.0f || projection[1][1] == 0.0f)
                return std::nullopt;
            const float lightLength = glm::length(lightDirection);
            if (!(lightLength > 1.e-6f) || !std::isfinite(lightLength) ||
                !std::isfinite(shadowDistance) || !std::isfinite(casterDistance) ||
                shadowDistance <= 0.0f || casterDistance < 0.0f ||
                !Finite(cameraPosition))
                return std::nullopt;
            // A frustum point at view depth D lies at most D * corner from the eye.
            const float tanX = 1.0f / projection[0][0], tanY = 1.0f / projection[1][1];
            const float corner = std::sqrt(1.0f + tanX * tanX + tanY * tanY);
            DirectionalShadowReach reach;
            reach.m_start = cameraPosition;
            reach.m_upstream = -lightDirection / lightLength * casterDistance;
            reach.m_receiverRadius = shadowDistance * corner;
            if (!std::isfinite(reach.m_receiverRadius)) return std::nullopt;
            return reach;
        }

        // Unknown (non-positive or non-finite) bounds are always in reach.
        [[nodiscard]] bool Contains(const glm::vec3 &center, float radius) const noexcept
        {
            if (!(radius > 0.0f) || !std::isfinite(radius) || !Finite(center)) return true;
            const float lengthSquared = glm::dot(m_upstream, m_upstream);
            const float t = lengthSquared > 0.0f
                ? std::clamp(glm::dot(center - m_start, m_upstream) / lengthSquared, 0.0f, 1.0f) : 0.0f;
            const glm::vec3 offset = center - (m_start + m_upstream * t);
            const float limit = m_receiverRadius + radius;
            return glm::dot(offset, offset) <= limit * limit;
        }

    private:
        static bool Finite(const glm::vec3 &value) noexcept
        { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

        glm::vec3 m_start{0.0f}, m_upstream{0.0f};
        float m_receiverRadius = 0.0f;
    };
}
