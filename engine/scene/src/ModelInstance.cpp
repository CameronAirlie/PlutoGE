#include "PlutoGE/scene/ModelInstance.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/StaticModelHierarchy.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/ModelGenerationSnapshot.h"
#include "PlutoGE/assets/SceneModelInstanceRecord.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace PlutoGE::scene
{
    namespace
    {
        bool Near(const glm::mat4 &a, const glm::mat4 &b)
        {
            for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row)
                if (!std::isfinite(a[column][row]) || !std::isfinite(b[column][row]) ||
                    std::abs(a[column][row] - b[column][row]) > 0.0001f * (std::max)(1.0f, std::abs(b[column][row]))) return false;
            return true;
        }
    }
    Entity *CreateStaticModelInstance(Scene &scene, const assets::Project &project,
        const assets::StaticModelGenerationSnapshot &generation, assets::AssetManager &sharedAssets, const std::string &name,
        Entity *parent, std::string *errorMessage)
    {
        Entity *created = nullptr;
        try
        {
            if (project.GetManifest().assetPipelineVersion < assets::kLinkedModelSceneProjectVersion)
                throw std::runtime_error("Linked model creation requires explicit project version 5 activation.");
            if (std::filesystem::absolute(sharedAssets.GetProjectRootDirectory()).lexically_normal() !=
                std::filesystem::absolute(project.GetRootDirectory()).lexically_normal())
                throw std::runtime_error("Shared assets belong to another project context.");
            std::shared_ptr<assets::AssetManager> reader;
            std::string error;
            for (const auto &[root, instance] : scene.GetStaticModelInstances())
            {
                if (instance.state.artifactGenerationKey != generation.artifacts.generation) continue;
                if (!assets::ValidateStaticModelInstanceBaseline(instance.state, generation, &error)) throw std::runtime_error(error);
                reader = instance.resources;
                if (!reader || std::filesystem::absolute(reader->GetProjectRootDirectory()).lexically_normal() !=
                    std::filesystem::absolute(project.GetRootDirectory()).lexically_normal())
                    throw std::runtime_error("Linked generation belongs to another project context.");
                break;
            }
            if (!reader)
            {
                reader = std::make_shared<assets::AssetManager>(assets::AssetManager::ResourceLifetime::Scoped,
                    &sharedAssets, generation.generation.layout.sourceAssetId);
                reader->SetProjectContext(project.GetRootDirectory().string(), project.GetManifest().assetDirectory,
                    project.GetManifest().assetPipelineVersion);
                reader->SetAssetSnapshot(generation.artifacts.catalog, generation.artifacts.storage);
            }
            const auto &layout = generation.generation.layout;
            auto *mesh = layout.bindings.empty() ? nullptr : reader->LoadMeshAsset(layout.meshReference);
            if (!layout.bindings.empty() && !mesh) throw std::runtime_error("Accepted linked geometry is unavailable.");
            StaticModelHierarchyInsertion inserted;
            if (!InsertStaticModelHierarchy(scene, layout, mesh, generation.defaultMaterials, *reader, name, parent, inserted, error, true))
                throw std::runtime_error(error);
            created = inserted.root;
            for (auto *entity : inserted.geometry) entity->GetComponent<MeshComponent>()->RetainAssetReader(reader);
            assets::StaticModelInstanceState state;
            state.rootEntityId = created->GetID(); state.artifactGenerationKey = generation.artifacts.generation;
            state.packageArtifact = generation.artifacts.packageArtifact; state.accepted = generation.generation;
            state.defaultMaterials = generation.defaultMaterials;
            state.overrides.hierarchyDigest = layout.hierarchyDigest; state.overrides.meshDigest = state.accepted.meshDigest;
            for (std::size_t index = 0; index < layout.nodes.size(); ++index)
                state.nodeEntities.push_back({layout.nodes[index].sourceNodeId, inserted.nodes[index]->GetID()});
            for (const auto *entity : inserted.geometry) state.bindingEntities.push_back(entity->GetID());
            if (!scene.InstallStaticModelInstance({std::move(state), std::move(reader)}, &error)) throw std::runtime_error(error);
            if (errorMessage) errorMessage->clear();
            return created;
        }
        catch (const std::exception &error)
        {
            if (created) scene.RemoveEntity(created);
            if (errorMessage) *errorMessage = std::string("Cannot create linked model instance: ") + error.what();
            return nullptr;
        }
    }

    bool CaptureStaticModelInstance(const Scene &scene, const StaticModelSceneInstance &instance,
        assets::StaticModelInstanceState &output, std::string *errorMessage)
    {
        try
        {
            auto candidate = instance.state;
            std::string bytes, error;
            if (!instance.resources || !assets::SerializeStaticModelInstanceState(candidate, bytes, &error))
                throw std::runtime_error(error.empty() ? "Linked model has no accepted resource context." : error);
            const auto *root = scene.FindEntityByID(candidate.rootEntityId);
            if (!root) throw std::runtime_error("Linked model root is missing.");
            std::map<std::uint64_t, std::uint32_t> entities;
            std::set<std::uint32_t> generated{candidate.rootEntityId};
            for (const auto &mapping : candidate.nodeEntities) { entities.emplace(mapping.sourceNodeId, mapping.sceneEntityId); generated.insert(mapping.sceneEntityId); }
            for (const auto entity : candidate.bindingEntities) generated.insert(entity);
            const auto edit = [&](std::uint64_t nodeId) -> assets::StaticModelNodeOverride &
            {
                const auto found = std::find_if(candidate.overrides.nodes.begin(), candidate.overrides.nodes.end(),
                    [&](const auto &entry) { return entry.sourceNodeId == nodeId; });
                if (found != candidate.overrides.nodes.end()) return *found;
                candidate.overrides.nodes.push_back({nodeId}); return candidate.overrides.nodes.back();
            };
            const auto geometryEdit = [&](std::uint64_t nodeId)
            {
                auto &value = edit(nodeId);
                value.hasAdditionalEdits = true;
                value.hasGeometryEdits = true;
            };
            for (const auto &mapping : candidate.nodeEntities)
            {
                const auto *entity = scene.FindEntityByID(mapping.sceneEntityId);
                if (!entity || !entity->GetGeneratedTransformEdits()) continue;
                const auto channels = entity->GetGeneratedTransformEdits();
                auto &authored = edit(mapping.sourceNodeId);
                if (channels & static_cast<uint8_t>(Entity::TransformEditChannel::Affine))
                {
                    authored.localTransform = entity->GetLocalTransform();
                    authored.localPosition.reset(); authored.localRotation.reset(); authored.localScale.reset();
                }
                else
                {
                    if (channels & static_cast<uint8_t>(Entity::TransformEditChannel::Position)) authored.localPosition = entity->GetPosition();
                    if (channels & static_cast<uint8_t>(Entity::TransformEditChannel::Rotation)) authored.localRotation = entity->GetRotation();
                    if (channels & static_cast<uint8_t>(Entity::TransformEditChannel::Scale)) authored.localScale = entity->GetScale();
                }
            }
            assets::StaticModelInstanceReconciliation effective;
            if (!assets::PrepareStaticModelInstanceReconciliation(candidate.accepted, candidate.overrides,
                candidate.accepted, effective, &error)) throw std::runtime_error(error);
            for (std::size_t index = 0; index < candidate.accepted.layout.nodes.size(); ++index)
            {
                const auto &baseline = candidate.accepted.layout.nodes[index];
                const auto *entity = scene.FindEntityByID(entities.at(baseline.sourceNodeId));
                if (!entity) throw std::runtime_error("A generated model node was deleted; preserve an authored snapshot before saving structural changes.");
                const auto expectedParent = baseline.parentIndex < 0 ? candidate.rootEntityId :
                    entities.at(candidate.accepted.layout.nodes[baseline.parentIndex].sourceNodeId);
                if (!entity->GetParent() || entity->GetParent()->GetID() != expectedParent) edit(baseline.sourceNodeId).hasStructuralEdits = true;
                if (!Near(entity->GetLocalTransform(), effective.nodes[index].localTransform))
                {
                    auto &authored = edit(baseline.sourceNodeId);
                    authored.localTransform = entity->GetLocalTransform();
                    authored.localPosition.reset(); authored.localRotation.reset(); authored.localScale.reset();
                }
                if (entity->IsSelfActive() != effective.nodes[index].enabled) edit(baseline.sourceNodeId).enabled = entity->IsSelfActive();
                if (entity->GetName() != (baseline.name.empty() ? "Unnamed Node" : baseline.name) || !entity->GetTags().empty() ||
                    std::any_of(entity->GetComponentBuckets().begin(), entity->GetComponentBuckets().end(), [](const auto &bucket) { return !bucket.empty(); }) ||
                    std::any_of(entity->GetChildren().begin(), entity->GetChildren().end(), [&](const auto *child) { return !generated.contains(child->GetID()); }))
                    edit(baseline.sourceNodeId).hasAdditionalEdits = true;
            }
            for (std::size_t index = 0; index < candidate.bindingEntities.size(); ++index)
            {
                const auto &binding = candidate.accepted.layout.bindings[index];
                const auto &node = candidate.accepted.layout.nodes[binding.nodeIndex];
                const auto *entity = scene.FindEntityByID(candidate.bindingEntities[index]);
                const auto *mesh = entity ? entity->GetComponent<MeshComponent>() : nullptr;
                if (!entity || !mesh || !mesh->GetMesh() || mesh->GetMeshAssetReference() != candidate.accepted.layout.meshReference ||
                    mesh->GetSubmeshIndex() != static_cast<int>(binding.submeshIndex) || mesh->GetSubmeshRangeCount() != 1 ||
                    !mesh->UsesRetainedGeometry(instance.resources->LoadMeshAsset(candidate.accepted.layout.meshReference)))
                    throw std::runtime_error("Linked model geometry or its accepted binding was removed/replaced.");
                if (!entity->GetParent() || entity->GetParent()->GetID() != entities.at(node.sourceNodeId)) edit(node.sourceNodeId).hasStructuralEdits = true;
                if (!Near(entity->GetLocalTransform(), binding.geometryToNode) || !entity->IsSelfActive() || !mesh->IsEnabled() ||
                    !mesh->IsVisible() || mesh->IsStatic() || mesh->GetPivotOffset() != glm::vec3(0) ||
                    mesh->GetMeshPositionOffset() != glm::vec3(0) || mesh->GetMeshRotationOffset() != glm::vec3(0) ||
                    !entity->GetTags().empty() || entity->GetName() != "Geometry " + std::to_string(binding.submeshIndex) ||
                    !entity->GetChildren().empty()) geometryEdit(node.sourceNodeId);
                for (const auto &bucket : entity->GetComponentBuckets())
                    for (const auto *component : bucket)
                        if (component != mesh) geometryEdit(node.sourceNodeId);
                for (const auto &property : mesh->Serialize())
                    if (property.name.starts_with("SubmeshTransforms.") || property.name == "GeneratedLightmapUvSubmeshes" ||
                        ((property.name.starts_with("MaterialSlots.") || property.name.starts_with("SubmeshOverrides.")) &&
                            !property.name.ends_with("MaterialAsset"))) geometryEdit(node.sourceNodeId);
                for (std::size_t slot = 0; slot < candidate.defaultMaterials.size(); ++slot)
                {
                    auto found = std::find_if(candidate.overrides.materials.begin(), candidate.overrides.materials.end(),
                        [&](const auto &entry) { return entry.bindingIndex == index && entry.materialSlot == slot; });
                    const auto expected = found == candidate.overrides.materials.end() ? candidate.defaultMaterials[slot] : found->reference;
                    const bool submeshOverride = mesh->GetMesh()->GetSubmesh(binding.submeshIndex).materialIndex == slot &&
                        mesh->HasMaterialOverrideForSubmesh(binding.submeshIndex);
                    const auto &actual = submeshOverride ? mesh->GetMaterialAssetForSubmesh(binding.submeshIndex) : mesh->GetMaterialAssetForMaterialSlot(slot);
                    if (actual == expected) continue;
                    if (found == candidate.overrides.materials.end()) candidate.overrides.materials.push_back({index, slot, actual});
                    else found->reference = actual;
                }
            }
            if (!assets::SerializeStaticModelInstanceState(candidate, bytes, &error)) throw std::runtime_error(error);
            output = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        { if (errorMessage) *errorMessage = error.what(); return false; }
    }

    bool CopyStaticModelInstanceLinks(const Entity &source, Entity &clone, std::string *errorMessage)
    {
        try
        {
            const auto *origin = source.GetScene(); auto *destination = clone.GetScene();
            if (!destination) throw std::runtime_error("Model linkage cloning requires a destination scene.");
            if (!origin) { if (errorMessage) errorMessage->clear(); return true; }
            std::map<EntityID, EntityID> remap;
            std::vector<std::pair<const Entity *, Entity *>> pending{{&source, &clone}};
            while (!pending.empty())
            {
                const auto [oldEntity, newEntity] = pending.back(); pending.pop_back();
                if (oldEntity->GetChildren().size() != newEntity->GetChildren().size() ||
                    !remap.emplace(oldEntity->GetID(), newEntity->GetID()).second)
                    throw std::runtime_error("Cloned model subtree does not match its source.");
                for (std::size_t index = 0; index < oldEntity->GetChildren().size(); ++index)
                    pending.emplace_back(oldEntity->GetChildren()[index], newEntity->GetChildren()[index]);
            }
            std::vector<StaticModelSceneInstance> instances;
            for (const auto &[root, instance] : origin->GetStaticModelInstances())
            {
                bool overlaps = remap.contains(root);
                for (const auto &node : instance.state.nodeEntities) overlaps = overlaps || remap.contains(node.sceneEntityId);
                for (const auto id : instance.state.bindingEntities) overlaps = overlaps || remap.contains(id);
                if (!overlaps) continue;
                if (!remap.contains(root)) throw std::runtime_error("Duplicate the complete linked model instance or unpack it before duplicating generated nodes.");
                auto copied = instance;
                if (!CaptureStaticModelInstance(*origin, instance, copied.state, errorMessage)) return false;
                copied.state.rootEntityId = remap.at(root);
                for (auto &node : copied.state.nodeEntities) node.sceneEntityId = remap.at(node.sceneEntityId);
                for (auto &binding : copied.state.bindingEntities) binding = remap.at(binding);
                instances.push_back(std::move(copied));
            }
            for (auto &instance : instances)
                if (!destination->InstallStaticModelInstance(std::move(instance), errorMessage)) return false;
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        { if (errorMessage) *errorMessage = error.what(); return false; }
    }

    bool Scene::DetachStaticModelInstance(EntityID rootEntityId, std::string *errorMessage)
    {
        const auto found = m_staticModelInstances.find(rootEntityId);
        if (found == m_staticModelInstances.end())
        { if (errorMessage) *errorMessage = "Model instance is not linked."; return false; }
        for (const auto entityId : found->second.state.bindingEntities)
        {
            const auto *entity = FindEntityByID(entityId);
            const auto *mesh = entity ? entity->GetComponent<MeshComponent>() : nullptr;
            if (!mesh || !mesh->GetMesh() || mesh->GetRetainedAssetReader() ||
                !mesh->GetModelAssetId().empty() || mesh->GetModelObjectId() != 0 || mesh->GetMeshAssetReference().empty())
            { if (errorMessage) *errorMessage = "Convert every accepted binding to authored resources before detaching a model instance."; return false; }
        }
        for (const auto &node : found->second.state.nodeEntities)
            if (auto *entity = FindEntityByID(node.sceneEntityId)) entity->SetGeneratedTransformEditTracking(false);
        m_staticModelInstances.erase(found);
        if (errorMessage) errorMessage->clear();
        return true;
    }

    bool Scene::InstallStaticModelInstance(StaticModelSceneInstance instance, std::string *errorMessage)
    {
        if (!instance.resources) { if (errorMessage) *errorMessage = "Linked model has no resource context."; return false; }
        auto &reader = *instance.resources;
        if (reader.GetProjectRootDirectory().empty() || reader.GetAssetPipelineVersion() < assets::kLinkedModelSceneProjectVersion)
        { if (errorMessage) *errorMessage = "Linked model instances require project version 5."; return false; }
        assets::ProjectManifest manifest;
        manifest.assetDirectory = reader.GetProjectAssetDirectory(); manifest.assetPipelineVersion = reader.GetAssetPipelineVersion();
        const assets::Project project(std::filesystem::path(reader.GetProjectRootDirectory()) / "linked-context.plutoproject", manifest);
        assets::ModelGenerationSnapshot snapshot; assets::StaticModelGenerationSnapshot prepared;
        if (!assets::ReadModelGenerationSnapshot(project, instance.state.accepted.layout.sourceAssetId,
            instance.state.artifactGenerationKey, instance.state.packageArtifact, reader.GetAssetCatalog(), reader.GetAssetStorageMap(), snapshot, errorMessage) ||
            !assets::PrepareStaticModelGenerationSnapshot(project, snapshot, prepared, errorMessage) ||
            !assets::ValidateStaticModelInstanceBaseline(instance.state, prepared, errorMessage)) return false;
        const auto actualCatalog = reader.GetAssetCatalog();
        const auto actualStorage = reader.GetAssetStorageMap();
        if (!actualCatalog || !actualStorage)
        { if (errorMessage) *errorMessage = "Linked geometry requires a private accepted resource snapshot."; return false; }
        for (const auto &object : actualCatalog->GetObjects())
        {
            if (object.identity.assetId != snapshot.package.sourceAssetId || !object.identity.localObjectId) continue;
            const auto *expected = snapshot.catalog->Find(object.identity);
            const auto *actualBytes = actualStorage->Find(object.location);
            const auto *expectedBytes = expected ? snapshot.storage->Find(expected->location) : nullptr;
            if (!expected || object.location != expected->location || object.type != expected->type ||
                object.ownership != expected->ownership || !actualBytes || !actualBytes->available || !expectedBytes ||
                actualBytes->digest != expectedBytes->digest)
            { if (errorMessage) *errorMessage = "Linked reader differs from its accepted generation scope."; return false; }
        }
        for (const auto &object : snapshot.package.objects)
            if (!actualCatalog->Find({snapshot.package.sourceAssetId, object.localId}))
            { if (errorMessage) *errorMessage = "Linked reader is missing an accepted object."; return false; }
        assets::StaticModelInstanceState captured;
        if (!CaptureStaticModelInstance(*this, instance, captured, errorMessage)) return false;
        std::set<EntityID> ids{captured.rootEntityId};
        for (const auto &node : captured.nodeEntities) ids.insert(node.sceneEntityId);
        for (const auto id : captured.bindingEntities) ids.insert(id);
        for (const auto &[root, other] : m_staticModelInstances)
        {
            if (root == captured.rootEntityId) continue;
            if (ids.contains(root) || std::any_of(other.state.nodeEntities.begin(), other.state.nodeEntities.end(),
                    [&](const auto &node) { return ids.contains(node.sceneEntityId); }) ||
                std::any_of(other.state.bindingEntities.begin(), other.state.bindingEntities.end(), [&](auto id) { return ids.contains(id); }))
            { if (errorMessage) *errorMessage = "Scene entities cannot belong to two generated model instances."; return false; }
        }
        for (const auto id : captured.bindingEntities)
            FindEntityByID(id)->GetComponent<MeshComponent>()->RetainAssetReader(instance.resources);
        if (const auto previous = m_staticModelInstances.find(captured.rootEntityId); previous != m_staticModelInstances.end())
            for (const auto &node : previous->second.state.nodeEntities)
                if (auto *entity = FindEntityByID(node.sceneEntityId)) entity->SetGeneratedTransformEditTracking(false);
        for (const auto &node : captured.nodeEntities)
            FindEntityByID(node.sceneEntityId)->SetGeneratedTransformEditTracking(true);
        instance.state = std::move(captured);
        m_staticModelInstances.insert_or_assign(instance.state.rootEntityId, std::move(instance));
        if (errorMessage) errorMessage->clear();
        return true;
    }
}
