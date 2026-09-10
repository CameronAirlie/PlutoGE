#include "PlutoGE/ui/PlayModeChanges.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace PlutoGE::scene;
    using PlutoGE::ui::PlayModeChanges;

    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    std::string Save(const Scene &scene)
    {
        std::string text, error;
        Require(SceneSerializer::SaveToString(scene, text, &error), "Snapshot save failed");
        return text;
    }

    std::unique_ptr<Scene> Load(const std::string &text)
    {
        std::string error;
        auto scene = SceneSerializer::LoadFromString(text, &error);
        Require(scene != nullptr, "Snapshot restore failed");
        return scene;
    }

    void TestSelectiveRestore()
    {
        Scene scene;
        auto *entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Tunable light"}));
        const auto id = entity->GetID();
        auto *light = entity->CreateComponent<LightComponent>();
        light->SetIntensity(2.0f);
        light->SetColor({1, 0, 0});
        entity->SetPosition({0.12345678f, 2.0f, 3.0f});
        const auto baseline = PlayModeChanges::Capture(scene);
        const auto beforeText = Save(scene);
        light->SetIntensity(6.0f);
        light->SetColor({0, 1, 0});
        entity->SetPosition({4, 5, 6});
        auto changes = PlayModeChanges::Compare(baseline, PlayModeChanges::Capture(scene));
        Require(changes.size() == 3, "Expected position, intensity and color changes");
        Require(std::none_of(changes.begin(), changes.end(), [](const auto &c) { return c.selected; }),
                "Changes must default to unselected");
        auto cancelled = Load(beforeText);
        std::string error;
        Require(PlayModeChanges::Apply(*cancelled, changes, error), "Empty selection failed");
        Require(Save(*cancelled) == beforeText, "Empty selection changed the scene");
        for (auto &change : changes)
            change.selected = change.after.name == "Intensity" || change.after.name == "Position";
        auto restored = Load(beforeText);
        Require(PlayModeChanges::Apply(*restored, changes, error), error);
        Require(restored->FindEntityByID(id)->GetPosition() == glm::vec3(4, 5, 6), "Transform was not retained");
        const auto &retained = restored->FindEntityByID(id)->GetComponent<LightComponent>()->GetLight();
        Require(retained.intensity == 6 && retained.color == glm::vec3(1, 0, 0), "Unselected component property changed");
        // Exercise the same serialized before/after states used by editor history.
        const auto afterText = Save(*restored);
        auto undo = Load(beforeText);
        Require(undo->FindEntityByID(id)->GetComponent<LightComponent>()->GetLight().intensity == 2, "Undo snapshot failed");
        auto redo = Load(afterText);
        Require(redo->FindEntityByID(id)->GetPosition() == glm::vec3(4, 5, 6), "Redo snapshot failed");
        auto conflict = Load(beforeText);
        conflict->FindEntityByID(id)->SetPosition({100, 0, 0});
        Require(!PlayModeChanges::Apply(*conflict, changes, error), "Conflicting baseline was accepted");
        Require(conflict->FindEntityByID(id)->GetComponent<LightComponent>()->GetLight().intensity == 2,
                "Validation failure partially applied component changes");
    }

    void TestStructuralChanges()
    {
        Scene scene;
        auto *entity = scene.AddEntity(std::make_unique<Entity>());
        auto *first = entity->CreateComponent<LightComponent>();
        auto *second = entity->CreateComponent<LightComponent>();
        const auto baseline = PlayModeChanges::Capture(scene);
        second->SetIntensity(9);
        auto changes = PlayModeChanges::Compare(baseline, PlayModeChanges::Capture(scene));
        Require(changes.size() == 1 && changes.front().componentIndex == 1, "Repeated component was misidentified");
        entity->RemoveComponent(first);
        entity->CreateComponent<LightComponent>();
        Require(PlayModeChanges::Compare(baseline, PlayModeChanges::Capture(scene)).empty(),
                "Removed/replaced component layout was accepted");
        auto *spawned = scene.AddEntity(std::make_unique<Entity>());
        spawned->SetPosition({5, 0, 0});
        entity->SetParent(spawned);
        entity->SetPosition({1, 2, 3});
        changes = PlayModeChanges::Compare(baseline, PlayModeChanges::Capture(scene));
        Require(changes.size() == 1 && !changes.front().unavailableReason.empty(), "Reparented local transform was accepted");
    }

    void TestPrefabOverrides()
    {
        Scene scene;
        auto *entity = scene.AddEntity(std::make_unique<Entity>());
        entity->SetPrefabLink("project://Prefabs/Test.plutoprefab", 1, true);
        entity->CreateComponent<LightComponent>();
        const auto text = Save(scene);
        const auto baseline = PlayModeChanges::Capture(scene);
        entity->SetPosition({1, 0, 0});
        entity->GetComponent<LightComponent>()->SetIntensity(7);
        entity->GetComponent<LightComponent>()->SetEnabled(false);
        auto changes = PlayModeChanges::Compare(baseline, PlayModeChanges::Capture(scene));
        for (auto &change : changes) change.selected = true;
        auto restored = Load(text);
        std::string error;
        Require(PlayModeChanges::Apply(*restored, changes, error), error);
        const auto &overrides = restored->FindEntityByID(entity->GetID())->GetPrefabOverrides();
        for (const auto *path : {"Transform.Position", "Component:LightComponent:Intensity", "Component:LightComponent:Enabled"})
            Require(std::find(overrides.begin(), overrides.end(), path) != overrides.end(), "Missing prefab override path");
    }

    void TestInvalidReferenceAndSchema()
    {
        PlayModeChanges::Snapshot before;
        PlayModeChanges::EntityState state;
        state.name = "Script owner";
        state.components.push_back({0, 0, "ScriptComponent", true,
            {{"Source", PropertyType::String, "Test", {}}, {"Target", PropertyType::Entity, "0", {}}}});
        before.emplace(1, state);
        auto after = before;
        after.at(1).components[0].properties[1].value = "2";
        after.emplace(2, PlayModeChanges::EntityState{});
        auto changes = PlayModeChanges::Compare(before, after);
        Require(changes.size() == 1 && !changes[0].unavailableReason.empty(), "Runtime-only reference was accepted");
        after.at(1).components[0].properties[0].value = "OtherScript";
        Require(PlayModeChanges::Compare(before, after).empty(), "Script class replacement was accepted");
        after = before;
        after.at(1).components[0].properties.push_back({"Added", PropertyType::Float, "1", {}});
        after.at(1).components[0].properties[1].value = "1";
        Require(PlayModeChanges::Compare(before, after).empty(), "Changed component schema was accepted");
        after.erase(1);
        Require(PlayModeChanges::Compare(before, after).empty(), "Deleted entity generated changes");
    }
}

int main()
{
    try
    {
        TestSelectiveRestore();
        TestStructuralChanges();
        TestPrefabOverrides();
        TestInvalidReferenceAndSchema();
        std::cout << "PASS: selective play changes, snapshots, conflicts, structure and prefab overrides\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
