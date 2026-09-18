#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <glm/glm.hpp>

namespace PlutoGE::ui
{
    struct ViewportPickRay
    {
        glm::vec3 origin{0.0f};
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
    };

    inline std::optional<ViewportPickRay> TransformViewportPickRay(
        const ViewportPickRay &ray, const glm::mat4 &worldTransform)
    {
        const glm::mat4 inverseWorld = glm::inverse(worldTransform);
        ViewportPickRay local{
            glm::vec3(inverseWorld * glm::vec4(ray.origin, 1.0f)),
            glm::vec3(inverseWorld * glm::vec4(ray.direction, 0.0f))};
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(local.origin[axis]) || !std::isfinite(local.direction[axis]))
                return std::nullopt;

        // Inverse scaling shortens a valid ray: a 300x object produces a
        // direction of length 1/300. An absolute squared-length epsilon would
        // incorrectly reject it. Rescale first to avoid under/overflow as well.
        const glm::vec3 magnitude = glm::abs(local.direction);
        const float largest = std::max({magnitude.x, magnitude.y, magnitude.z});
        if (largest == 0.0f)
            return std::nullopt;
        local.direction = glm::normalize(local.direction / largest);
        return local;
    }
}
