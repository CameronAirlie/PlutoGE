#include "PlutoGE/scene/AssetReconciliation.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/assets/AssetManager.h"

#include <filesystem>
#include <algorithm>
#include <utility>
#include <optional>

namespace PlutoGE::scene
{
    namespace
    {
        std::string PathKey(const std::string &path)
        {
            const auto text = std::filesystem::path(path).lexically_normal().generic_u8string();
            std::string key(reinterpret_cast<const char *>(text.data()), text.size());
#ifdef _WIN32
            for (auto &character : key) if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
#endif
            return key;
        }
        std::optional<assets::AssetReference> Identity(const std::string &reference, const assets::AssetCatalog *catalog,
                                                       const assets::AssetManager &manager)
        {
            assets::AssetReference identity;
            if (assets::ParseAssetReference(reference, identity)) return identity;
            if (!catalog || reference.empty()) return std::nullopt;
            if (const auto found = catalog->FindIdentityByLocation(reference)) return found;
            const auto path = PathKey(manager.ResolveAssetPath(reference));
            std::optional<assets::AssetReference> found;
            for (const auto &object : catalog->GetObjects())
                if (PathKey(manager.ResolveAssetPath(object.location)) == path)
                {
                    if (found && found->localObjectId != 0 && object.identity.localObjectId != 0 && *found != object.identity) return std::nullopt;
                    if (!found || object.identity.localObjectId != 0) found = object.identity;
                }
            return found;
        }
        bool SameBinding(const std::string &first, const std::string &second, const assets::AssetCatalog *catalog,
                         const assets::AssetManager &manager)
        {
            if (first.empty() || second.empty()) return false;
            if (first == second) return true;
            const auto a = Identity(first, catalog, manager), b = Identity(second, catalog, manager);
            if (a && b) return *a == *b;
            const auto firstPath = manager.ResolveAssetPath(first), secondPath = manager.ResolveAssetPath(second);
            return !firstPath.empty() && !secondPath.empty() && PathKey(firstPath) == PathKey(secondPath);
        }
    }
    ModelAssetSnapshot CaptureModelAssetSnapshot(assets::AssetManager &manager, const std::string &sourceAssetId)
    {
        ModelAssetSnapshot result{sourceAssetId, manager.GetAssetCatalog()};
        if (result.catalog)
            for (const auto &object : result.catalog->GetObjects())
                if (object.identity.assetId == sourceAssetId && object.type == assets::ProjectAssetType::Mesh &&
                    object.ownership == assets::AssetOwnership::Imported)
                {
                    auto bindings = manager.GetMeshAssetMaterialReferences(object.location);
                    std::vector<render::Material *> materials;
                    for (const auto &reference : bindings) materials.push_back(manager.FindLoadedMaterialAsset(reference));
                    result.meshMaterialBindings.emplace(object.identity.localObjectId, std::move(bindings));
                    result.meshMaterials.emplace(object.identity.localObjectId, std::move(materials));
                }
        return result;
    }
    AssetReconciliationReport Scene::ApplyModelAssetGeneration(const std::string &sourceAssetId,
        const std::string &sourceReference, assets::AssetManager &manager, const ModelAssetSnapshot &previous)
    {
        AssetReconciliationReport report;
        const auto catalog = manager.GetAssetCatalog();
        if (!catalog || sourceAssetId.empty()) return report;
        std::vector<const assets::AssetObjectDescriptor *> importedMeshes;
        for (const auto &object : catalog->GetObjects())
            if (object.identity.assetId == sourceAssetId && object.type == assets::ProjectAssetType::Mesh &&
                object.ownership == assets::AssetOwnership::Imported) importedMeshes.push_back(&object);
        for (auto *component : m_meshComponents)
        {
            // Private generation readers require coordinated hierarchy publication.
            // Replacing just their mesh invalidates accepted baked compensation.
            if (component && component->GetRetainedAssetReader()) continue;
            if (!component || assets::Project::IsEngineAssetReference(component->GetMeshAssetReference())) continue;
            const auto currentReference = component->GetMeshAssetReference();
            auto identity = Identity(currentReference, catalog.get(), manager);
            if (component->GetModelAssetId() == sourceAssetId && component->GetModelObjectId() != 0)
                identity = assets::AssetReference{sourceAssetId, component->GetModelObjectId()};
            else if ((!identity || identity->assetId != sourceAssetId || identity->localObjectId == 0) &&
                     SameBinding(currentReference, sourceReference, catalog.get(), manager) && importedMeshes.size() == 1)
                identity = importedMeshes.front()->identity;
            if (!identity || identity->assetId != sourceAssetId || identity->localObjectId == 0) continue;
            const auto *object = catalog->Find(*identity);
            std::string logical;
            assets::SerializeAssetReference(*identity, logical);
            auto *mesh = object && object->type == assets::ProjectAssetType::Mesh && object->ownership == assets::AssetOwnership::Imported ?
                manager.LoadMeshAsset(logical) : nullptr;
            if (!mesh)
            {
                ++report.unresolvedMeshes;
                report.diagnostics.push_back("Imported mesh requires reference repair: " + logical);
                continue; // Keep the borrowed old generation alive and bound.
            }
            const auto defaults = manager.GetMeshAssetMaterialReferences(logical);
            const auto oldBindings = previous.meshMaterialBindings.find(identity->localObjectId);
            const auto oldMaterials = previous.meshMaterials.find(identity->localObjectId);
            for (std::size_t slot = 0; slot < defaults.size(); ++slot)
            {
                const auto reference = component->GetMaterialAssetForMaterialSlot(slot);
                const bool unassigned = slot >= component->GetMaterials().size() || component->GetMaterials()[slot] == nullptr;
                auto *currentMaterial = slot < component->GetMaterials().size() ? component->GetMaterials()[slot] : nullptr;
                auto *previousDefault = oldMaterials != previous.meshMaterials.end() && slot < oldMaterials->second.size() ? oldMaterials->second[slot] : nullptr;
                const bool inherited = (!currentMaterial || currentMaterial == previousDefault) && oldBindings != previous.meshMaterialBindings.end() && slot < oldBindings->second.size() &&
                    SameBinding(reference, oldBindings->second[slot], previous.catalog.get(), manager);
                if (inherited || (unassigned && reference.empty()))
                {
                    if (auto *material = manager.LoadMaterialAsset(defaults[slot]))
                    {
                        component->SetMaterialForMaterialSlot(slot, material);
                        component->SetMaterialAssetForMaterialSlot(slot, manager.PersistAssetPath(manager.ResolveAssetPath(defaults[slot])));
                    }
                }
                else if (unassigned && !reference.empty())
                    if (auto *material = manager.LoadMaterialAsset(reference)) component->SetMaterialForMaterialSlot(slot, material);
            }
            component->SetMesh(mesh);
            component->SetModelObjectIdentity(sourceAssetId, identity->localObjectId);
            if (!currentReference.starts_with("asset://")) component->SetMeshAssetReference(object->location);
            component->NotifyMeshDataChanged();
            if (auto *owner = component->GetOwner())
                for (auto *animation : owner->GetComponents<AnimationComponent>()) if (animation) animation->NotifyMeshChanged();
            ++report.refreshedMeshes;
        }
        std::sort(report.diagnostics.begin(), report.diagnostics.end());
        report.diagnostics.erase(std::unique(report.diagnostics.begin(), report.diagnostics.end()), report.diagnostics.end());
        if (report.refreshedMeshes != 0) MarkShadowLightsDirty();
        return report;
    }
}
