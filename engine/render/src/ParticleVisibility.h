#pragma once

#include <array>
#include <glm/glm.hpp>

namespace PlutoGE::render
{
    // CameraData uses [-1, 1] clip depth, independently of the rendering API.
    // Test live particle bounds, not emitter origins: particles can travel far
    // from their source, and large billboards can straddle the viewport edge.
    class ParticleVisibility
    {
    public:
        explicit ParticleVisibility(const glm::mat4 &viewProjection)
        {
            const auto rows = glm::transpose(viewProjection);
            m_planes = {rows[3] + rows[0], rows[3] - rows[0],
                        rows[3] + rows[1], rows[3] - rows[1],
                        rows[3] + rows[2], rows[3] - rows[2]};
            for (auto &plane : m_planes)
            {
                const float length = glm::length(glm::vec3(plane));
                if (length > 0.000001f)
                    plane /= length;
            }
        }

        [[nodiscard]] bool IsVisible(glm::vec3 center, float radius) const
        {
            for (const auto &plane : m_planes)
                if (glm::dot(plane, glm::vec4(center, 1.0f)) < -radius)
                    return false;
            return true;
        }

    private:
        std::array<glm::vec4, 6> m_planes;
    };
}
