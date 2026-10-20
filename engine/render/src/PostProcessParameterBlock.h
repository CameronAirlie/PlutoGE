#pragma once

#include "PlutoGE/render/PostProcessLayout.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace PlutoGE::render
{
    // CPU mirror of PostProcessParameterBlock in PostProcessParameters.slang,
    // shared by every screen-space pass that binds it as a uniform buffer.
    struct alignas(16) BasicPostProcessParameters
    {
        float exposure = 1.0f;
        float gamma = 2.2f;
        std::uint32_t flipY = 0;
        std::uint32_t quality = 0;
        glm::vec2 inverseResolution{0.0f};
        float time = 0.0f;
        std::uint32_t zeroToOneDepth = 0;
        std::array<glm::vec4, PLUTO_POST_PROCESS_LANES> parameters{};
        glm::mat4 inverseViewProjection{1.0f};
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::vec4 cameraPosition{0.0f};
        glm::mat4 worldToLocal{1.0f};
    };
    static_assert(sizeof(BasicPostProcessParameters) == 304 + 16 * PLUTO_POST_PROCESS_LANES);
}
