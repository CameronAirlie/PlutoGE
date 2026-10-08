#include "PlutoGE/ui/StaticModelHierarchyPlacement.h"
#include "PlutoGE/ui/GroundPlacement.h"
#include "PlutoGE/ui/SurfacePlacement.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/SceneFormat.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/ModelSourcePackage.h"
#include "PlutoGE/asset_import/ModelObjectExtractionService.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include <algorithm>
#include <stdexcept>
#include <limits>

namespace PlutoGE::ui
{
    namespace
    {
        bool ValidateLayout(const assets::StaticModelInstanceLayout &layout, std::size_t submeshCount, std::string &error)
        {
            if (layout.nodes.empty() || layout.nodes.size() + layout.bindings.size() > 4096 || layout.bindings.empty())
            { error = "Static hierarchy placement requires 1–4096 nodes/bindings with visible geometry."; return false; }
            std::vector<unsigned> depths(layout.nodes.size());
            scene::Transform transform;
            glm::mat4 correction;
            for (int index = 0; index < static_cast<int>(layout.nodes.size()); ++index)
            {
                const auto &node = layout.nodes[index];
                if (node.parentIndex < -1 || node.parentIndex >= index ||
                    !scene::FactorLocalTransform(node.localTransform, transform, correction))
                { error = "Static hierarchy contains an unsupported local affine transform or parent ordering."; return false; }
                depths[index] = node.parentIndex < 0 ? 0 : depths[node.parentIndex] + 1;
                if (depths[index] > 128)
                { error = "Static snapshot placement currently supports source hierarchies up to 128 levels deep."; return false; }
            }
            for (const auto &binding : layout.bindings)
                if (binding.nodeIndex < 0 || binding.nodeIndex >= static_cast<int>(layout.nodes.size()) ||
                    binding.submeshIndex >= submeshCount || !scene::FactorLocalTransform(binding.geometryToNode, transform, correction))
                { error = "Static hierarchy contains an unsupported render binding."; return false; }
            return true;
        }
    }

    bool PrepareStaticModelHierarchySnapshot(const assets::Project &project, const std::string &sourceReference,
        StaticModelHierarchySnapshot &snapshot, std::string &error)
    {
        error.clear();
        if (project.GetManifest().assetPipelineVersion < 3)
        { error = "Static hierarchy snapshots require a Library-based project (version 3 or later)."; return false; }
        assets::ModelHierarchyAsset hierarchy;
        if (!assets::LoadModelHierarchyAsset(project, sourceReference, hierarchy, &error)) return false;
        // The package loaded after the hierarchy must describe those exact bytes.
        assets::ModelAsset package;
        assets::ModelHierarchyArtifact descriptor;
        std::string bytes;
        if (!assets::LoadModelSourcePackage(project, sourceReference, package, &error) ||
            assets::ReadModelHierarchyArtifact(package, descriptor, &error) != assets::ModelHierarchyArtifactStatus::Success ||
            !assets::SerializeModelHierarchyAsset(hierarchy, bytes, &error)) return false;
        if (package.sourceAssetId != hierarchy.sourceAssetId ||
            content::HashContent(std::as_bytes(std::span(bytes.data(), bytes.size()))) != descriptor.digest)
        { error = "Source hierarchy changed during snapshot preparation; retry."; return false; }
        assets::AssetDatabase database;
        auto scanProject = project;
        if (!database.Scan(scanProject, assets::AssetScanOptions{.createMissingMetadata = false}, &error)) return false;
        assets::AssetReference identity;
        if (!assets::ParseAssetReference(hierarchy.meshReference, identity, &error)) return false;
        const auto *object = database.GetCatalog()->Find(identity);
        if (!object || object->type != assets::ProjectAssetType::Mesh || object->ownership != assets::AssetOwnership::Imported)
        { error = "Source hierarchy mesh is not an available imported object."; return false; }
        const auto *storage = database.GetStorageMap() ? database.GetStorageMap()->Find(object->location) : nullptr;
        const auto baseline = std::find_if(package.generatedFiles.begin(), package.generatedFiles.end(),
            [&](const auto &file) { return file.reference == object->location; });
        if (!storage || !storage->available || baseline == package.generatedFiles.end() || storage->digest != baseline->digest)
        { error = "Source mesh generation changed during snapshot preparation; retry."; return false; }
        assets::AssetManager reader;
        reader.SetProjectContext(project.GetRootDirectory().string(), project.GetManifest().assetDirectory, project.GetManifest().assetPipelineVersion);
        reader.SetAssetSnapshot(database.GetCatalog(), database.GetStorageMap());
        render::MeshConfig config;
        assets::MeshAssetMetadata metadata;
        StaticModelHierarchySnapshot candidate;
        if (!reader.LoadMeshAssetData(hierarchy.meshReference, config, candidate.materials, metadata, &error)) return false;
        content::ContentDigest current;
        if (!content::HashFileContent(storage->path, current, &error) || current != storage->digest)
        { error = "Source geometry changed during snapshot preparation; retry."; return false; }
        if (metadata.sourceAssetId != identity.assetId || metadata.sourceObjectId != identity.localObjectId || !config.skeleton.joints.empty())
        { error = "Static hierarchy snapshots require a rigid source-owned mesh without skinning."; return false; }
        if (!assets::PrepareStaticModelInstanceLayout(hierarchy, config.submeshes.size(), candidate.layout, &error,
            assets::StaticModelIdentityPolicy::IndependentSnapshot) || !ValidateLayout(candidate.layout, config.submeshes.size(), error)) return false;
        for (const auto &binding : candidate.layout.bindings)
            if (config.submeshes[binding.submeshIndex].animatedNodeIndex >= 0)
            { error = "Selected mesh binding still owns an animation-node transform; static snapshot placement is unsupported."; return false; }
        candidate.materials = reader.GetMeshAssetMaterialReferences(hierarchy.meshReference);
        candidate.sourceMeshDigest = current;
        candidate.name = std::filesystem::path(sourceReference).stem().string();
        snapshot = std::move(candidate);
        return true;
    }

    scene::Entity *InsertStaticModelHierarchy(scene::Scene &destination, const assets::StaticModelInstanceLayout &layout,
        render::Mesh &mesh, const std::vector<std::string> &materialReferences, assets::AssetManager &assets,
        const std::string &name, scene::Entity *parent, std::string &error)
    {
        error.clear();
        if (!assets.GetProjectRootDirectory().empty() && assets.GetAssetPipelineVersion() < assets::kAffineSceneProjectVersion)
        { error = "Static hierarchy insertion requires explicit project version 4 activation."; return nullptr; }
        if ((parent && !destination.ContainsEntity(parent)) || !ValidateLayout(layout, mesh.GetSubmeshCount(), error))
        { if (error.empty()) error = "Hierarchy parent is not in the destination scene."; return nullptr; }
        if (mesh.HasSkeleton()) { error = "Static hierarchy does not support skinned geometry."; return nullptr; }
        std::vector<render::Material *> materials;
        for (const auto &reference : materialReferences)
        {
            auto *material = reference.empty() ? nullptr : assets.LoadMaterialAsset(reference);
            if (!reference.empty() && !material) { error = "Could not load hierarchy material: " + reference; return nullptr; }
            materials.push_back(material);
        }
        for (const auto &binding : layout.bindings)
            if (mesh.GetSubmesh(binding.submeshIndex).animatedNodeIndex >= 0)
            { error = "Static hierarchy cannot apply animation-node geometry transforms."; return nullptr; }
        scene::Entity *root = nullptr;
        try
        {
            root = destination.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = name + " (Snapshot)"}), parent);
            std::vector<scene::Entity *> nodes;
            nodes.reserve(layout.nodes.size());
            for (const auto &node : layout.nodes)
            {
                auto entity = std::make_unique<scene::Entity>(scene::EntityConfig{.name = node.name.empty() ? "Unnamed Node" : node.name});
                if (!entity->SetLocalTransformMatrix(node.localTransform)) throw std::runtime_error("Could not preserve source node transform.");
                nodes.push_back(destination.AddEntity(std::move(entity), node.parentIndex < 0 ? root : nodes[node.parentIndex]));
            }
            for (const auto &binding : layout.bindings)
            {
                auto entity = std::make_unique<scene::Entity>(scene::EntityConfig{
                    .name = "Geometry " + std::to_string(binding.submeshIndex)});
                if (!entity->SetLocalTransformMatrix(binding.geometryToNode)) throw std::runtime_error("Could not preserve baked geometry compensation.");
                auto *component = entity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = &mesh});
                component->SetMeshAssetReference(layout.meshReference);
                component->SetSubmeshRange(static_cast<int>(binding.submeshIndex), 1);
                for (std::size_t slot = 0; slot < materials.size(); ++slot)
                {
                    if (materials[slot]) component->SetMaterialForMaterialSlot(slot, materials[slot]);
                    component->SetMaterialAssetForMaterialSlot(slot, materialReferences[slot]);
                }
                destination.AddEntity(std::move(entity), nodes[binding.nodeIndex]);
            }
            return root;
        }
        catch (const std::exception &exception)
        {
            if (root) destination.RemoveEntity(root);
            error = std::string("Could not insert static hierarchy: ") + exception.what();
            return nullptr;
        }
    }

    scene::Entity *PlaceStaticModelHierarchySnapshot(assets::Project &project, const StaticModelHierarchySnapshot &snapshot,
        const std::string &destination, assets::AssetManager &assets, scene::Scene &scene,
        const ViewportPickRay &ray, std::string &error)
    {
        error.clear();
        if (project.GetManifest().assetPipelineVersion < assets::kAffineSceneProjectVersion ||
            assets.GetAssetPipelineVersion() < assets::kAffineSceneProjectVersion)
        { error = "Enable project version 4 before placing exact affine model hierarchies."; return nullptr; }
        if (!MakeCameraPlacementHit(ray)) { error = "Invalid camera placement target."; return nullptr; }
        // Resolve all material failures before creating an authored geometry asset.
        for (const auto &reference : snapshot.materials)
            if (!reference.empty() && !assets.LoadMaterialAsset(reference))
            { error = "Could not load hierarchy material: " + reference; return nullptr; }
        assetimport::ModelObjectExtractionResult extracted;
        if (!assetimport::ModelObjectExtractionService{}.Extract(project, snapshot.layout.meshReference, destination,
            extracted, &error, false, snapshot.sourceMeshDigest)) return nullptr;
        assets.SetAssetSnapshot(extracted.catalog, extracted.storage);
        auto layout = snapshot.layout;
        if (!assets::SerializeAssetReference(extracted.identity, layout.meshReference, &error)) return nullptr;
        auto *mesh = assets.LoadMeshAsset(layout.meshReference);
        if (!mesh) { error = "The authored mesh was created but could not be loaded: " + destination; return nullptr; }
        const auto references = assets.GetMeshAssetMaterialReferences(layout.meshReference);
        // Prepare the exact placement pose in a detached scene before publication.
        scene::Scene prototype;
        auto *prototypeRoot = InsertStaticModelHierarchy(prototype, layout, *mesh, references, assets, snapshot.name, nullptr, error);
        if (!prototypeRoot) { error += " Authored mesh remains at " + destination; return nullptr; }
        glm::vec3 low(std::numeric_limits<float>::infinity()), high(-std::numeric_limits<float>::infinity());
        std::vector<const scene::Entity *> pending{prototypeRoot};
        while (!pending.empty())
        {
            const auto *entity = pending.back();
            pending.pop_back();
            if (const auto *component = entity->GetComponent<scene::MeshComponent>())
            {
                const auto &part = mesh->GetSubmesh(static_cast<std::size_t>(component->GetSubmeshIndex()));
                for (int corner = 0; corner < 8; ++corner)
                {
                    const auto point = glm::vec3(entity->GetWorldTransform() * glm::vec4(
                        (corner & 1) ? part.boundsMax.x : part.boundsMin.x,
                        (corner & 2) ? part.boundsMax.y : part.boundsMin.y,
                        (corner & 4) ? part.boundsMax.z : part.boundsMin.z, 1));
                    low = glm::min(low, point); high = glm::max(high, point);
                }
            }
            for (const auto *child : entity->GetChildren()) pending.push_back(child);
        }
        const auto hit = MakeCameraPlacementHit(ray, high - low);
        scene::Transform pose;
        if (!hit || !GroundPlacement::ComputeAtSurface(*prototypeRoot, nullptr, hit->point, hit->normal, {}, pose, error))
        { if (error.empty()) error = "Invalid camera placement target."; error += " Authored mesh remains at " + destination; return nullptr; }
        auto *created = InsertStaticModelHierarchy(scene, layout, *mesh, references, assets, snapshot.name, nullptr, error);
        if (!created) { error += " Authored mesh remains at " + destination; return nullptr; }
        created->SetPosition(pose.position);
        created->SetRotation(pose.rotation);
        created->SetScale(pose.scale);
        return created;
    }
}
