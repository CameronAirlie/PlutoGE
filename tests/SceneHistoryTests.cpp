#include "PlutoGE/ui/SceneHistory.h"
#include "PlutoGE/ui/SceneSnapshots.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
    std::string Snapshot(const PlutoGE::scene::Scene &scene)
    {
        std::string state, error;
        Require(PlutoGE::scene::SceneSerializer::SaveToString(scene, state, &error), "Snapshot failed");
        return state;
    }
}
int main()
{
    try
    {
        using namespace PlutoGE::ui;
        using namespace PlutoGE::scene;
        auto scene = std::make_unique<Scene>();
        auto *entity = scene->AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Before"}));
        const auto id = entity->GetID();
        entity->CreateComponent<LightComponent>()->SetIntensity(2);
        const auto before = Snapshot(*scene);
        entity->SetName("After");
        entity->SetPosition({3, 4, 5});
        entity->GetComponent<LightComponent>()->SetIntensity(8);
        const auto after = Snapshot(*scene);
        std::vector<SceneHistoryEntry> undo{{.label = "Inspector", .beforeState = before, .afterState = after}}, redo;
        const auto restore = [&](const std::string &state) {
            std::string error;
            auto staged = LoadSceneSnapshot(state, error);
            if (!staged) return false;
            scene = std::move(staged);
            return true;
        };
        Require(!TransferSceneHistory(undo, redo, [](const auto &) { return false; }), "Failed application accepted");
        Require(undo.size() == 1 && redo.empty() && Snapshot(*scene) == after, "Failure lost history or changed scene");
        Require(TransferSceneHistory(undo, redo, [&](const auto &entry) { return restore(entry.beforeState); }), "Undo failed");
        Require(undo.empty() && redo.size() == 1 && Snapshot(*scene) == before, "Undo did not restore saved state");
        Require(scene->FindEntityByID(id)->GetName() == "Before", "Owner ID/name did not survive restore");
        Require(TransferSceneHistory(redo, undo, [&](const auto &entry) { return restore(entry.afterState); }), "Redo failed");
        Require(Snapshot(*scene) == after && scene->FindEntityByID(id)->GetComponent<LightComponent>()->GetLight().intensity == 8, "Component edit did not roundtrip");
        undo.push_back({.label = "Bad snapshot", .beforeState = "broken"});
        Require(!TransferSceneHistory(undo, redo, [&](const auto &entry) { return restore(entry.beforeState); }) && undo.size() == 2, "Malformed restore lost entry");
        Require(Snapshot(*scene) == after, "Malformed restore replaced scene");
        undo.pop_back();
        int value = 2;
        undo.push_back({.label = "Command", .undo = [&] { value = 1; return true; }, .redo = [&] { value = 2; return true; }});
        Require(TransferSceneHistory(undo, redo, [](const auto &entry) { return entry.undo(); }) && value == 1, "Command undo failed");
        Require(TransferSceneHistory(redo, undo, [](const auto &entry) { return entry.redo(); }) && value == 2, "Command redo failed");
        std::cout << "PASS: history transfer, failed restores, serialized inspector edits and command history\n";
        return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
