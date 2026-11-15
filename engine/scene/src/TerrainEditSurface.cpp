#include "PlutoGE/scene/TerrainEditSurface.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <algorithm>
#include <limits>

namespace PlutoGE::scene
{
    namespace
    {
        bool Usable(const TerrainComponent *terrain)
        {
            return terrain && terrain->IsEnabled() && terrain->GetOwner() && terrain->GetOwner()->IsActive();
        }

        // The terrain's footprint in the world's x, z plane.
        std::pair<glm::vec2, glm::vec2> Footprint(const TerrainComponent &terrain)
        {
            const glm::mat4 world = terrain.GetOwner()->GetWorldTransform();
            const glm::vec2 extent(static_cast<float>(terrain.GetWidth() - 1) * terrain.GetCellSize(),
                                   static_cast<float>(terrain.GetDepth() - 1) * terrain.GetCellSize());
            glm::vec2 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
            for (const glm::vec2 corner : {glm::vec2(0.0f), glm::vec2(extent.x, 0.0f), glm::vec2(0.0f, extent.y), extent})
            {
                const glm::vec4 point = world * glm::vec4(corner.x, 0.0f, corner.y, 1.0f);
                low = glm::min(low, glm::vec2(point.x, point.z));
                high = glm::max(high, glm::vec2(point.x, point.z));
            }
            return {low, high};
        }
    }

    TerrainEditSurface::TerrainEditSurface(std::vector<TerrainComponent *> terrains) : m_terrains(std::move(terrains))
    {
        std::erase(m_terrains, nullptr);
    }

    std::optional<TerrainEditSurface::Hit> TerrainEditSurface::Raycast(const glm::vec3 &origin, const glm::vec3 &direction) const
    {
        std::optional<Hit> nearest;
        float nearestDistance = std::numeric_limits<float>::max();
        for (auto *terrain : m_terrains)
        {
            glm::vec3 point{0.0f};
            if (!Usable(terrain))
                continue;
            terrain->GetMeshForBaking(); // the ray walks the built surface
            if (!terrain->Raycast(origin, direction, point))
                continue;
            const float distance = glm::distance(origin, point);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = Hit{point, terrain};
            }
        }
        return nearest;
    }

    std::vector<TerrainComponent *> TerrainEditSurface::TerrainsNear(const glm::vec3 &worldPosition, float radius) const
    {
        std::vector<TerrainComponent *> near;
        for (auto *terrain : m_terrains)
        {
            if (!Usable(terrain))
                continue;
            const auto [low, high] = Footprint(*terrain);
            const glm::vec2 point(worldPosition.x, worldPosition.z);
            const glm::vec2 outside = glm::max(glm::max(low - point, point - high), glm::vec2(0.0f));
            if (glm::length(outside) <= radius)
                near.push_back(terrain);
        }
        return near;
    }

    std::vector<TerrainEditChange> TerrainEditSurface::Sculpt(const glm::vec3 &worldPosition, const TerrainSculptBrush &brush, float deltaTime)
    {
        std::vector<TerrainEditChange> changes;
        for (auto *terrain : TerrainsNear(worldPosition, brush.radius))
        {
            auto local = brush;
            // The brush's flatten height is a world height; each terrain takes it in its own space.
            const glm::mat4 toLocal = glm::inverse(terrain->GetOwner()->GetWorldTransform());
            local.flattenHeight = glm::vec3(toLocal * glm::vec4(worldPosition.x, brush.flattenHeight, worldPosition.z, 1.0f)).y;
            if (const auto rect = terrain->Sculpt(worldPosition, local, deltaTime); !rect.Empty())
                changes.push_back({terrain, rect, {}, false});
        }
        return changes;
    }

    std::vector<TerrainEditChange> TerrainEditSurface::PaintHoles(const glm::vec3 &worldPosition, const TerrainHoleBrush &brush)
    {
        std::vector<TerrainEditChange> changes;
        for (auto *terrain : TerrainsNear(worldPosition, brush.radius))
            if (terrain->PaintHoles(worldPosition, brush))
                changes.push_back({terrain, {}, {}, true});
        return changes;
    }

    std::vector<TerrainEditChange> TerrainEditSurface::PaintLayer(const glm::vec3 &worldPosition, const TerrainPaintBrush &brush)
    {
        std::vector<TerrainEditChange> changes;
        for (auto *terrain : TerrainsNear(worldPosition, brush.radius))
            if (const auto rect = terrain->PaintLayer(worldPosition, brush); !rect.Empty())
                changes.push_back({terrain, {}, rect, false});
        return changes;
    }
}
