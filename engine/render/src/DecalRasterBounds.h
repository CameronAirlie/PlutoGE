#pragma once
#include "PlutoGE/render/rhi/Types.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    inline rhi::Scissor DecalRasterBounds(const glm::mat4 &clipModel,
        std::uint32_t width, std::uint32_t height, bool flipY)
    {
        glm::vec2 low(1), high(-1);
        bool front = false, behind = false;
        for (unsigned corner = 0; corner < 8; ++corner)
        {
            const auto clip = clipModel * glm::vec4((corner & 1) ? .5f : -.5f,
                (corner & 2) ? .5f : -.5f, (corner & 4) ? .5f : -.5f, 1);
            if (clip.w <= .0001f) { behind = true; continue; }
            front = true;
            glm::vec2 ndc = glm::vec2(clip) / clip.w;
            if (flipY) ndc.y = -ndc.y;
            low = glm::min(low, ndc); high = glm::max(high, ndc);
        }
        if (!front) return {};
        // Crossing the eye plane requires conservative coverage.
        if (behind) return {0, 0, width, height};
        const glm::vec2 size(width, height);
        low = glm::clamp(glm::floor((low * .5f + .5f) * size) - 1.0f, glm::vec2(0), size);
        high = glm::clamp(glm::ceil((high * .5f + .5f) * size) + 1.0f, glm::vec2(0), size);
        return {static_cast<std::int32_t>(low.x), static_cast<std::int32_t>(low.y),
            static_cast<std::uint32_t>(high.x - low.x), static_cast<std::uint32_t>(high.y - low.y)};
    }
}
