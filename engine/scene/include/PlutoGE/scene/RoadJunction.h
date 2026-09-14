#pragma once
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace PlutoGE::render { struct MeshConfig; }
namespace PlutoGE::assets { class AssetManager; }
namespace PlutoGE::scene
{
    class Entity;
    struct RoadJunctionEndpoint { std::uint32_t entity = 0; bool atEnd = true; };
    // Convex junction surface with exact banked mouth vertices. Rejects overlapping
    // or interior entrances and leaves output unchanged on failure. Optional bottom
    // mouths add an underside and exposed walls, matching each road cross-section.
    bool BuildRoadJunction(const std::vector<std::array<glm::vec3, 2>> &mouths,
        float uvMetersPerTile, render::MeshConfig &output, std::string &error,
        const std::vector<std::array<glm::vec3, 2>> &bottomMouths = {});
    // Explicit bake into a mesh asset and an undoable child of the first road.
    Entity *BakeRoadJunction(Entity &owner, const std::vector<RoadJunctionEndpoint> &endpoints,
        assets::AssetManager &assets, const std::string &reference, std::string &error);
}
