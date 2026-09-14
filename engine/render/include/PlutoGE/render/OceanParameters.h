#pragma once
#include <array>
#include <glm/glm.hpp>
namespace PlutoGE::render
{
    // std140-compatible ocean packet shared by the OpenGL and Vulkan backends.
    struct OceanParameters
    {
        glm::mat4 localToWorld{1};
        glm::vec4 shallowOpacity{}, deepSmoothness{}, foamVisibility{}, waves{};
        glm::vec4 underwater{}, surface{}, sunDirectionTime{}, sunColorIntensity{};
        glm::ivec4 mask{};
        std::array<glm::ivec4, 8> areaCounts{};
        std::array<glm::vec4, 256> points{};
        std::array<glm::vec4, 8> waveShape{}, waveMotion{};
        glm::vec4 crestFoam{}, detail{}, flow{};
    };
    static_assert(sizeof(OceanParameters) == 4736);
}
