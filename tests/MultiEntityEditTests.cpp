#include "PlutoGE/ui/EntitySelection.h"
#include "PlutoGE/ui/MultiEntityEdit.h"
#include "PlutoGE/ui/SceneSnapshots.h"
#include "PlutoGE/ui/SceneHistory.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace PlutoGE::ui;
    using namespace PlutoGE::scene;
    void Require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
    void Near(glm::vec3 a, glm::vec3 b) { Require(glm::length(a - b) < 0.001f, "Unexpected transform"); }
    void Selection()
    {
        EntitySelection s;
        const std::vector<std::uint32_t> visible{1, 2, 3, 4, 5};
        s.Click(2, false, false);
        s.Click(4, false, true, visible);
        Require(s.Ids() == std::vector<std::uint32_t>({2, 3, 4}), "Forward range");
        s.Click(1, false, true, visible);
        Require(s.Ids() == std::vector<std::uint32_t>({2, 1}), "Reverse range retains anchor");
        s.Click(5, true, false);
        s.Click(1, true, false);
        Require(s.Contains(2) && s.Contains(5) && !s.Contains(1), "Control toggle");
        s.Click(3, true, true, visible);
        Require(s.Contains(1) && s.Contains(2) && s.Contains(3) && s.Contains(5), "Additive range");
        s.Prune([](auto id) { return id == 2; });
        Require(s.Ids() == std::vector<std::uint32_t>{2}, "Deleted IDs pruned");
        s.Click(4, false, true, {3, 4});
        Require(s.Contains(4), "Missing visible anchor fallback");
        s.Click(0, false, false);
        Require(s.Ids().empty(), "Background clear");
    }
    void Transforms()
    {
        Scene scene;
        auto *parent = scene.AddEntity(std::make_unique<Entity>());
        auto *child = scene.AddEntity(std::make_unique<Entity>());
        auto *other = scene.AddEntity(std::make_unique<Entity>());
        child->SetParent(parent);
        child->SetPosition({2, 0, 0});
        parent->SetScale({-2, 3, 2});
        parent->SetRotation({0, 0, 35});
        const auto oldChild = glm::vec3(child->GetWorldTransform()[3]);
        std::string error;
        Require(SelectionRoots({child, parent, other, parent}).size() == 2, "Root filtering");
        Require(TransformSelection({child, parent, other}, glm::translate(glm::mat4(1), {1, 2, 3}), error), error.c_str());
        Near(glm::vec3(child->GetWorldTransform()[3]), oldChild + glm::vec3(1, 2, 3));
        Near(child->GetPosition(), {2, 0, 0});
        Near(parent->GetScale(), {-2, 3, 2});
        Require(TransformSelection({child, other}, glm::translate(glm::mat4(1), {0, 4, 0}), error), error.c_str());
        Near(glm::vec3(child->GetWorldTransform()[3]), oldChild + glm::vec3(1, 6, 3));
        const auto before = other->GetPosition();
        parent->SetScale({0, 1, 1});
        Require(!TransformSelection({other, child}, glm::translate(glm::mat4(1), {1, 0, 0}), error), "Singular parent accepted");
        Near(other->GetPosition(), before);
        parent->SetScale({2, 1, 1});
        Require(!TransformSelection({other, child}, glm::rotate(glm::mat4(1), 0.7f, glm::vec3(0, 0, 1)), error), "Shear accepted");
        Near(other->GetPosition(), before);
        parent->SetScale({1, 1, 1});
        parent->SetRotation({0, 0, 0});
        Require(TransformSelection({parent, other}, glm::rotate(glm::mat4(1), 0.7f, glm::vec3(0, 0, 1)), error), error.c_str());
        Near(parent->GetRotation(), {0, 0, glm::degrees(0.7f)});
        Require(TransformSelection({parent, other}, glm::scale(glm::mat4(1), glm::vec3(2)), error), error.c_str());
        Near(parent->GetScale(), {2, 2, 2});
        const auto unchanged = parent->GetLocalTransform();
        Require(TransformSelection({parent}, glm::mat4(1), error), error.c_str());
        Require(parent->GetLocalTransform() == unchanged, "Identity gizmo generated an edit");
    }
    void PropertiesAndHistory()
    {
        auto scene = std::make_unique<Scene>();
        auto *a = scene->AddEntity(std::make_unique<Entity>());
        auto *b = scene->AddEntity(std::make_unique<Entity>());
        auto *lightA = a->CreateComponent<LightComponent>();
        auto *lightB = b->CreateComponent<LightComponent>();
        lightB->GetLight().color = {0.2f, 0.3f, 0.4f};
        auto groups = FindCommonComponents({a, b});
        Require(groups.size() == 1, "Common component missing");
        std::string before, after, error;
        Require(SceneSerializer::SaveToString(*scene, before, &error), error.c_str());
        bool edited = false;
        for (auto p : groups.front().properties)
            if (p.name == "Intensity") { p.value = "9"; SetCommonProperty(groups.front(), p); edited = true; }
        Require(edited && lightA->GetLight().intensity == 9 && lightB->GetLight().intensity == 9, "Common property not applied");
        Near(lightB->GetLight().color, {0.2f, 0.3f, 0.4f});
        for (auto p : groups.front().properties)
            if (p.name == "Color") { p.value = "0.8"; SetCommonProperty(groups.front(), p, 0); }
        Near(lightB->GetLight().color, {0.8f, 0.3f, 0.4f});
        auto *empty = scene->AddEntity(std::make_unique<Entity>());
        Require(FindCommonComponents({a, empty}).empty(), "Non-common component included");
        scene->RemoveEntity(empty);
        const auto idA = a->GetID(), idB = b->GetID();
        EntitySelection selection;
        selection.Set({idA, idB});
        Require(SceneSerializer::SaveToString(*scene, after, &error), error.c_str());
        std::vector<SceneHistoryEntry> undo{{.label = "Edit selection", .beforeState = before, .afterState = after}}, redo;
        const auto restore = [&](const std::string &state)
        {
            auto restored = LoadSceneSnapshot(state, error);
            if (!restored) return false;
            scene = std::move(restored);
            selection.Prune([&](auto id) { return scene->FindEntityByID(id) != nullptr; });
            return true;
        };
        Require(TransferSceneHistory(undo, redo, [&](const auto &entry) { return restore(entry.beforeState); }), "Undo failed");
        Require(scene->FindEntityByID(idA)->GetComponent<LightComponent>()->GetLight().intensity != 9, "Undo missed first entity");
        Require(scene->FindEntityByID(idB)->GetComponent<LightComponent>()->GetLight().intensity != 9, "Undo missed second entity");
        Require(TransferSceneHistory(redo, undo, [&](const auto &entry) { return restore(entry.afterState); }), "Redo failed");
        Require(selection.Ids() == std::vector<std::uint32_t>({idA, idB}), "Selection lost across restore");
        Require(scene->FindEntityByID(idB)->GetComponent<LightComponent>()->GetLight().intensity == 9, "Redo missed common edit");
    }
}
int main()
{
    try { Selection(); Transforms(); PropertiesAndHistory(); std::cout << "PASS: multi-selection, group transforms, common properties and history\n"; }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

