#include "PlutoGE/ui/FoliageEditing.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetType.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/FoliageEditTool.h"

#include <cctype>
#include <filesystem>
#include <utility>
#include <vector>

namespace PlutoGE::ui
{
    namespace
    {
        scene::FoliageComponent *FindFoliage(scene::EntityID entityId)
        {
            auto *scene = EditorShell::GetInstance().GetScene();
            auto *entity = scene ? scene->FindEntityByID(entityId) : nullptr;
            return entity ? entity->GetComponent<scene::FoliageComponent>() : nullptr;
        }

        std::size_t SnapshotBytes(const scene::FoliageInstanceSnapshot &snapshot)
        {
            std::size_t bytes = snapshot.size() * sizeof(std::vector<scene::FoliageInstance>);
            for (const auto &instances : snapshot) bytes += instances.size() * sizeof(scene::FoliageInstance);
            return bytes;
        }

        void PushInstanceTransformCommand(EditorShell &shell, scene::EntityID entityId, std::string label,
                                          std::vector<scene::FoliageInstance> before, std::vector<scene::FoliageInstance> after)
        {
            const std::size_t bytes = (before.size() + after.size()) * sizeof(scene::FoliageInstance);
            const auto apply = [entityId](const std::vector<scene::FoliageInstance> &instances)
            {
                auto *foliage = FindFoliage(entityId);
                if (!foliage) return false;
                foliage->SetInstances(instances);
                EditorShell::GetInstance().MarkSceneDirty();
                return true;
            };
            shell.PushSceneEditCommand(std::move(label),
                                       [apply, instances = std::move(before)] { return apply(instances); },
                                       [apply, instances = std::move(after)] { return apply(instances); },
                                       bytes);
        }

        // Overrides for every type a group of instances belongs to.
        void RecordOverridesFor(scene::Entity &entity, scene::FoliageComponent &foliage, std::span<const scene::FoliageInstance> instances)
        {
            std::vector<bool> touched(foliage.GetTypeCount(), false);
            for (const auto &instance : instances)
                if (const auto location = foliage.FindInstance(instance.id); location && !touched[location->typeIndex])
                {
                    touched[location->typeIndex] = true;
                    RecordFoliageInstanceOverride(entity, location->typeIndex);
                }
        }
    }

    void RecordFoliageTypeOverride(scene::Entity &entity, std::size_t typeIndex, std::string_view property)
    {
        entity.AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(typeIndex) + "." + std::string(property));
    }

    void RecordFoliageInstanceOverride(scene::Entity &entity, std::size_t typeIndex)
    {
        RecordFoliageTypeOverride(entity, typeIndex, "Instances");
    }

    void PushFoliageSnapshotCommand(EditorShell &shell, scene::EntityID entity, std::string label,
                                    scene::FoliageInstanceSnapshot before, scene::FoliageInstanceSnapshot after)
    {
        const std::size_t bytes = SnapshotBytes(before) + SnapshotBytes(after);
        const auto restore = [entity](const scene::FoliageInstanceSnapshot &snapshot)
        {
            auto *foliage = FindFoliage(entity);
            if (!foliage) return false;
            foliage->RestoreInstanceSnapshot(snapshot);
            EditorShell::GetInstance().MarkSceneDirty();
            return true;
        };
        shell.PushSceneEditCommand(std::move(label),
                                   [restore, snapshot = std::move(before)] { return restore(snapshot); },
                                   [restore, snapshot = std::move(after)] { return restore(snapshot); },
                                   bytes);
    }

    bool RunFoliageInstanceEdit(EditorShell &shell, scene::Entity &entity, scene::FoliageComponent &foliage,
                                std::string label, const std::function<void()> &edit)
    {
        shell.FlushPendingSceneEdits();
        auto before = foliage.CaptureInstanceSnapshot();
        edit();
        auto after = foliage.CaptureInstanceSnapshot();
        if (before == after) return false;
        for (std::size_t typeIndex = 0; typeIndex < after.size(); ++typeIndex)
            if (typeIndex >= before.size() || before[typeIndex] != after[typeIndex])
                RecordFoliageInstanceOverride(entity, typeIndex);
        PushFoliageSnapshotCommand(shell, entity.GetID(), std::move(label), std::move(before), std::move(after));
        return true;
    }

    void BeginFoliageTransform(EditorShell &shell, FoliageEditTool &tool, scene::FoliageComponent &foliage)
    {
        shell.FlushPendingSceneEdits();
        tool.transformBefore.clear();
        for (const auto &location : tool.selection.Resolve(foliage))
            tool.transformBefore.push_back(*foliage.GetInstance(location));
    }

    void UpdateFoliageTransform(FoliageEditTool &, scene::FoliageComponent &foliage, std::span<const scene::FoliageInstance> instances)
    {
        foliage.SetInstances(instances);
    }

    void CommitFoliageTransform(EditorShell &shell, FoliageEditTool &tool, scene::Entity &entity,
                                scene::FoliageComponent &foliage, std::string label)
    {
        auto before = std::move(tool.transformBefore);
        tool.transformBefore.clear();
        std::vector<scene::FoliageInstance> after;
        after.reserve(before.size());
        for (const auto &instance : before)
        {
            const auto location = foliage.FindInstance(instance.id);
            after.push_back(location ? *foliage.GetInstance(*location) : instance);
        }
        if (before == after) return;
        RecordOverridesFor(entity, foliage, after);
        PushInstanceTransformCommand(shell, entity.GetID(), std::move(label), std::move(before), std::move(after));
    }

    void CancelFoliageTransform(FoliageEditTool &tool, scene::FoliageComponent &foliage)
    {
        foliage.SetInstances(tool.transformBefore);
        tool.transformBefore.clear();
    }

    void DeleteSelectedFoliageInstances(EditorShell &shell, FoliageEditTool &tool, scene::Entity &entity, scene::FoliageComponent &foliage)
    {
        tool.selection.Resolve(foliage);
        if (tool.selection.Empty()) return;
        shell.FlushPendingSceneEdits();

        auto removed = foliage.RemoveInstances(tool.selection.Ids());
        for (const auto &entry : removed) RecordFoliageInstanceOverride(entity, entry.location.typeIndex);
        tool.selection.Clear();

        const auto entityId = entity.GetID();
        const auto bytes = removed.size() * sizeof(scene::FoliageRemovedInstance);
        const std::string label = removed.size() == 1 ? "Delete Foliage Instance" : "Delete Foliage Instances";
        auto ids = std::vector<std::uint64_t>{};
        ids.reserve(removed.size());
        for (const auto &entry : removed) ids.push_back(entry.instance.id);
        shell.PushSceneEditCommand(
            label,
            [entityId, removed = std::move(removed)]
            {
                auto *target = FindFoliage(entityId);
                if (!target) return false;
                target->RestoreInstances(removed);
                EditorShell::GetInstance().MarkSceneDirty();
                return true;
            },
            [entityId, ids = std::move(ids)]
            {
                auto *target = FindFoliage(entityId);
                if (!target) return false;
                target->RemoveInstances(ids);
                EditorShell::GetInstance().MarkSceneDirty();
                return true;
            },
            bytes);
    }

    std::size_t SnapSelectedFoliageInstances(EditorShell &shell, FoliageEditTool &tool, scene::Entity &entity,
                                             scene::FoliageComponent &foliage, const std::function<float(float, float)> &sampleHeight)
    {
        if (!sampleHeight) return 0;
        BeginFoliageTransform(shell, tool, foliage);
        std::vector<scene::FoliageInstance> snapped = tool.transformBefore;
        for (auto &instance : snapped)
            instance.position.y = sampleHeight(instance.position.x, instance.position.z);
        const std::size_t moved = foliage.SetInstances(snapped);
        CommitFoliageTransform(shell, tool, entity, foliage, "Snap Foliage Instances");
        return moved;
    }

    void ApplyFoliageTypeEdit(EditorShell &shell, scene::Entity &entity, scene::FoliageComponent &foliage,
                              std::size_t typeIndex, const scene::FoliageTypeAsset &edited)
    {
        const auto *type = foliage.GetType(typeIndex);
        if (!type) return;
        const scene::FoliageTypeAsset before = type->asset;
        foliage.ApplyTypeAsset(typeIndex, edited);
        const auto changed = scene::ChangedFoliageTypeProperties(before, foliage.GetType(typeIndex)->asset);
        if (changed.empty()) return;
        for (const auto property : changed) RecordFoliageTypeOverride(entity, typeIndex, property);
        shell.MarkSceneDirty();
    }

    void GenerateFoliageTypeLods(EditorShell &shell, scene::Entity &entity, scene::FoliageComponent &foliage, std::size_t typeIndex)
    {
        const auto *type = foliage.GetType(typeIndex);
        if (!type) return;
        const std::string source = type->asset.sourceMeshPath;
        auto &engine = core::Engine::GetInstance();
        try
        {
            auto imported = engine.GenerateMeshAssetLods(engine.GetAssetManager().ResolveMeshAssetSourcePath(source));
            if (!imported.mesh)
            {
                shell.Log(EditorShell::ConsoleSeverity::Error, "Failed to generate foliage LODs: " + source);
                return;
            }
            foliage.SetTypeMeshAndMaterials(typeIndex, imported.mesh, imported.materials, source);
            foliage.SetTypeUseGeneratedLods(typeIndex, true);
            RecordFoliageTypeOverride(entity, typeIndex, "SourceMesh");
            RecordFoliageTypeOverride(entity, typeIndex, "UseGeneratedLods");
            shell.MarkSceneDirty();
            shell.Log(EditorShell::ConsoleSeverity::Info, "Generated foliage LODs: " + source);
        }
        catch (const std::exception &exception)
        {
            shell.Log(EditorShell::ConsoleSeverity::Error, std::string("Failed to generate foliage LODs: ") + exception.what());
        }
    }

    void SetFoliageAssetMesh(scene::FoliageTypeAsset &asset, const std::string &reference)
    {
        const auto &metadata = core::Engine::GetInstance().GetAssetManager().GetMeshAssetMetadata(reference);
        asset.sourceMeshPath = reference;
        asset.modelAssetId = metadata.sourceAssetId;
        asset.modelObjectId = metadata.sourceObjectId;
        // A different mesh has different parts.
        asset.submeshIndex = -1;
        asset.submeshIndices.clear();
        asset.useGeneratedLods = false;
    }

    scene::FoliageTypeAsset MakeFoliageAssetForMesh(const std::string &meshReference)
    {
        scene::FoliageTypeAsset asset;
        if (!meshReference.empty()) SetFoliageAssetMesh(asset, meshReference);
        return asset;
    }

    bool CanCreateFoliageTypeFrom(const std::string &reference)
    {
        const auto type = assets::Project::GetAssetTypeForReference(reference);
        return type == assets::ProjectAssetType::Mesh || type == assets::ProjectAssetType::Model ||
               type == assets::ProjectAssetType::FoliageType;
    }

    bool AddFoliageTypeFromAsset(EditorShell &shell, scene::Entity &entity, scene::FoliageComponent &foliage, const std::string &reference)
    {
        if (!CanCreateFoliageTypeFrom(reference))
        {
            shell.Log(EditorShell::ConsoleSeverity::Warning, "A foliage type needs a mesh, model or foliage type asset: " + reference);
            return false;
        }
        const bool linked = assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::FoliageType;
        bool succeeded = false;
        shell.ExecuteSceneEdit("Add Foliage Type", [&]
        {
            const auto name = std::filesystem::path(reference).stem().string();
            foliage.AddType(name);
            const std::size_t index = foliage.GetTypeCount() - 1;
            std::string error;
            if (linked && !foliage.SetTypeAssetReference(index, reference, &error))
            {
                foliage.RemoveType(index);
                shell.Log(EditorShell::ConsoleSeverity::Error, error);
                return;
            }
            if (!linked) foliage.ApplyTypeAsset(index, MakeFoliageAssetForMesh(reference));
            entity.AddPrefabOverride("Component:FoliageComponent:FoliageTypeCount");
            entity.AddPrefabOverride("Component:FoliageComponent:SelectedType");
            for (const auto &property : foliage.Serialize())
            {
                const std::string prefix = "Type." + std::to_string(index) + ".";
                if (property.name.rfind(prefix, 0) == 0)
                    entity.AddPrefabOverride("Component:FoliageComponent:" + property.name);
            }
            succeeded = true;
        });
        return succeeded;
    }

    void RemoveFoliageType(EditorShell &shell, scene::Entity &entity, scene::FoliageComponent &foliage, std::size_t typeIndex)
    {
        shell.ExecuteSceneEdit("Remove Foliage Type", [&]
        {
            foliage.RemoveType(typeIndex);
            entity.SetPrefabOverrides(scene::RemapFoliageOverridesAfterTypeRemoval(entity.GetPrefabOverrides(), typeIndex));
            entity.AddPrefabOverride("Component:FoliageComponent:FoliageTypeCount");
            entity.AddPrefabOverride("Component:FoliageComponent:SelectedType");
        });
    }

    std::string CreateFoliageTypeAssetFile(EditorShell &shell, const std::string &name, const scene::FoliageTypeAsset &asset)
    {
        auto *project = shell.GetProject();
        if (!project)
        {
            shell.Log(EditorShell::ConsoleSeverity::Error, "Open a project to save foliage type assets.");
            return {};
        }
        std::string safeName;
        for (const char character : name)
            safeName += std::isalnum(static_cast<unsigned char>(character)) || character == '_' || character == '-' ? character : '_';
        if (safeName.empty()) safeName = "FoliageType";

        const auto directory = std::filesystem::path(project->GetAssetDirectoryPath()) / "Foliage";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        auto path = directory / (safeName + ".plutofoliage");
        for (int suffix = 2; std::filesystem::exists(path, error); ++suffix)
            path = directory / (safeName + "_" + std::to_string(suffix) + ".plutofoliage");

        std::string message;
        scene::FoliageTypeAsset stored = asset;
        stored.assetReference.clear();
        if (!scene::SaveFoliageTypeAsset(path.string(), stored, &message))
        {
            shell.Log(EditorShell::ConsoleSeverity::Error, message);
            return {};
        }
        shell.RefreshProjectAssets();
        shell.MarkProjectDirty();
        return project->MakeAssetReference(path);
    }

    bool LinkFoliageType(EditorShell &shell, scene::Entity &entity, scene::FoliageComponent &foliage, std::size_t typeIndex, const std::string &reference)
    {
        bool linked = false;
        shell.ExecuteSceneEdit("Link Foliage Type", [&]
        {
            const auto *type = foliage.GetType(typeIndex);
            if (!type) return;
            const scene::FoliageTypeAsset before = type->asset;
            std::string error;
            if (!foliage.SetTypeAssetReference(typeIndex, reference, &error))
            {
                shell.Log(EditorShell::ConsoleSeverity::Error, error);
                return;
            }
            for (const auto property : scene::ChangedFoliageTypeProperties(before, foliage.GetType(typeIndex)->asset))
                RecordFoliageTypeOverride(entity, typeIndex, property);
            linked = true;
        });
        return linked;
    }

    void MakeFoliageTypeLocal(EditorShell &shell, scene::Entity &entity, scene::FoliageComponent &foliage, std::size_t typeIndex)
    {
        shell.ExecuteSceneEdit("Make Foliage Type Local", [&]
        {
            foliage.SetTypeAssetReference(typeIndex, {});
            RecordFoliageTypeOverride(entity, typeIndex, "AssetReference");
        });
    }
}
