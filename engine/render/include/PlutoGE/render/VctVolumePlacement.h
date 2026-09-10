#pragma once
#include <glm/glm.hpp>

namespace PlutoGE::render::detail
{
    inline glm::vec3 VctVolumeOrigin(const glm::vec3 &camera, float size, unsigned resolution,
                                     const glm::vec3 &publishedOrigin, bool valid)
    {
        const float step = size / float(resolution) * 8.0f;
        // Keep an overlapping range on both sides of a grid boundary. Small
        // camera movements must not alternate two expensive volume rebuilds.
        if (valid && glm::all(glm::lessThanEqual(glm::abs(camera -
            (publishedOrigin + glm::vec3(size * .5f))), glm::vec3(step))))
            return publishedOrigin;
        return glm::floor((camera - glm::vec3(size * .5f)) / step) * step;
    }
}
