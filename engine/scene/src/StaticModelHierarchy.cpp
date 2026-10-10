#include "PlutoGE/scene/StaticModelHierarchy.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/ModelHierarchyAsset.h"
#include "PlutoGE/assets/SceneFormat.h"
#include <stdexcept>
#include <utility>

namespace PlutoGE::scene
{
    bool ValidateStaticModelHierarchyLayout(const assets::StaticModelInstanceLayout &layout, std::size_t submeshCount, std::string &error, bool allowEmpty)
    {
        error.clear();
        if (layout.nodes.size() > 4096 || layout.bindings.size() > 4096 - layout.nodes.size() ||
                (!allowEmpty && (layout.nodes.empty() || layout.bindings.empty())))
        { error = "Static hierarchy placement requires 1–4096 nodes/bindings with visible geometry."; return false; }
        std::vector<unsigned> depths(layout.nodes.size());
        Transform transform;
        glm::mat4 correction;
        for (int index = 0; index < static_cast<int>(layout.nodes.size()); ++index)
        {
            const auto &node = layout.nodes[index];
            if (node.parentIndex < -1 || node.parentIndex >= index ||
                !FactorLocalTransform(node.localTransform, transform, correction))
            { error = "Static hierarchy contains an unsupported local affine transform or parent ordering."; return false; }
            depths[index] = node.parentIndex < 0 ? 1 : depths[node.parentIndex] + 1;
            if (depths[index] > 128)
            { error = "Static snapshot placement currently supports source hierarchies up to 128 levels deep."; return false; }
        }
        for (const auto &binding : layout.bindings)
            if (binding.nodeIndex < 0 || binding.nodeIndex >= static_cast<int>(layout.nodes.size()) ||
                binding.submeshIndex >= submeshCount || !FactorLocalTransform(binding.geometryToNode, transform, correction))
            { error = "Static hierarchy contains an unsupported render binding."; return false; }
        return true;
    }

    bool InsertStaticModelHierarchy(Scene &destination, const assets::StaticModelInstanceLayout &layout,
        render::Mesh *mesh, const std::vector<std::string> &materialReferences, assets::AssetManager &assets,
        const std::string &name, Entity *parent, StaticModelHierarchyInsertion &output, std::string &error, bool allowEmpty)
    {
        Entity *root = nullptr;
        try
        {
            error.clear();
            if (!assets.GetProjectRootDirectory().empty() && assets.GetAssetPipelineVersion() < assets::kAffineSceneProjectVersion)
            { error = "Static hierarchy insertion requires explicit project version 4 activation."; return false; }
            if ((parent && !destination.ContainsEntity(parent)) || !ValidateStaticModelHierarchyLayout(layout, (mesh ? mesh->GetSubmeshCount() : 0), error, allowEmpty))
            { if (error.empty()) error = "Hierarchy parent is not in the destination scene."; return false; }
            if (mesh && mesh->HasSkeleton()) { error = "Static hierarchy does not support skinned geometry."; return false; }
            std::vector<render::Material *> materials;
            for (const auto &reference : materialReferences)
            {
                auto *material = reference.empty() ? nullptr : assets.LoadMaterialAsset(reference);
                if (!reference.empty() && !material) { error = "Could not load hierarchy material: " + reference; return false; }
                materials.push_back(material);
            }
            for (const auto &binding : layout.bindings)
                if (mesh->GetSubmesh(binding.submeshIndex).animatedNodeIndex >= 0)
                { error = "Static hierarchy cannot apply animation-node geometry transforms."; return false; }
            root = destination.AddEntity(std::make_unique<Entity>(EntityConfig{.name = name}), parent);
            std::vector<Entity *> nodes;
            nodes.reserve(layout.nodes.size());
            std::vector<Entity *> geometry;
            geometry.reserve(layout.bindings.size());
            for (const auto &node : layout.nodes)
            {
                auto entity = std::make_unique<Entity>(EntityConfig{.name = node.name.empty() ? "Unnamed Node" : node.name});
                if (!entity->SetLocalTransformMatrix(node.localTransform)) throw std::runtime_error("Could not preserve source node transform.");
                nodes.push_back(destination.AddEntity(std::move(entity), node.parentIndex < 0 ? root : nodes[node.parentIndex]));
            }
            for (const auto &binding : layout.bindings)
            {
                auto entity = std::make_unique<Entity>(EntityConfig{
                    .name = "Geometry " + std::to_string(binding.submeshIndex)});
                if (!entity->SetLocalTransformMatrix(binding.geometryToNode)) throw std::runtime_error("Could not preserve baked geometry compensation.");
                auto *component = entity->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh = mesh});
                component->SetMeshAssetReference(layout.meshReference);
                component->SetSubmeshRange(static_cast<int>(binding.submeshIndex), 1);
                for (std::size_t slot = 0; slot < materials.size(); ++slot)
                {
                    if (materials[slot]) component->SetMaterialForMaterialSlot(slot, materials[slot]);
                    component->SetMaterialAssetForMaterialSlot(slot, materialReferences[slot]);
                }
                geometry.push_back(destination.AddEntity(std::move(entity), nodes[binding.nodeIndex]));
            }
            output = {root, std::move(nodes), std::move(geometry)};
            return true;
        }
        catch (const std::exception &exception)
        {
            if (root) destination.RemoveEntity(root);
            error = std::string("Could not insert static hierarchy: ") + exception.what();
            return false;
        }
    }

}
