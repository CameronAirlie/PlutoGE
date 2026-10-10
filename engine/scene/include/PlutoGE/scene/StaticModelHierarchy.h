#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace PlutoGE::assets { class AssetManager; struct StaticModelInstanceLayout; }
namespace PlutoGE::render { class Mesh; }
namespace PlutoGE::scene
{
    class Scene;
    class Entity;
    struct StaticModelHierarchyInsertion
    {
        Entity *root = nullptr;
        std::vector<Entity *> nodes;
        std::vector<Entity *> geometry;
    };
    // CPU affine/topology validation shared by independent and linked hierarchies.
    bool ValidateStaticModelHierarchyLayout(const assets::StaticModelInstanceLayout &layout,
        std::size_t submeshCount, std::string &error, bool allowEmpty = false);
    // Resource-owning thread. Borrowed resources must outlive the new entities.
    // Output is replaced only after complete insertion; failures remove this tree.
    bool InsertStaticModelHierarchy(Scene &destination, const assets::StaticModelInstanceLayout &layout,
        render::Mesh *mesh, const std::vector<std::string> &materialReferences, assets::AssetManager &assets,
        const std::string &name, Entity *parent, StaticModelHierarchyInsertion &output,
        std::string &error, bool allowEmpty = false);
}
