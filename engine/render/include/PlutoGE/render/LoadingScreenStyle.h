#pragma once
#include <string>
#include <glm/vec3.hpp>

namespace PlutoGE::render
{
    // Empty title selects the neutral engine loading screen. No scene assets required.
    struct LoadingScreenStyle
    {
        std::string title;
        glm::vec3 accent{0.65f, 0.68f, 0.72f};
        // Optional project .plutoloading asset. Empty uses the built-in screen.
        std::string assetReference;
    };
}
