#pragma once

#include "PlutoGE/scene/TerrainBrush.h"
#include "PlutoGE/scene/TerrainHeightData.h"
#include "PlutoGE/scene/TerrainWeightMap.h"

#include <glm/glm.hpp>

#include <optional>
#include <vector>

namespace PlutoGE::scene
{
    class TerrainComponent;

    // What one dab changed on one terrain.
    struct TerrainEditChange
    {
        TerrainComponent *terrain = nullptr;
        TerrainSampleRect heights;
        TerrainWeightRect weights;
        bool holes = false;
    };

    // The terrains a brush acts on: one terrain, or every loaded tile of a terrain world. Brushes are applied in world
    // space to each terrain they overlap, so a stroke across a tile edge edits both tiles alike; tools treat a lone
    // terrain and a world the same way.
    class TerrainEditSurface
    {
    public:
        TerrainEditSurface() = default;
        explicit TerrainEditSurface(std::vector<TerrainComponent *> terrains);

        [[nodiscard]] const std::vector<TerrainComponent *> &Terrains() const { return m_terrains; }
        [[nodiscard]] bool Empty() const { return m_terrains.empty(); }

        struct Hit
        {
            glm::vec3 position{0.0f};
            TerrainComponent *terrain = nullptr;
        };
        // The nearest terrain surface along a world-space ray.
        [[nodiscard]] std::optional<Hit> Raycast(const glm::vec3 &origin, const glm::vec3 &direction) const;
        // The terrains whose footprint a circle of `radius` around a world position touches.
        [[nodiscard]] std::vector<TerrainComponent *> TerrainsNear(const glm::vec3 &worldPosition, float radius) const;

        // One dab on every terrain the brush reaches. Flatten heights are world heights here.
        std::vector<TerrainEditChange> Sculpt(const glm::vec3 &worldPosition, const TerrainSculptBrush &brush, float deltaTime);
        std::vector<TerrainEditChange> PaintHoles(const glm::vec3 &worldPosition, const TerrainHoleBrush &brush);
        std::vector<TerrainEditChange> PaintLayer(const glm::vec3 &worldPosition, const TerrainPaintBrush &brush);

    private:
        std::vector<TerrainComponent *> m_terrains;
    };
}
