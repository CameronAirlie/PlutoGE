#include "PlutoGE/scene/FoliageQueries.h"

#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/FoliageComponent.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::scene
{
    std::vector<FoliageInstanceMatch> FindFoliageInstances(const Scene &scene, const FoliageInstanceQuery &query)
    {
        std::vector<FoliageInstanceMatch> matches;
        if (!(query.radius >= 0.0f) || !std::isfinite(query.radius)) return matches;
        const FoliageComponent::TypeFilter accept = [&query](const FoliageType &type)
        {
            if (query.requireProxy && type.asset.proxyPrefabReference.empty()) return false;
            return query.interactionTag.empty() || HasFoliageInteractionTag(type.asset, query.interactionTag);
        };
        for (const auto *foliage : scene.GetFoliageComponents())
        {
            const auto *owner = foliage ? foliage->GetOwner() : nullptr;
            if (!owner || !owner->IsActive() || !foliage->IsEnabled()) continue;
            // Each component's nearest maxResults are all that can make the scene's nearest maxResults.
            for (const auto &location : foliage->FindInstancesInRadius(query.center, query.radius, accept, query.maxResults))
            {
                const auto *instance = foliage->GetInstance(location);
                const auto transform = foliage->GetInstanceWorldTransform(location);
                if (!instance || !transform) continue;
                matches.push_back({owner->GetID(), instance->id, location.typeIndex,
                                   glm::length(glm::vec3((*transform)[3]) - query.center)});
            }
        }
        std::sort(matches.begin(), matches.end(), [](const auto &a, const auto &b) { return a.distance < b.distance; });
        if (query.maxResults > 0 && matches.size() > query.maxResults) matches.resize(query.maxResults);
        return matches;
    }
}
