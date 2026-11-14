#pragma once

#include "PlutoGE/render/FogTerrainShadow.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace PlutoGE::scene
{
    class Scene;
    class TerrainComponent;
}

namespace PlutoGE::render
{
    // Keeps a scene's FogTerrainShadow current: the terrains' heights are resampled into the grid when a terrain
    // changes (heights, holes, transform, enabled), and the shadow heights are swept again when the sun moves.
    class FogTerrainShadowBuilder
    {
    public:
        // Texels per side at most; the grid otherwise follows the finest terrain cell size.
        static constexpr int kMaxResolution = 256;

        // Null when the scene has no enabled terrain or the sun is below the horizon.
        [[nodiscard]] std::shared_ptr<const FogTerrainShadow> Update(const scene::Scene &scene, const glm::vec3 &directionToSun);

    private:
        struct TerrainKey
        {
            const scene::TerrainComponent *terrain = nullptr;
            std::uint64_t heightsRevision = 0, holesRevision = 0;
            glm::mat4 world{1.0f};
            bool operator==(const TerrainKey &) const = default;
        };

        std::vector<TerrainKey> m_terrains;
        std::vector<float> m_heights;
        glm::vec2 m_origin{0.0f};
        float m_extent = 0.0f;
        int m_resolution = 0;
        bool m_heightsChanged = false;
        glm::vec3 m_sunDirection{0.0f};
        std::shared_ptr<FogTerrainShadow> m_shadow;
        std::uint64_t m_revision = 0;
    };
}
