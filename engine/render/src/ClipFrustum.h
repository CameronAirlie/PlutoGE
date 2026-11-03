#pragma once

#include "PlutoGE/render/BasicRenderer.h"

#include <glm/glm.hpp>

#include <array>
#include <cmath>

namespace PlutoGE::render
{
    // The six planes of a view-projection's clip volume (normals inward), for sphere tests against draws' bounds.
    struct ClipFrustum
    {
        struct Plane
        {
            glm::vec3 normal{0.0f};
            float distance = 0.0f;
        };

        std::array<Plane, 6> planes{};

        explicit ClipFrustum(const glm::mat4 &viewProjection)
        {
            const auto row = [&](int index)
            {
                return glm::vec4(viewProjection[0][index], viewProjection[1][index],
                                 viewProjection[2][index], viewProjection[3][index]);
            };
            const glm::vec4 row0 = row(0), row1 = row(1), row2 = row(2), row3 = row(3);
            const std::array equations{row3 + row0, row3 - row0, row3 + row1,
                                       row3 - row1, row2, row3 - row2};
            for (std::size_t index = 0; index < equations.size(); ++index)
            {
                const auto normal = glm::vec3(equations[index]);
                const float length = glm::length(normal);
                if (length > 0.000001f)
                    planes[index] = {normal / length, equations[index].w / length};
            }
        }

        [[nodiscard]] bool Intersects(const BasicDraw &draw) const
        {
            if (draw.shadowBoundsRadius < 0.0f || !std::isfinite(draw.shadowBoundsRadius))
                return true;
            for (const auto &plane : planes)
            {
                if (glm::dot(plane.normal, draw.shadowBoundsCenter) + plane.distance <
                    -draw.shadowBoundsRadius)
                    return false;
            }
            return true;
        }

        // Left, right, bottom and top only. These hold for any depth convention, so camera projections
        // (reversed-Z) can use them; the camera's draw distances stand in for the far plane.
        [[nodiscard]] bool SidesIntersect(const glm::vec3 &center, float radius) const
        {
            for (std::size_t index = 0; index < 4; ++index)
                if (glm::dot(planes[index].normal, center) + planes[index].distance < -radius)
                    return false;
            return true;
        }
    };
}
