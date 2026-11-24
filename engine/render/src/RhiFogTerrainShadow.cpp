#include "RhiFogTerrainShadow.h"

#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace PlutoGE::render
{
    std::shared_ptr<const FogTerrainShadow> FogTerrainShadowBuilder::Update(const scene::Scene &scene,
                                                                            const glm::vec3 &directionToSun)
    {
        std::vector<TerrainKey> terrains;
        for (const auto *terrain : scene.GetTerrainComponents())
        {
            const auto *owner = terrain ? terrain->GetOwner() : nullptr;
            if (!owner || !terrain->IsEnabled() || !owner->IsActiveInHierarchy() || terrain->GetHeightSamples().empty())
                continue;
            terrains.push_back({terrain, terrain->GetHeightsRevision(), terrain->GetHolesRevision(), owner->GetWorldTransform()});
        }
        if (terrains.empty() || !(directionToSun.y > 0.0f))
            return nullptr;

        // A change waits until the set has settled, while the previous heights keep the fog shadowed.
        bool resample = false;
        if (terrains == m_terrains)
            m_pendingTerrains.clear();
        else if (!m_shadow)
            resample = true;
        else if (terrains != m_pendingTerrains)
        {
            m_pendingTerrains = terrains;
            m_pendingFrames = 0;
        }
        else
            resample = ++m_pendingFrames >= kSettleFrames;
        if (resample)
        {
            core::CpuScope scope("Fog terrain shadow heights", core::CpuCategory::Rendering);
            m_pendingTerrains.clear();
            m_terrains = terrains;
            // The terrains' world XZ footprint, and the finest of their cells.
            glm::vec2 lower(std::numeric_limits<float>::max()), upper(std::numeric_limits<float>::lowest());
            float finestCell = std::numeric_limits<float>::max();
            for (const auto &key : terrains)
            {
                const float sizeX = static_cast<float>(key.terrain->GetWidth() - 1) * key.terrain->GetCellSize();
                const float sizeZ = static_cast<float>(key.terrain->GetDepth() - 1) * key.terrain->GetCellSize();
                for (const glm::vec2 corner : {glm::vec2(0.0f), glm::vec2(sizeX, 0.0f), glm::vec2(0.0f, sizeZ), glm::vec2(sizeX, sizeZ)})
                {
                    const glm::vec3 world = glm::vec3(key.world * glm::vec4(corner.x, 0.0f, corner.y, 1.0f));
                    lower = glm::min(lower, glm::vec2(world.x, world.z));
                    upper = glm::max(upper, glm::vec2(world.x, world.z));
                }
                const float scale = glm::length(glm::vec3(key.world[0]));
                finestCell = std::min(finestCell, key.terrain->GetCellSize() * std::max(scale, 1.0e-3f));
            }
            m_extent = std::max(std::max(upper.x - lower.x, upper.y - lower.y), 1.0e-3f);
            m_origin = lower;
            m_resolution = std::clamp(static_cast<int>(std::ceil(m_extent / std::max(finestCell, 1.0e-3f))) + 1, 2, kMaxResolution);
            const float texel = m_extent / static_cast<float>(m_resolution);

            m_heights.assign(static_cast<std::size_t>(m_resolution) * static_cast<std::size_t>(m_resolution), kFogTerrainNoHeight);
            for (const auto &key : terrains)
            {
                const glm::mat4 toLocal = glm::inverse(key.world);
                // Only the texels over this terrain's footprint (with a texel of margin) can sample it: a world's tiles
                // each cover a small part of the grid.
                const float sizeX = static_cast<float>(key.terrain->GetWidth() - 1) * key.terrain->GetCellSize();
                const float sizeZ = static_cast<float>(key.terrain->GetDepth() - 1) * key.terrain->GetCellSize();
                glm::vec2 footprintLower(std::numeric_limits<float>::max()), footprintUpper(std::numeric_limits<float>::lowest());
                for (const glm::vec2 corner : {glm::vec2(0.0f), glm::vec2(sizeX, 0.0f), glm::vec2(0.0f, sizeZ), glm::vec2(sizeX, sizeZ)})
                {
                    const glm::vec3 world = glm::vec3(key.world * glm::vec4(corner.x, 0.0f, corner.y, 1.0f));
                    footprintLower = glm::min(footprintLower, glm::vec2(world.x, world.z));
                    footprintUpper = glm::max(footprintUpper, glm::vec2(world.x, world.z));
                }
                const auto texelRange = [&](float lowerWorld, float upperWorld, float origin) {
                    const int first = static_cast<int>(std::floor((lowerWorld - origin) / texel - 0.5f)) - 1;
                    const int last = static_cast<int>(std::ceil((upperWorld - origin) / texel - 0.5f)) + 1;
                    return std::pair{std::clamp(first, 0, m_resolution - 1), std::clamp(last, 0, m_resolution - 1)};
                };
                const auto [firstX, lastX] = texelRange(footprintLower.x, footprintUpper.x, m_origin.x);
                const auto [firstZ, lastZ] = texelRange(footprintLower.y, footprintUpper.y, m_origin.y);
                for (int z = firstZ; z <= lastZ; ++z)
                    for (int x = firstX; x <= lastX; ++x)
                    {
                        const glm::vec2 worldXz = m_origin + (glm::vec2(static_cast<float>(x), static_cast<float>(z)) + 0.5f) * texel;
                        const glm::vec3 local = glm::vec3(toLocal * glm::vec4(worldXz.x, 0.0f, worldXz.y, 1.0f));
                        float height = 0.0f;
                        glm::vec3 normal(0.0f, 1.0f, 0.0f);
                        if (!key.terrain->TrySampleSurface(local.x, local.z, height, normal))
                            continue;
                        const float worldHeight = glm::vec3(key.world * glm::vec4(local.x, height, local.z, 1.0f)).y;
                        float &cell = m_heights[static_cast<std::size_t>(z) * static_cast<std::size_t>(m_resolution) + static_cast<std::size_t>(x)];
                        cell = std::max(cell, worldHeight);
                    }
            }
            m_heightsChanged = true;
        }

        // The sun moves continuously in a day cycle; a sweep per 0.1 degree keeps that cheap and invisible.
        const glm::vec3 sun = glm::normalize(directionToSun);
        if (m_heightsChanged || !m_shadow || glm::dot(sun, m_sunDirection) < 0.9999985f)
        {
            core::CpuScope scope("Fog terrain shadow sweep", core::CpuCategory::Rendering);
            auto shadow = std::make_shared<FogTerrainShadow>();
            shadow->origin = m_origin;
            shadow->extent = m_extent;
            shadow->resolution = m_resolution;
            shadow->shadowHeights = ComputeFogTerrainShadowHeights(m_heights, m_resolution, m_extent / static_cast<float>(m_resolution), sun);
            shadow->revision = ++m_revision;
            m_shadow = std::move(shadow);
            m_sunDirection = sun;
            m_heightsChanged = false;
        }
        return m_shadow;
    }
}
