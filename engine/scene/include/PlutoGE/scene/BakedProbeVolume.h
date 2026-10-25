#pragma once
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <cstddef>

namespace PlutoGE::scene
{
    struct BakedProbeVolume
    {
        glm::vec3 origin{0.0f};
        glm::vec3 size{1.0f};
        // Unit-length transformed grid axes; may include inherited shear.
        glm::mat3 gridBasis{1.0f};
        glm::vec3 GridToWorld(glm::vec3 uvw) const { return origin+gridBasis*(uvw*size); }
        glm::vec3 WorldToGrid(glm::vec3 position) const { return (glm::inverse(gridBasis)*(position-origin))/size; }
        glm::ivec3 resolution{0};
        std::vector<glm::vec3> irradiance;
        // Optional directional grid data. Old RGB-only scenes remain readable.
        std::vector<glm::vec4> positions; // xyz relocated position, w validity
        std::vector<std::array<glm::vec3, 9>> sphericalHarmonics;
        std::vector<std::array<float, 64>> visibility; // 8x8 octahedral ray distances
        std::vector<std::array<glm::vec3, 64>> visibilityNormals; // Geometric hit normals; zero for misses
        // Authoring setting mirrored from the owning component, not bake output:
        // world distance over which this volume fades into the next one.
        float blendDistance = 1.0f;
        bool HasDirectionalData() const {
            return IsValid() && positions.size() == irradiance.size() &&
                sphericalHarmonics.size() == irradiance.size() && visibility.size() == irradiance.size();
        }

        [[nodiscard]] bool IsValid() const
        {
            return resolution.x > 0 && resolution.y > 0 && resolution.z > 0 &&
                   irradiance.size() == static_cast<std::size_t>(resolution.x * resolution.y * resolution.z);
        }
    };

}
