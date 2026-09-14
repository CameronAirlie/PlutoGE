#pragma once
#include "PlutoGE/scene/components/SplineComponent.h"
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    class Entity;
    // Deterministic local-space poses. Empty means invalid/degenerate input or
    // more than 1,024 placements. Sampling does not create scene or GPU objects.
    std::vector<SplineControlPoint> SampleRoadsidePlacements(
        const SplineComponent &road, float spacing, float edgeOffset, bool bothSides);
    // Authoring bake: ordinary prefab children, grouped for scene history.
    Entity *BakeRoadsidePrefabs(Entity &owner, const std::string &prefab,
        float spacing, float edgeOffset, bool bothSides, std::string &error);
}
