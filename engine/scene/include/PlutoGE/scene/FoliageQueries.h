#pragma once

#include "PlutoGE/scene/Entity.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    class Scene;

    // Spatial queries over every foliage component in a scene at once: hand-painted foliage, prefab foliage and the
    // tiles a TerrainWorld streams in all answer the same query, so gameplay need not know how a world is split up.
    //
    // A query names what it wants by type policy rather than by type name. An interaction tag
    // (FoliageTypeAsset::interactionTags) asks for "anything the player can gather here"; requireProxy asks for
    // instances that can become entities. Further criteria belong in FoliageInstanceQuery, so callers keep one entry
    // point as the policy grows.
    struct FoliageInstanceQuery
    {
        glm::vec3 center{0.0f};
        float radius = 0.0f;
        // Only types carrying this interaction tag; empty accepts every type.
        std::string interactionTag;
        // Only types with a proxy prefab (instances that PromoteFoliageInstance can turn into entities).
        bool requireProxy = false;
        // Keeps the nearest this many across the whole scene; 0 keeps all.
        std::size_t maxResults = 0;
    };

    struct FoliageInstanceMatch
    {
        EntityID foliageEntity = 0;
        std::uint64_t instanceId = 0;
        std::size_t typeIndex = 0;
        // From the query's centre to the instance's pivot, in world units.
        float distance = 0.0f;
    };

    // The instances whose pivot lies within the query's sphere, nearest first, from the active entities' foliage
    // components (an inactive entity, such as a TerrainWorld's tile template, is a description, not scenery).
    // Promoted instances are not among them: their proxies are entities. Each component reads only the grid cells the
    // sphere touches (FoliageComponent::FindInstancesInRadius), so this is cheap enough for every frame.
    [[nodiscard]] std::vector<FoliageInstanceMatch> FindFoliageInstances(const Scene &scene, const FoliageInstanceQuery &query);
}
