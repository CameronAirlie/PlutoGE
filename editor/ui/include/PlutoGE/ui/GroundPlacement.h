#pragma once

#include "PlutoGE/scene/Entity.h"
#include <cstdint>
#include <string>

namespace PlutoGE::scene { class Scene; }

namespace PlutoGE::ui
{
    struct GroundPlacementOptions
    {
        float maxDistance = 1000;
        float surfaceOffset = 0;
        bool alignToNormal = false;
        bool usePivot = false;
        float randomYawDegrees = 0;
        float minScaleFactor = 1;
        float maxScaleFactor = 1;
        uint32_t seed = 0;
    };

    class GroundPlacement
    {
    public:
        // Compute a local authoring transform without mutating the selection.
        // Scene::Raycast excludes the selected entity and all of its descendants.
        static bool Compute(const scene::Scene &scene, const scene::Entity &entity,
                            const GroundPlacementOptions &options, scene::Transform &result,
                            std::string &error);
    };
}
