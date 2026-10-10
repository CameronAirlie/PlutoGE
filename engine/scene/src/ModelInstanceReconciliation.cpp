#include "PlutoGE/scene/ModelInstanceReconciliation.h"
#include "PlutoGE/math/AffineTransform.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace PlutoGE::scene
{
    PreparedStaticModelSceneReconciliation::PreparedStaticModelSceneReconciliation() = default;
    PreparedStaticModelSceneReconciliation::~PreparedStaticModelSceneReconciliation() = default;
    PreparedStaticModelSceneReconciliation::PreparedStaticModelSceneReconciliation(PreparedStaticModelSceneReconciliation &&) noexcept = default;
    PreparedStaticModelSceneReconciliation &PreparedStaticModelSceneReconciliation::operator=(PreparedStaticModelSceneReconciliation &&) noexcept = default;
    namespace
    {
        struct InstanceUpdate
        {
            assets::StaticModelInstanceState accepted;
            assets::StaticModelInstanceReconciliation prepared;
        };
        std::string NodeName(const assets::StaticModelInstanceNode &node)
        { return node.name.empty() ? "Unnamed Node" : node.name; }
        using BindingAddress = std::pair<std::uint64_t, std::size_t>;
        std::vector<BindingAddress> BindingAddresses(const assets::StaticModelInstanceLayout &layout)
        {
            std::map<std::uint64_t, std::size_t> ordinals;
            std::vector<BindingAddress> result;
            for (const auto &binding : layout.bindings)
            {
                const auto node = layout.nodes.at(binding.nodeIndex).sourceNodeId;
                result.emplace_back(node, ordinals[node]++);
            }
            return result;
        }
        bool HasBindingEdits(const InstanceUpdate &update, std::size_t index)
        {
            const auto node = update.accepted.accepted.layout.nodes.at(
                update.accepted.accepted.layout.bindings.at(index).nodeIndex).sourceNodeId;
            return std::any_of(update.accepted.overrides.nodes.begin(), update.accepted.overrides.nodes.end(),
                [&](const auto &value) { return value.sourceNodeId == node && value.hasGeometryEdits; }) ||
                std::any_of(update.accepted.overrides.materials.begin(), update.accepted.overrides.materials.end(),
                    [&](const auto &value) { return value.bindingIndex == index; });
        }
        std::vector<Property> RetainedProperties(const MeshComponent &component, assets::AssetManager &reader)
        {
            auto properties = component.Serialize();
            for (auto &property : properties)
                if (property.name.ends_with("Path"))
                {
                    property.value = reader.PersistAssetPath(property.value);
                    if (const auto catalog = reader.GetAssetCatalog())
                        if (const auto identity = catalog->FindIdentityByLocation(property.value))
                        {
                            std::string encoded;
                            if (!assets::SerializeAssetReference(*identity, encoded)) throw std::runtime_error("Invalid retained property identity.");
                            property.value = std::move(encoded);
                        }
                }
            return properties;
        }
        void Apply(Scene &scene, const InstanceUpdate &update, const assets::StaticModelGenerationSnapshot &incoming,
            const std::shared_ptr<assets::AssetManager> &reader)
        {
            const auto &old = update.accepted;
            const auto &layout = incoming.generation.layout;
            auto *root = scene.FindEntityByID(old.rootEntityId);
            if (!root) throw std::runtime_error("Linked root disappeared during isolated preparation.");
            std::map<std::uint64_t, Entity *> previousNodes;
            std::map<std::uint64_t, std::string> previousNames;
            for (std::size_t index = 0; index < old.nodeEntities.size(); ++index)
            {
                previousNodes.emplace(old.nodeEntities[index].sourceNodeId, scene.FindEntityByID(old.nodeEntities[index].sceneEntityId));
            }
            for (const auto &node : old.accepted.layout.nodes) previousNames.emplace(node.sourceNodeId, NodeName(node));
            const auto before = BindingAddresses(old.accepted.layout), after = BindingAddresses(layout);
            std::map<BindingAddress, std::size_t> previousBindings;
            for (std::size_t index = 0; index < before.size(); ++index) previousBindings.emplace(before[index], index);
            auto *mesh = layout.bindings.empty() ? nullptr : reader->LoadMeshAsset(layout.meshReference);
            if (!layout.bindings.empty() && !mesh) throw std::runtime_error("Incoming linked mesh is unavailable.");
            std::vector<render::Material *> defaults;
            for (const auto &reference : incoming.defaultMaterials)
            {
                auto *material = reference.empty() ? nullptr : reader->LoadMaterialAsset(reference);
                if (!reference.empty() && !material) throw std::runtime_error("Incoming linked material is unavailable.");
                defaults.push_back(material);
            }
            auto state = old;
            state.artifactGenerationKey = incoming.artifacts.generation;
            state.packageArtifact = incoming.artifacts.packageArtifact;
            state.accepted = incoming.generation;
            state.defaultMaterials = incoming.defaultMaterials;
            state.overrides = update.prepared.overrides;
            state.nodeEntities.clear(); state.bindingEntities.clear();
            std::vector<Entity *> nodes;
            std::set<EntityID> retained;
            for (std::size_t index = 0; index < layout.nodes.size(); ++index)
            {
                const auto &node = layout.nodes[index];
                auto *parent = node.parentIndex < 0 ? root : nodes.at(node.parentIndex);
                const auto found = previousNodes.find(node.sourceNodeId);
                Entity *entity = found == previousNodes.end() ? scene.AddEntity(
                    std::make_unique<Entity>(EntityConfig{.name = NodeName(node)}), parent) : found->second;
                if (!entity) throw std::runtime_error("Accepted linked node is missing.");
                if (found != previousNodes.end() && entity->GetName() == previousNames.at(node.sourceNodeId)) entity->SetName(NodeName(node));
                entity->SetParent(parent);
                entity->SetGeneratedTransformEditTracking(false);
                if (entity->GetParent() != parent) throw std::runtime_error("Could not apply incoming node hierarchy.");
                const auto &matrix = update.prepared.nodes.at(index).localTransform;
                const auto authored = std::find_if(update.prepared.overrides.nodes.begin(), update.prepared.overrides.nodes.end(),
                    [&](const auto &value) { return value.sourceNodeId == node.sourceNodeId; });
                if (authored != update.prepared.overrides.nodes.end() && (authored->localRotation || authored->localScale))
                {
                    math::ResolvedTransform resolved;
                    if (!math::ResolveLocalTransform(authored->localTransform.value_or(node.localTransform),
                        authored->localPosition, authored->localRotation, authored->localScale, resolved) || resolved.matrix != matrix ||
                        !entity->SetLocalTransformCorrection(resolved.correction))
                        throw std::runtime_error("Could not apply incoming authored transform.");
                    // Preserve authored Euler/signed-scale representations, including
                    // zero scale, instead of decomposing the composed matrix again.
                    entity->SetPosition(resolved.controls.position);
                    entity->SetRotation(resolved.controls.rotation);
                    entity->SetScale(resolved.controls.scale);
                }
                else if (!entity->SetLocalTransformMatrix(matrix))
                    throw std::runtime_error("Could not apply incoming node transform.");
                entity->SetGeneratedTransformEditTracking(true);
                entity->SetActive(update.prepared.nodes.at(index).enabled);
                nodes.push_back(entity); retained.insert(entity->GetID());
                state.nodeEntities.push_back({node.sourceNodeId, entity->GetID()});
            }
            for (std::size_t index = 0; index < layout.bindings.size(); ++index)
            {
                const auto &binding = layout.bindings[index];
                const auto found = previousBindings.find(after[index]);
                Entity *entity = found == previousBindings.end() ? scene.AddEntity(std::make_unique<Entity>(EntityConfig{
                    .name = "Geometry " + std::to_string(binding.submeshIndex)}), nodes.at(binding.nodeIndex)) :
                    scene.FindEntityByID(old.bindingEntities.at(found->second));
                if (!entity) throw std::runtime_error("Accepted linked geometry is missing.");
                auto *component = entity->GetComponent<MeshComponent>();
                if (!component) component = entity->CreateComponent<MeshComponent>(MeshComponentConfig{});
                const bool edited = found != previousBindings.end() && HasBindingEdits(update, found->second);
                const bool enabled = component->IsEnabled();
                std::vector<Property> retainedProperties;
                if (edited) retainedProperties = RetainedProperties(*component, *component->GetRetainedAssetReader());
                auto replacement = std::make_unique<MeshComponent>(MeshComponentConfig{});
                replacement->SetEnabled(enabled);
                if (!entity->RemoveComponent(component)) throw std::runtime_error("Could not replace prepared linked geometry.");
                component = replacement.get();
                entity->AddComponent(replacement.release());
                if (edited)
                {
                    // CPU preparation proved this node's ordered binding inventory
                    // and mesh bytes identical. Re-read explicit properties through
                    // the incoming scope so inline textures never borrow a retired reader.
                    component->DeserializeWithAssetManager(retainedProperties, reader);
                }
                else
                {
                    component->RetainAssetReader(reader);
                    component->SetMesh(mesh); component->SetMeshAssetReference(layout.meshReference);
                    component->SetSubmeshRange(static_cast<int>(binding.submeshIndex), 1);
                    component->SetMaterials(defaults);
                    for (std::size_t slot = 0; slot < incoming.defaultMaterials.size(); ++slot)
                        component->SetMaterialAssetForMaterialSlot(slot, incoming.defaultMaterials[slot]);
                    entity->SetName("Geometry " + std::to_string(binding.submeshIndex));
                    if (!entity->SetLocalTransformMatrix(binding.geometryToNode)) throw std::runtime_error("Incoming binding compensation is invalid.");
                }
                entity->SetParent(nodes.at(binding.nodeIndex));
                if (entity->GetParent() != nodes.at(binding.nodeIndex)) throw std::runtime_error("Could not reparent incoming geometry.");
                retained.insert(entity->GetID()); state.bindingEntities.push_back(entity->GetID());
            }
            for (const auto id : old.bindingEntities)
                if (!retained.contains(id)) if (auto *entity = scene.FindEntityByID(id)) scene.RemoveEntity(entity);
            for (auto index = old.nodeEntities.size(); index-- > 0;)
                if (!retained.contains(old.nodeEntities[index].sceneEntityId))
                    if (auto *entity = scene.FindEntityByID(old.nodeEntities[index].sceneEntityId)) scene.RemoveEntity(entity);
            std::string error;
            if (!scene.InstallStaticModelInstance({std::move(state), reader}, &error)) throw std::runtime_error(error);
        }
    }
    bool PrepareStaticModelSceneReconciliation(const Scene &source, const assets::Project &project,
        const assets::StaticModelGenerationSnapshot &incoming, assets::AssetManager &sharedAssets,
        PreparedStaticModelSceneReconciliation &output, std::string *errorMessage)
    {
        try
        {
            if (&sharedAssets != &core::Engine::GetInstance().GetAssetManager())
                throw std::runtime_error("Scene snapshot reconciliation requires the engine's active project resource context.");
            std::vector<const Entity *> pending(source.GetRootEntities().begin(), source.GetRootEntities().end());
            while (!pending.empty())
            {
                const auto *entity = pending.back(); pending.pop_back();
                if (source.GetSectionOwner(entity->GetID()))
                    throw std::runtime_error("Streamed scenes require section-scoped reconciliation.");
                for (const auto *child : entity->GetChildren()) pending.push_back(child);
            }
            PreparedStaticModelSceneReconciliation candidate;
            std::vector<InstanceUpdate> updates;
            std::string error;
            // Verify the incoming package and all resources even if every current
            // instance conflicts; invalid evidence is not a reviewable conflict.
            Scene incomingProof;
            if (!CreateStaticModelInstance(incomingProof, project, incoming, sharedAssets, "Incoming proof", nullptr, &error))
                throw std::runtime_error(error);
            auto reader = incomingProof.GetStaticModelInstances().begin()->second.resources;
            for (const auto &[root, instance] : source.GetStaticModelInstances())
            {
                if (instance.state.accepted.layout.sourceAssetId != incoming.generation.layout.sourceAssetId ||
                    instance.state.artifactGenerationKey == incoming.artifacts.generation) continue;
                InstanceUpdate update;
                if (!CaptureStaticModelInstance(source, instance, update.accepted, &error) ||
                    !assets::PrepareStaticModelInstanceReconciliation(update.accepted.accepted, update.accepted.overrides,
                        incoming.generation, update.prepared, &error)) throw std::runtime_error(error);
                if (update.prepared.CanPublish())
                {
                    for (std::size_t index = 0; index < update.accepted.bindingEntities.size(); ++index)
                    {
                        if (!HasBindingEdits(update, index)) continue;
                        const auto *component = source.FindEntityByID(update.accepted.bindingEntities[index])->GetComponent<MeshComponent>();
                        for (const auto &property : RetainedProperties(*component, *instance.resources))
                        {
                            assets::AssetReference identity;
                            if (assets::ParseAssetReference(property.value, identity) && identity.assetId == incoming.generation.layout.sourceAssetId &&
                                !reader->GetAssetCatalog()->Find(identity))
                            {
                                const auto node = update.accepted.accepted.layout.nodes.at(
                                    update.accepted.accepted.layout.bindings[index].nodeIndex).sourceNodeId;
                                update.prepared.conflicts.push_back({assets::StaticModelInstanceConflictKind::OverrideDependencyRemoved, node});
                            }
                        }
                    }
                }
                std::sort(update.prepared.conflicts.begin(), update.prepared.conflicts.end(), [](const auto &a, const auto &b)
                    { return a.sourceNodeId != b.sourceNodeId ? a.sourceNodeId < b.sourceNodeId : a.kind < b.kind; });
                update.prepared.conflicts.erase(std::unique(update.prepared.conflicts.begin(), update.prepared.conflicts.end(),
                    [](const auto &a, const auto &b) { return a.sourceNodeId == b.sourceNodeId && a.kind == b.kind; }), update.prepared.conflicts.end());
                if (!update.prepared.conflicts.empty())
                    candidate.conflicts.push_back({root, instance.state.artifactGenerationKey, incoming.artifacts.generation, update.prepared.conflicts});
                else updates.push_back(std::move(update));
            }
            if (!updates.empty())
            {
                std::string serialized;
                if (!SceneSerializer::SaveToString(source, serialized, &error)) throw std::runtime_error(error);
                candidate.scene = SceneSerializer::LoadFromString(serialized, &error);
                if (!candidate.scene) throw std::runtime_error(error);
                candidate.scene->SetFilePath(source.GetFilePath());
                std::string roundTrip;
                if (!SceneSerializer::SaveToString(*candidate.scene, roundTrip, &error) || roundTrip != serialized)
                    throw std::runtime_error(error.empty() ? "Scene snapshot could not round-trip without changing authored state." : error);
                for (const auto &[root, instance] : candidate.scene->GetStaticModelInstances())
                    if (instance.state.artifactGenerationKey == incoming.artifacts.generation)
                    {
                        if (!assets::ValidateStaticModelInstanceBaseline(instance.state, incoming, &error)) throw std::runtime_error(error);
                        reader = instance.resources; break;
                    }
                std::sort(updates.begin(), updates.end(), [](const auto &a, const auto &b) { return a.accepted.rootEntityId < b.accepted.rootEntityId; });
                for (const auto &update : updates)
                {
                    Apply(*candidate.scene, update, incoming, reader);
                    candidate.updatedRoots.push_back(update.accepted.rootEntityId);
                }
                if (!SceneSerializer::SaveToString(*candidate.scene, serialized, &error)) throw std::runtime_error(error);
            }
            std::sort(candidate.updatedRoots.begin(), candidate.updatedRoots.end());
            std::sort(candidate.conflicts.begin(), candidate.conflicts.end(), [](const auto &a, const auto &b) { return a.rootEntityId < b.rootEntityId; });
            output = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        { if (errorMessage) *errorMessage = std::string("Cannot prepare linked scene update: ") + error.what(); return false; }
    }
}
