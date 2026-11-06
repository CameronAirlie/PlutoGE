#pragma once

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/FoliageComponent.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>

// The undoable edits behind the foliage inspector, viewport tool and asset editor, so each records history, dirty state
// and prefab overrides the same way. Instance edits record only what they changed; structural edits (types) use the
// scene's whole-state history, because they are rare and touch everything.
namespace PlutoGE::ui
{
    class EditorShell;
    class FoliageEditTool;

    // Prefab overrides name real serialized properties, so painted instances are overridden per type.
    void RecordFoliageTypeOverride(scene::Entity &entity, std::size_t typeIndex, std::string_view property);
    void RecordFoliageInstanceOverride(scene::Entity &entity, std::size_t typeIndex);

    // Runs `edit`, which changes instances of `foliage`, as one undo step holding before and after snapshots. Returns
    // whether anything changed.
    bool RunFoliageInstanceEdit(EditorShell &shell, scene::Entity &entity, scene::FoliageComponent &foliage,
                                std::string label, const std::function<void()> &edit);
    // The same, for a change that has already happened.
    void PushFoliageSnapshotCommand(EditorShell &shell, scene::EntityID entity, std::string label,
                                    scene::FoliageInstanceSnapshot before, scene::FoliageInstanceSnapshot after);

    // A transform gesture on the selected instances, from a gizmo or a field: Begin once, Update as it moves, then
    // Commit (one undo step) or Cancel.
    void BeginFoliageTransform(EditorShell &shell, FoliageEditTool &tool, scene::FoliageComponent &foliage);
    void UpdateFoliageTransform(FoliageEditTool &tool, scene::FoliageComponent &foliage,
                                std::span<const scene::FoliageInstance> instances);
    void CommitFoliageTransform(EditorShell &shell, FoliageEditTool &tool, scene::Entity &entity,
                                scene::FoliageComponent &foliage, std::string label);
    void CancelFoliageTransform(FoliageEditTool &tool, scene::FoliageComponent &foliage);

    // Removes the selected instances as one undo step.
    void DeleteSelectedFoliageInstances(EditorShell &shell, FoliageEditTool &tool, scene::Entity &entity, scene::FoliageComponent &foliage);
    // Drops the selected instances onto `sampleHeight` as one undo step. Returns how many moved.
    std::size_t SnapSelectedFoliageInstances(EditorShell &shell, FoliageEditTool &tool, scene::Entity &entity,
                                             scene::FoliageComponent &foliage, const std::function<float(float, float)> &sampleHeight);

    // A type's settings from an inspector edit: applies them, marks the scene dirty and records overrides.
    void ApplyFoliageTypeEdit(EditorShell &shell, scene::Entity &entity, scene::FoliageComponent &foliage,
                              std::size_t typeIndex, const scene::FoliageTypeAsset &edited);

    // Builds LODs for the type's mesh now and uses them. Logs why when it cannot.
    void GenerateFoliageTypeLods(EditorShell &shell, scene::Entity &entity, scene::FoliageComponent &foliage, std::size_t typeIndex);

    // Points `asset` at a mesh or model, filling in the model identity that lets it follow reimports.
    void SetFoliageAssetMesh(scene::FoliageTypeAsset &asset, const std::string &reference);
    // A new asset that draws `meshReference`, with the defaults for anything else.
    [[nodiscard]] scene::FoliageTypeAsset MakeFoliageAssetForMesh(const std::string &meshReference);
    // Whether a dragged asset can become a foliage type (a mesh, a model or a .plutofoliage file).
    [[nodiscard]] bool CanCreateFoliageTypeFrom(const std::string &reference);
    // Adds a type from a mesh or model (named after it) or a .plutofoliage file (linked to it) and selects it, as one
    // undo step. Returns false, logging why, when the asset cannot be used.
    bool AddFoliageTypeFromAsset(EditorShell &shell, scene::Entity &entity, scene::FoliageComponent &foliage, const std::string &reference);
    void RemoveFoliageType(EditorShell &shell, scene::Entity &entity, scene::FoliageComponent &foliage, std::size_t typeIndex);

    // Writes `asset` to a new .plutofoliage file named after `name` under the project's Foliage folder, without
    // replacing an existing one. Returns its reference, or empty after logging why not.
    [[nodiscard]] std::string CreateFoliageTypeAssetFile(EditorShell &shell, const std::string &name, const scene::FoliageTypeAsset &asset);
    // Links a type to an asset file, replacing its settings with the file's. Logs and returns false if it cannot be read.
    bool LinkFoliageType(EditorShell &shell, scene::Entity &entity, scene::FoliageComponent &foliage, std::size_t typeIndex, const std::string &reference);
    // Keeps a linked type's current settings in the scene and drops the link.
    void MakeFoliageTypeLocal(EditorShell &shell, scene::Entity &entity, scene::FoliageComponent &foliage, std::size_t typeIndex);
}
