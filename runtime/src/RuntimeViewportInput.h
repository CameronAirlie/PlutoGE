#pragma once

#include <glm/glm.hpp>
#include <cmath>

namespace PlutoGE
{
    struct RuntimeViewportInput
    {
        glm::vec2 canvasSize{};
        glm::vec2 canvasPointer{-1.0f};
        bool pointerInside = false;
        glm::vec4 logicalViewport{};
    };

    inline RuntimeViewportInput MapRuntimeViewportInput(glm::vec2 framebufferSize,
                                                       glm::vec2 logicalSize,
                                                       glm::dvec2 pointer, bool focused)
    {
        RuntimeViewportInput result;
        result.canvasSize = glm::max(framebufferSize, glm::vec2(0));
        if (logicalSize.x <= 0 || logicalSize.y <= 0 ||
            framebufferSize.x <= 0 || framebufferSize.y <= 0)
            return result;
        result.logicalViewport = {0, 0, logicalSize.x, logicalSize.y};
        result.pointerInside = focused && std::isfinite(pointer.x) && std::isfinite(pointer.y) &&
                               pointer.x >= 0 && pointer.y >= 0 &&
                               pointer.x < logicalSize.x && pointer.y < logicalSize.y;
        if (result.pointerInside)
        {
            const glm::dvec2 normalized = pointer / glm::dvec2(logicalSize);
            // UI layout uses framebuffer pixels with a bottom-left origin;
            // live aiming uses the logical, top-left viewport bounds above.
            result.canvasPointer = {normalized.x * framebufferSize.x,
                                    (1.0 - normalized.y) * framebufferSize.y};
        }
        return result;
    }
}
