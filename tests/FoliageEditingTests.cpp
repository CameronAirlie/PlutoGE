// Foliage authoring without a UI: the .plutofoliage format, linked types, old scene compatibility, instance operations,
// picking, selection, prefab override upkeep and diagnostics.
#include "PlutoGE/assets/AssetReferences.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/ui/FoliageDiagnostics.h"
#include "PlutoGE/ui/FoliageEditTool.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE;
using namespace PlutoGE::scene;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    bool Near(float a, float b, float tolerance = 1e-3f) { return std::abs(a - b) <= tolerance; }
    bool Near(const glm::vec3 &a, const glm::vec3 &b, float tolerance = 1e-3f) { return glm::length(a - b) <= tolerance; }

    struct Fixture
    {
        Scene scene;
        Entity *owner = nullptr;
        FoliageComponent *foliage = nullptr;

        Fixture()
        {
            owner = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Forest"}));
            foliage = owner->CreateComponent<FoliageComponent>();
        }

        // Normalizes hand-placed instances the way loading does.
        void Normalize() { foliage->RestoreInstanceSnapshot(foliage->CaptureInstanceSnapshot()); }
    };

    // A thin vertical quad at the origin in the XY plane, 1 wide and 2 tall: a "trunk" with most of its bounding sphere
    // empty, which is what makes triangle tests matter when picking.
    render::MeshConfig QuadMesh()
    {
        const auto vertex = [](float x, float y)
        {
            render::MeshVertexData data{};
            data.position = {x, y, 0.0f};
            data.normal = {0.0f, 0.0f, 1.0f};
            data.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
            return data;
        };
        render::MeshConfig config{};
        config.data.vertices = {vertex(-0.5f, 0.0f), vertex(0.5f, 0.0f), vertex(0.5f, 2.0f), vertex(-0.5f, 2.0f)};
        config.data.indices = {0, 1, 2, 0, 2, 3};
        return config;
    }

    FoliageTypeAsset FullAsset()
    {
        FoliageTypeAsset asset;
        asset.sourceMeshPath = "project://Trees/Oak Large.plutomesh";
        asset.modelAssetId = "oak-model-id";
        asset.modelObjectId = 12345678901234ull;
        asset.materialAssetReference = "project://Materials/Bark.plutomaterial";
        asset.submeshIndices = {1, 3};
        asset.useGeneratedLods = true;
        asset.minScale = 0.5f;
        asset.maxScale = 2.5f;
        asset.alignToTerrainNormal = true;
        asset.cellSize = 64.0f;
        asset.maxDrawDistance = 120.0f;
        asset.lodDistanceScale = 1.5f;
        asset.collisionEnabled = true;
        asset.collisionCenter = {0.0f, 1.5f, 0.25f};
        asset.collisionRadius = 0.4f;
        asset.collisionHeight = 3.0f;
        asset.wind = render::FoliageWindPresetResponse(render::FoliageWindPreset::Tree, 6.0f);
        return asset;
    }

    void TestAssetRoundTrip()
    {
        const auto asset = FullAsset();
        std::stringstream file;
        Require(WriteFoliageTypeAsset(file, asset), "Writing an asset should succeed");
        Require(file.str().rfind("PLUTOFOLIAGE\t2\n", 0) == 0, "New assets should be version 2");

        FoliageTypeAsset loaded;
        Require(ReadFoliageTypeAsset(file, loaded), "Reading an asset back should succeed");
        FoliageTypeAsset expected = asset;
        expected.wind = expected.wind.Sanitized();
        Require(loaded == expected, "Every setting should survive the file");

        // A file does not name itself: a reference is where it was loaded from.
        std::stringstream linked;
        WriteFoliageTypeAsset(linked, asset);
        FoliageTypeAsset target;
        target.assetReference = "project://Oak.plutofoliage";
        ReadFoliageTypeAsset(linked, target);
        Require(target.assetReference == "project://Oak.plutofoliage", "Reading should keep the caller's reference");

        std::stringstream singlePart;
        singlePart << "PLUTOFOLIAGE\t2\nSUBMESHES\t4\n";
        FoliageTypeAsset one;
        ReadFoliageTypeAsset(singlePart, one);
        Require(one.submeshIndex == 4 && one.submeshIndices == std::vector<int>{4}, "One part should set the single index too");
    }

    void TestAssetVersions()
    {
        // Version 1 holds policy only, so a type linked to one keeps the mesh and placement the scene gave it.
        std::stringstream legacy;
        legacy << "PLUTOFOLIAGE\t1\r\nCELL_SIZE\t16\r\nCOLLISION_ENABLED\t1\r\n";
        FoliageTypeAsset asset;
        asset.sourceMeshPath = "project://Keep.plutomesh";
        asset.minScale = 2.0f;
        asset.maxScale = 3.0f;
        asset.alignToTerrainNormal = true;
        asset.cellSize = 99.0f;
        Require(ReadFoliageTypeAsset(legacy, asset), "Version 1 should load");
        Require(asset.sourceMeshPath == "project://Keep.plutomesh" && Near(asset.minScale, 2.0f) && asset.alignToTerrainNormal,
                "Version 1 should keep prototype and placement values");
        Require(Near(asset.cellSize, 16.0f) && asset.collisionEnabled, "Version 1 should still set its own records");

        // Version 2 is the whole definition: what it omits is the default, not whatever was there.
        std::stringstream current;
        current << "PLUTOFOLIAGE\t2\nCELL_SIZE\t16\n";
        FoliageTypeAsset replaced;
        replaced.sourceMeshPath = "project://Gone.plutomesh";
        Require(ReadFoliageTypeAsset(current, replaced), "Version 2 should load");
        Require(replaced.sourceMeshPath.empty(), "Version 2 should replace the prototype");

        for (const char *bad : {"", "NOTFOLIAGE\t1\n", "PLUTOFOLIAGE\t0\n", "PLUTOFOLIAGE\t99\n"})
        {
            std::stringstream invalid(bad);
            FoliageTypeAsset untouched;
            untouched.cellSize = 7.0f;
            std::string error;
            Require(!ReadFoliageTypeAsset(invalid, untouched, &error) && !error.empty(), "An invalid file should report why");
            Require(Near(untouched.cellSize, 7.0f), "An invalid file should change nothing");
        }

        std::stringstream hostile;
        hostile << "PLUTOFOLIAGE\t2\nSCALE_RANGE\t5\t1\nCOLLISION_CAPSULE\t1\t0.5\nLOD_DISTANCE_SCALE\t1000\nCELL_SIZE\t-3\n";
        FoliageTypeAsset clamped;
        ReadFoliageTypeAsset(hostile, clamped);
        Require(Near(clamped.minScale, 1.0f) && Near(clamped.maxScale, 5.0f), "A reversed scale range should be put in order");
        Require(clamped.collisionHeight >= clamped.collisionRadius * 2.0f, "A capsule should hold its own radius");
        Require(Near(clamped.lodDistanceScale, render::MaxLodDistanceScale) && clamped.cellSize >= 1.0f, "Values should be clamped");
    }

    void TestChangedProperties()
    {
        const auto base = FullAsset();
        Require(ChangedFoliageTypeProperties(base, base).empty(), "Identical assets should report nothing");
        auto edited = base;
        edited.minScale = 9.0f;
        edited.wind.flutter = base.wind.flutter + 0.1f;
        const auto changed = ChangedFoliageTypeProperties(base, edited);
        Require(changed.size() == 2 && changed[0] == "MinScale" && changed[1] == "WindFlutter", "Only the edited fields should be reported");

        // These names must be real serialized properties: prefab overrides are read back through them.
        Fixture fixture;
        fixture.foliage->AddType("Oak");
        const auto properties = fixture.foliage->Serialize();
        const auto all = ChangedFoliageTypeProperties(FoliageTypeAsset{}, base);
        for (const auto name : all)
        {
            bool found = false;
            for (const auto &property : properties)
                found = found || property.name.ends_with("." + std::string(name));
            Require(found, "Not a serialized property: " + std::string(name));
        }
    }

    void TestAssetReferenceScan()
    {
        const auto directory = std::filesystem::temp_directory_path() / "PlutoGEFoliageEditingTests";
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        const auto path = directory / "Oak.plutofoliage";
        std::string error;
        Require(SaveFoliageTypeAsset(path.string(), FullAsset(), &error), error);

        Require(assets::Project::GetAssetTypeForReference("project://Oak.plutofoliage") == assets::ProjectAssetType::FoliageType,
                "The extension should be a recognized asset type");
        Require(assets::SupportsAssetReferenceScan(path), "Foliage assets should be scanned so their dependencies cook");
        const auto scan = assets::ScanAssetReferences(path);
        Require(scan.errors.empty(), "Scanning a foliage asset should not fail");
        std::vector<std::string> references;
        for (const auto &occurrence : scan.occurrences) references.push_back(occurrence.reference);
        const auto has = [&](const std::string &wanted) { return std::find(references.begin(), references.end(), wanted) != references.end(); };
        Require(has("project://Trees/Oak Large.plutomesh") && has("project://Materials/Bark.plutomaterial"),
                "The mesh and material should be dependencies");
        Require(references.size() == 2, "Only references should be reported as dependencies");
        std::filesystem::remove_all(directory);
    }

    void TestOldSceneCompatibility()
    {
        // A scene written before brush settings left the component and scale ranges moved to types.
        Fixture fixture;
        fixture.foliage->Deserialize({
            {"PaintEnabled", PropertyType::Bool, "true"},
            {"BrushMode", PropertyType::String, "Remove"},
            {"BrushRadius", PropertyType::Float, "9"},
            {"Density", PropertyType::Int, "7"},
            {"MinScale", PropertyType::Float, "0.4"},
            {"MaxScale", PropertyType::Float, "0.6"},
            {"FoliageTypeCount", PropertyType::Int, "2"},
            {"Type.0.Name", PropertyType::String, "Old"},
            {"Type.0.AlignToTerrainNormal", PropertyType::Bool, "true"},
            {"Type.1.Name", PropertyType::String, "New"},
            {"Type.1.MinScale", PropertyType::Float, "2"},
            {"Type.1.MaxScale", PropertyType::Float, "3"},
        });
        const auto *old = fixture.foliage->GetType(0);
        const auto *current = fixture.foliage->GetType(1);
        Require(Near(old->asset.minScale, 0.4f) && Near(old->asset.maxScale, 0.6f), "An older type should inherit the component's range");
        Require(old->asset.alignToTerrainNormal, "An older type should keep its growth orientation");
        Require(Near(current->asset.minScale, 2.0f) && Near(current->asset.maxScale, 3.0f), "A type's own range should win");

        for (const auto &property : fixture.foliage->Serialize())
            Require(!fixture.foliage->IsRetiredProperty(property.name), "Retired property written: " + property.name);
        for (const char *name : {"PaintEnabled", "BrushMode", "BrushRadius", "Density", "MinScale", "MaxScale", "Instances"})
            Require(fixture.foliage->IsRetiredProperty(name), std::string("Should be retired: ") + name);
        Require(!fixture.foliage->IsRetiredProperty("MaxDrawDistance"), "Live properties are not retired");

        Fixture fresh;
        Require(fresh.foliage->GetMinRenderLod() == 0 && fresh.foliage->GetMinShadowLod() == 0, "New foliage should use every LOD");
    }

    void TestLinkedTypes()
    {
        const auto directory = std::filesystem::temp_directory_path() / "PlutoGEFoliageLinkedTests";
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        const auto file = directory / "Oak.plutofoliage";
        const std::string reference = file.string();

        Fixture fixture;
        auto &foliage = *fixture.foliage;
        foliage.AddType("Oak");
        const std::size_t index = foliage.GetTypeCount() - 1;

        FoliageTypeAsset asset;
        asset.cellSize = 48.0f;
        std::string error;
        Require(SaveFoliageTypeAsset(reference, asset, &error), error);
        Require(foliage.SetTypeAssetReference(index, reference, &error), "Linking should succeed: " + error);
        Require(foliage.GetType(index)->asset.assetReference == reference && Near(foliage.GetType(index)->asset.cellSize, 48.0f),
                "Linking should take the file's settings");

        // Saving the asset reaches linked types; others are left alone.
        foliage.AddType("Other");
        asset.cellSize = 96.0f;
        Require(SaveFoliageTypeAsset(reference, asset, &error), error);
        Require(fixture.scene.RefreshFoliageTypeAsset(reference) == 1, "Exactly the linked type should reload");
        Require(Near(foliage.GetType(index)->asset.cellSize, 96.0f), "A linked type should follow its file");
        Require(Near(foliage.GetType(index + 1)->asset.cellSize, 32.0f), "An unlinked type should not");

        // The scene's stale copy never beats the file, and a bad file falls back to it without losing the link.
        auto properties = foliage.Serialize();
        for (auto &property : properties)
            if (property.name == "Type." + std::to_string(index) + ".CellSize") property.value = "10";
        foliage.Deserialize(properties);
        Require(Near(foliage.GetType(index)->asset.cellSize, 96.0f), "The file should win on load");
        Require(foliage.GetType(index)->assetLoadError.empty(), "A good load should report nothing");

        std::filesystem::remove(file);
        foliage.Deserialize(properties);
        const auto *broken = foliage.GetType(index);
        Require(Near(broken->asset.cellSize, 10.0f) && broken->asset.assetReference == reference, "A missing file should fall back, keeping the link");
        Require(!broken->assetLoadError.empty(), "A missing file should say so");
        const auto issues = ui::DiagnoseFoliageType(*broken);
        Require(!issues.empty() && ui::WorstFoliageIssue(issues) == ui::FoliageIssueSeverity::Error, "The failure should reach the diagnostics");

        const auto before = broken->asset;
        Require(!foliage.SetTypeAssetReference(index + 1, reference, &error) && !error.empty(), "Linking to a missing file should fail");
        Require(foliage.GetType(index + 1)->asset.assetReference.empty(), "A failed link should change nothing");

        // Making a type local keeps what it has.
        Require(foliage.SetTypeAssetReference(index, {}), "Unlinking should succeed");
        Require(foliage.GetType(index)->asset.assetReference.empty() && Near(foliage.GetType(index)->asset.cellSize, before.cellSize),
                "Unlinking should keep the values");
        Require(foliage.GetType(index)->assetLoadError.empty(), "Unlinking should clear the error");
        std::filesystem::remove_all(directory);
    }

    void TestInstanceOperations()
    {
        Fixture fixture;
        auto &foliage = *fixture.foliage;
        auto &first = foliage.AddType("First");
        first.instances = {FoliageInstance{.id = 1, .position = {1, 0, 0}}, FoliageInstance{.id = 2, .position = {2, 0, 0}},
                           FoliageInstance{.id = 3, .position = {3, 0, 0}}};
        auto &second = foliage.AddType("Second");
        second.instances = {FoliageInstance{.id = 4, .position = {4, 0, 0}}};
        fixture.Normalize();
        const std::size_t firstIndex = foliage.GetTypeCount() - 2;

        const auto found = foliage.FindInstance(4);
        Require(found && found->typeIndex == firstIndex + 1 && found->instanceIndex == 0, "IDs should resolve across types");
        Require(!foliage.FindInstance(999), "An unknown ID should not resolve");
        const FoliageInstanceLocation stale{firstIndex, 0};
        Require(*foliage.FindInstance(2, stale) == FoliageInstanceLocation{firstIndex, 1}, "A stale hint should still find the instance");

        const auto revision = foliage.GetRevision();
        FoliageInstance moved = *foliage.GetInstance(*foliage.FindInstance(2));
        moved.position = {20, 0, 0};
        moved.scale = {-1, 2, 2};
        const FoliageInstance ghost{.id = 999};
        const std::vector edits{moved, ghost};
        Require(foliage.SetInstances(edits) == 1, "Only known instances should change");
        Require(foliage.GetRevision() != revision, "An edit should invalidate derived data");
        Require(foliage.GetInstance(*foliage.FindInstance(2))->scale.x > 0.0f, "Scale should be kept positive");
        const auto unchangedRevision = foliage.GetRevision();
        const std::vector same{*foliage.GetInstance(*foliage.FindInstance(2))};
        Require(foliage.SetInstances(same) == 0 && foliage.GetRevision() == unchangedRevision, "A no-op should not invalidate");

        const auto snapshot = foliage.CaptureInstanceSnapshot();
        const std::vector<std::uint64_t> doomed{3, 1, 4, 999};
        const auto removed = foliage.RemoveInstances(doomed);
        Require(removed.size() == 3 && foliage.GetTotalInstanceCount() == 1, "The named instances should go");
        Require(foliage.FindInstance(2)->instanceIndex == 0, "The survivor should still resolve by ID");
        foliage.RestoreInstances(removed);
        Require(foliage.CaptureInstanceSnapshot() == snapshot, "Restoring should put every instance back in place");
    }

    void TestWorldEdits()
    {
        const FoliageInstance instance{.id = 1, .position = {1, 2, 3}, .rotationDegrees = {10, 40, -20}, .scale = {2, 2, 2}};
        const glm::mat4 owner = glm::translate(glm::mat4(1.0f), glm::vec3(10, 0, 0));

        FoliageInstanceWorldEdit move;
        move.pivot = {11, 2, 3};
        move.translation = {0, 5, 0};
        const auto moved = ApplyFoliageInstanceWorldEdit(owner, instance, move);
        Require(Near(moved.position, glm::vec3(1, 7, 3)), "A move should land in owner space");
        Require(moved.rotationDegrees == instance.rotationDegrees && moved.scale == instance.scale, "A move should change only position");

        FoliageInstanceWorldEdit scale;
        scale.pivot = {11, 2, 3};
        scale.scale = {3, 3, 3};
        const auto scaled = ApplyFoliageInstanceWorldEdit(owner, instance, scale);
        Require(Near(scaled.scale, glm::vec3(6)) && Near(scaled.position, instance.position), "Scaling about the pivot should keep it in place");

        // A rotation about +Y composes with what is there; check by the axes it produces, which is what draws.
        const auto axes = [](const glm::vec3 &degrees)
        {
            glm::mat4 r(1.0f);
            r = glm::rotate(r, glm::radians(degrees.y), glm::vec3(0, 1, 0));
            r = glm::rotate(r, glm::radians(degrees.x), glm::vec3(1, 0, 0));
            r = glm::rotate(r, glm::radians(degrees.z), glm::vec3(0, 0, 1));
            return glm::mat3(r);
        };
        const glm::mat3 yaw = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(30.0f), glm::vec3(0, 1, 0)));
        FoliageInstanceWorldEdit turn;
        turn.pivot = {11, 2, 3};
        turn.rotation = yaw;
        const auto turned = ApplyFoliageInstanceWorldEdit(owner, instance, turn);
        const glm::mat3 expected = yaw * axes(instance.rotationDegrees);
        const glm::mat3 actual = axes(turned.rotationDegrees);
        for (int column = 0; column < 3; ++column)
            Require(Near(actual[column], expected[column]), "Rotating should compose with the instance's rotation");

        // A group turns about its pivot.
        FoliageInstanceWorldEdit orbit;
        orbit.pivot = {10, 2, 3};
        orbit.rotation = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0)));
        const auto orbited = ApplyFoliageInstanceWorldEdit(owner, instance, orbit);
        Require(Near(orbited.position, glm::vec3(0, 2, 2)), "An off-pivot instance should orbit the pivot");

        // Field edits move a group by the same amount.
        const std::vector group{instance, FoliageInstance{.id = 2, .position = {5, 5, 5}, .rotationDegrees = {0, 0, 0}, .scale = {1, 2, 1}}};
        FoliageInstance after = instance;
        after.position += glm::vec3(1, 0, 0);
        after.rotationDegrees.y += 10.0f;
        after.scale *= 2.0f;
        const auto edited = ui::ApplyFoliagePrimaryEdit(group, instance, after);
        Require(edited[0] == after, "The primary should take exactly the edit");
        Require(Near(edited[1].position, glm::vec3(6, 5, 5)) && Near(edited[1].rotationDegrees.y, 10.0f) && Near(edited[1].scale, glm::vec3(2, 4, 2)),
                "The rest should keep their arrangement and proportions");
    }

    void TestPicking()
    {
        Fixture fixture;
        auto &foliage = *fixture.foliage;
        const glm::vec3 forward(0, 0, -1);
        Require(!foliage.PickInstance({0, 1, 10}, forward), "Nothing to hit yet");

        render::Mesh quad(QuadMesh());
        foliage.SetMesh(&quad);
        foliage.GetSelectedType()->instances = {FoliageInstance{.id = 1, .position = {0, 0, 0}},
                                                FoliageInstance{.id = 2, .position = {0, 0, -5}},
                                                FoliageInstance{.id = 3, .position = {100, 0, 0}}};
        fixture.Normalize();

        auto hit = foliage.PickInstance({0, 1, 10}, forward);
        Require(hit && hit->instanceId == 1 && Near(hit->distance, 10.0f), "The nearest surface along the ray should win");
        hit = foliage.PickInstance({0, 1, -10}, -forward);
        Require(hit && hit->instanceId == 2 && Near(hit->distance, 5.0f), "From behind, the other instance is nearest");
        hit = foliage.PickInstance({100, 1, 10}, forward);
        Require(hit && hit->instanceId == 3, "A distant instance should be pickable");
        Require(!foliage.PickInstance({50, 1, 10}, forward), "A ray between instances should miss");
        Require(!foliage.PickInstance({0, 1, 10}, -forward), "Instances behind the ray should not be hit");

        // Through a gap inside the spheres: no triangle is hit, so the one passed closest to the middle is taken.
        foliage.GetSelectedType()->instances = {FoliageInstance{.id = 4, .position = {0, 0, 0}}};
        fixture.Normalize();
        hit = foliage.PickInstance({0.8f, 1, 10}, forward);
        Require(hit && hit->instanceId == 4, "A ray through a gap in the plant should still select it");

        // The owner's transform and the instance's scale both apply, and hits stay world distances.
        fixture.owner->SetPosition({10, 0, 0});
        foliage.GetSelectedType()->instances[0].scale = {2, 2, 2};
        fixture.Normalize();
        hit = foliage.PickInstance({10, 3, 10}, forward);
        Require(hit && hit->instanceId == 4 && Near(hit->distance, 10.0f), "Picking should follow the owner and the scale");

        const auto bounds = foliage.GetInstanceBounds(hit->location);
        Require(bounds && Near(glm::vec3(bounds->model * glm::vec4(bounds->localMax, 1.0f)), glm::vec3(11, 4, 0)), "Bounds should map to the world as it draws");
        const auto pivot = foliage.GetInstancePivotTransform(hit->location);
        Require(pivot && Near(glm::vec3((*pivot)[3]), glm::vec3(10, 0, 0)) && Near(glm::length(glm::vec3((*pivot)[0])), 1.0f),
                "The gizmo's transform should be the pivot, without scale");
    }

    void TestSelection()
    {
        Fixture fixture;
        auto &foliage = *fixture.foliage;
        auto &type = foliage.AddType("Type");
        type.instances = {FoliageInstance{.id = 1}, FoliageInstance{.id = 2}, FoliageInstance{.id = 3}};
        fixture.Normalize();

        ui::FoliageInstanceSelection selection;
        Require(selection.Empty() && !selection.Primary(), "A new selection is empty");
        selection.Set(1);
        selection.Add(2);
        selection.Add(3);
        selection.Add(2);
        Require(selection.Size() == 3 && *selection.Primary() == 2, "Re-adding should make an instance primary, not duplicate it");
        selection.Toggle(3);
        Require(!selection.Contains(3) && selection.Size() == 2, "Toggling off should remove");
        selection.Toggle(3);
        Require(*selection.Primary() == 3, "Toggling on should make it primary");
        selection.Set(1);
        selection.Add(3);

        const auto locations = selection.Resolve(foliage);
        Require(locations.size() == 2 && foliage.GetInstance(locations[1])->id == 3, "Resolve should parallel the IDs");

        // Removing another instance moves the selected ones.
        const std::vector<std::uint64_t> other{2};
        foliage.RemoveInstances(other);
        const auto moved = selection.Resolve(foliage);
        Require(moved.size() == 2 && foliage.GetInstance(moved[1])->id == 3, "The selection should follow instances that moved");

        // Removing a selected one drops it.
        const std::vector<std::uint64_t> selected{1};
        const auto removed = foliage.RemoveInstances(selected);
        Require(selection.Resolve(foliage).size() == 1 && selection.Ids() == std::vector<std::uint64_t>{3}, "A removed instance should leave the selection");

        ui::FoliageEditTool tool;
        Require(tool.GetMode() == ui::FoliageToolMode::Off, "The tool starts off");
        tool.SyncTarget(5, 1, true);
        tool.SetMode(ui::FoliageToolMode::Select, 5);
        tool.selection.Set(1);
        Require(tool.IsActiveFor(5) && !tool.IsActiveFor(6), "The tool is on for its target only");
        tool.SyncTarget(5, 1, true);
        Require(tool.IsActiveFor(5) && !tool.selection.Empty(), "No change should leave it on");
        tool.SyncTarget(6, 1, true);
        Require(tool.GetMode() == ui::FoliageToolMode::Off && tool.selection.Empty(), "Another entity should turn it off");

        tool.SetMode(ui::FoliageToolMode::Paint, 6);
        Require(tool.BrushFor(false).mode == FoliageBrushMode::Add && tool.BrushFor(true).mode == FoliageBrushMode::Remove, "Shift should swap Paint to erase");
        tool.SetMode(ui::FoliageToolMode::Erase, 6);
        Require(tool.BrushFor(false).mode == FoliageBrushMode::Remove && tool.BrushFor(true).mode == FoliageBrushMode::Add, "Shift should swap Erase to paint");
        tool.SyncTarget(6, 2, true);
        Require(tool.GetMode() == ui::FoliageToolMode::Off, "A new scene should turn it off");
        tool.SetMode(ui::FoliageToolMode::Paint, 6);
        tool.SyncTarget(6, 2, false);
        Require(tool.GetMode() == ui::FoliageToolMode::Off, "Entering play should turn it off");
    }

    void TestOverrideRemap()
    {
        const std::vector<std::string> overrides{
            "Transform.Position",
            "Component:FoliageComponent:FoliageTypeCount",
            "Component:FoliageComponent:Type.0.Name",
            "Component:FoliageComponent:Type.1.Instances",
            "Component:FoliageComponent:Type.2.CellSize",
            "Component:FoliageComponent:Type.12.WindEnabled",
            "Component:FoliageComponent:Type.x.Odd",
            "Component:MeshComponent:Type.2.Name",
        };
        const auto remapped = RemapFoliageOverridesAfterTypeRemoval(overrides, 1);
        const std::vector<std::string> expected{
            "Transform.Position",
            "Component:FoliageComponent:FoliageTypeCount",
            "Component:FoliageComponent:Type.0.Name",
            "Component:FoliageComponent:Type.1.CellSize",
            "Component:FoliageComponent:Type.11.WindEnabled",
            "Component:FoliageComponent:Type.x.Odd",
            "Component:MeshComponent:Type.2.Name",
        };
        Require(remapped == expected, "Overrides should drop the removed type and follow the rest");
    }

    void TestDiagnostics()
    {
        FoliageType empty;
        auto issues = ui::DiagnoseFoliageType(empty);
        Require(issues.size() == 1 && issues[0].severity == ui::FoliageIssueSeverity::Warning, "A type without a mesh should warn");

        FoliageType missing;
        missing.asset.sourceMeshPath = "project://Gone.plutomesh";
        issues = ui::DiagnoseFoliageType(missing);
        Require(issues.size() == 1 && issues[0].severity == ui::FoliageIssueSeverity::Error &&
                    issues[0].message.find("Gone.plutomesh") != std::string::npos, "A mesh that failed to load is an error naming it");

        render::MeshConfig config{};
        render::Mesh mesh(config);
        FoliageType detailed;
        detailed.mesh = &mesh;
        Require(ui::DiagnoseFoliageType(detailed).empty(), "Nothing placed means no cost to warn about");
        detailed.instances.push_back(FoliageInstance{.id = 1});
        issues = ui::DiagnoseFoliageType(detailed);
        Require(issues.size() == 1 && issues[0].severity == ui::FoliageIssueSeverity::Info, "Placed instances without LODs should be noted");

        Require(ui::DiagnoseFoliagePainting(true, &detailed).empty(), "A terrain and a mesh are all painting needs");
        Require(ui::DiagnoseFoliagePainting(false, &detailed).size() == 1, "No terrain should be reported");
        Require(ui::DiagnoseFoliagePainting(true, &empty).size() == 1, "No mesh should be reported");
        Require(ui::DiagnoseFoliagePainting(false, &empty).size() == 2, "Both should be reported");
        Require(ui::WorstFoliageIssue({}) == ui::FoliageIssueSeverity::Info, "No issues are the least severe");
    }

    void TestBrushDoesNotDependOnSceneState()
    {
        render::MeshConfig config{};
        render::Mesh mesh(config);
        Fixture fixture;
        auto &foliage = *fixture.foliage;
        foliage.SetMesh(&mesh);
        const auto index = static_cast<std::size_t>(foliage.GetSelectedTypeIndex());
        foliage.SetTypeScaleRange(index, 2.0f, 2.0f);
        const auto flat = [](float, float) -> std::optional<FoliageSurfaceSample> { return FoliageSurfaceSample{1.0f, {0, 1, 0}}; };

        const FoliageBrush brush{.mode = FoliageBrushMode::Add, .radius = 5.0f, .density = 20};
        Require(foliage.ApplyBrush(brush, {0, 0, 0}, flat), "A brush should paint with no scene state");
        Require(foliage.GetSelectedTypeInstanceCount() == 20, "Density comes from the brush");
        for (const auto &instance : foliage.GetInstances())
        {
            Require(Near(instance.scale.x, 2.0f), "Scale comes from the type's range");
            Require(Near(instance.position.y, 1.0f), "Height comes from the surface");
        }
        const FoliageBrush erase{.mode = FoliageBrushMode::Remove, .radius = 100.0f};
        Require(foliage.ApplyBrush(erase, {0, 1, 0}, flat) && foliage.GetSelectedTypeInstanceCount() == 0, "Erase should remove within its radius");
    }
}

int main()
try
{
    TestAssetRoundTrip();
    TestAssetVersions();
    TestChangedProperties();
    TestAssetReferenceScan();
    TestOldSceneCompatibility();
    TestLinkedTypes();
    TestInstanceOperations();
    TestWorldEdits();
    TestPicking();
    TestSelection();
    TestOverrideRemap();
    TestDiagnostics();
    TestBrushDoesNotDependOnSceneState();
    std::cout << "Foliage editing tests passed.\n";
    return 0;
}
catch (const std::exception &exception)
{
    std::cerr << exception.what() << '\n';
    return 1;
}
