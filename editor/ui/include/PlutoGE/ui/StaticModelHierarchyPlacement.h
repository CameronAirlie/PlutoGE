#pragma once
#include "PlutoGE/assets/ModelHierarchyAsset.h"
#include "PlutoGE/ui/ViewportPicking.h"
#include <memory>

namespace PlutoGE::assets { class AssetManager; }
namespace PlutoGE::scene { class Scene; class Entity; }
namespace PlutoGE::render { class Mesh; }
namespace PlutoGE::ui
{
    struct StaticModelHierarchySnapshot
    {
        assets::StaticModelInstanceLayout layout;
        content::ContentDigest sourceMeshDigest{};
        std::vector<std::string> materials;
        std::string name;
    };
    // No project/scene writes or GPU access. Captures a coherent accepted source
    // hierarchy and mesh baseline; anonymous nodes are allowed for detached copies.
    bool PrepareStaticModelHierarchySnapshot(const assets::Project &project, const std::string &sourceReference,
        StaticModelHierarchySnapshot &snapshot, std::string &error);
    // Also used for detached prototypes. Preserves exact nodes and draws each
    // binding once through a compensation child, sharing one mesh across all nodes.
    scene::Entity *InsertStaticModelHierarchy(scene::Scene &scene, const assets::StaticModelInstanceLayout &layout,
        render::Mesh &mesh, const std::vector<std::string> &materials, assets::AssetManager &assets,
        const std::string &name, scene::Entity *parent, std::string &error);
    // Extracts independent authored geometry, then inserts one undoable scene tree.
    // Caller owns the scene edit. Undo removes entities; the authored asset remains.
    scene::Entity *PlaceStaticModelHierarchySnapshot(assets::Project &project, const StaticModelHierarchySnapshot &snapshot,
        const std::string &destination, assets::AssetManager &assets, scene::Scene &scene,
        const ViewportPickRay &ray, std::string &error);
}
