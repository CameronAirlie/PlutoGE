#include "PlutoGE/ui/EntitySelection.h"
#include "PlutoGE/ui/MultiEntityEdit.h"
#include "PlutoGE/ui/HierarchyTransforms.h"
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
    void MatrixNear(const glm::mat4 &a, const glm::mat4 &b)
    {
        for (int c = 0; c < 4; ++c)
            Require(glm::length(a[c] - b[c]) < 0.002f, "World matrix changed");
    }
    void HierarchyPivots()
    {
        Scene scene;
        auto *parent = scene.AddEntity(std::make_unique<Entity>());
        parent->SetRotation({23, -41, 17});
        parent->SetScale({-2, 3, 0.7f});
        auto *group = scene.AddEntity(std::make_unique<Entity>(), parent);
        group->SetWorldPosition({7, 3, -2});
        auto *a = scene.AddEntity(std::make_unique<Entity>(), parent);
        auto *b = scene.AddEntity(std::make_unique<Entity>(), parent);
        a->SetPosition({3, 2, -1});
        a->SetRotation({34, 58, -19});
        a->SetScale({2, -0.5f, 4});
        b->SetRotation({-70, 25, 89});
        b->SetScale({0, 2, 1});
        const auto aWorld = a->GetWorldTransform(), bWorld = b->GetWorldTransform();
        const auto rotation = a->GetRotation(), scale = a->GetScale();
        ReparentIntoTranslationGroup(*a, *group);
        ReparentIntoTranslationGroup(*b, *group);
        MatrixNear(aWorld, a->GetWorldTransform());
        MatrixNear(bWorld, b->GetWorldTransform());
        Require(a->GetRotation() == rotation && a->GetScale() == scale, "Grouping altered local rotation/scale");
        Require(MoveEntityPivot(*group, {-4, 6, 8}), "Group pivot rejected");
        MatrixNear(aWorld, a->GetWorldTransform());
        MatrixNear(bWorld, b->GetWorldTransform());
        auto *mesh = a->CreateComponent<MeshComponent>(MeshComponentConfig{});
        mesh->SetMeshAssetReference("shared-mesh");
        mesh->SetMeshPositionOffset({1, -3, 2});
        mesh->SetMeshRotationOffset({12, 26, -31});
        auto *child = scene.AddEntity(std::make_unique<Entity>(), a);
        child->SetPosition({2, 1, 4});
        auto *childMesh = child->CreateComponent<MeshComponent>(MeshComponentConfig{});
        childMesh->SetMeshAssetReference("shared-mesh");
        childMesh->SetSubmeshIndex(0);
        const auto geometry = a->GetWorldTransform() * mesh->GetMeshOffsetTransform();
        const auto childGeometry = child->GetWorldTransform() * childMesh->GetMeshOffsetTransform();
        Require(MoveEntityPivot(*a, {2, 8, -3}), "Mesh pivot rejected");
        Near(a->GetWorldPosition(), {2, 8, -3});
        MatrixNear(geometry, a->GetWorldTransform() * mesh->GetMeshOffsetTransform());
        MatrixNear(childGeometry, child->GetWorldTransform() * childMesh->GetMeshOffsetTransform());
        Require(MoveEntityPivot(*child, {5, -1, 7}), "Submesh pivot rejected");
        MatrixNear(geometry, a->GetWorldTransform() * mesh->GetMeshOffsetTransform());
        MatrixNear(childGeometry, child->GetWorldTransform() * childMesh->GetMeshOffsetTransform());
        Require(MoveEntityPivot(*a, {-2, 3, 4}), "Repeated pivot rejected");
        MatrixNear(geometry, a->GetWorldTransform() * mesh->GetMeshOffsetTransform());
        MatrixNear(childGeometry, child->GetWorldTransform() * childMesh->GetMeshOffsetTransform());
        MeshComponent restored({});
        mesh->SetMeshAssetReference("");
        restored.Deserialize(mesh->Serialize());
        Near(restored.GetPivotOffset(), mesh->GetPivotOffset());
        MatrixNear(restored.GetMeshOffsetTransform(), mesh->GetMeshOffsetTransform());
        childMesh->SetMeshAssetReference("");
        const auto savedChildGeometry = child->GetWorldTransform() * childMesh->GetMeshOffsetTransform();
        std::string snapshot, error;
        Require(SceneSerializer::SaveToString(scene, snapshot, &error), error.c_str());
        auto loaded = LoadSceneSnapshot(snapshot, error);
        Require(loaded != nullptr, error.c_str());
        auto *loadedA = loaded->FindEntityByID(a->GetID());
        auto *loadedChild = loaded->FindEntityByID(child->GetID());
        Near(loadedA->GetWorldPosition(), a->GetWorldPosition());
        MatrixNear(geometry, loadedA->GetWorldTransform() * loadedA->GetComponent<MeshComponent>()->GetMeshOffsetTransform());
        MatrixNear(savedChildGeometry, loadedChild->GetWorldTransform() * loadedChild->GetComponent<MeshComponent>()->GetMeshOffsetTransform());
        const auto before = b->GetPosition();
        Require(!MoveEntityPivot(*b, {9, 8, 7}), "Singular pivot accepted");
        Near(before, b->GetPosition());
    }
    void SelectionPivots()
    {
        PlutoGE::render::MeshConfig config;
        config.data.vertices = {
            {.position = {0, 0, 0}}, {.position = {2, 0, 0}}, {.position = {0, 4, 0}}};
        config.data.indices = {0, 1, 2};
        PlutoGE::render::Mesh mesh(config);
        Scene scene;
        auto *a = scene.AddEntity(std::make_unique<Entity>());
        auto *b = scene.AddEntity(std::make_unique<Entity>());
        a->SetPosition({-10, 0, 0});
        b->SetPosition({10, 0, 0});
        b->SetScale({3, 1, 1});
        auto *ma = a->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh = &mesh});
        auto *mb = b->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh = &mesh});
        const auto worldA = a->GetWorldTransform() * ma->GetMeshOffsetTransform();
        const auto worldB = b->GetWorldTransform() * mb->GetMeshOffsetTransform();
        const auto unchanged = [&]()
        {
            MatrixNear(worldA, a->GetWorldTransform() * ma->GetMeshOffsetTransform());
            MatrixNear(worldB, b->GetWorldTransform() * mb->GetMeshOffsetTransform());
        };
        std::string error;
        Require(SetSelectionPivotsToMeshBounds({a, b}, false, false, error), error.c_str());
        Near(a->GetWorldPosition(), {-9, 2, 0});
        Near(b->GetWorldPosition(), {13, 2, 0});
        unchanged();
        Require(SetSelectionPivotsToMeshBounds({a, b}, true, false, error), error.c_str());
        Near(a->GetWorldPosition(), {3, 2, 0});
        Near(b->GetWorldPosition(), {3, 2, 0});
        unchanged();
        Require(SetSelectionPivotsToMeshBounds({b, a}, true, true, error), error.c_str());
        Near(a->GetWorldPosition(), {3, 0, 0});
        Near(b->GetWorldPosition(), {3, 0, 0});
        unchanged();
        Require(SetSelectionPivotsToMeshBounds({a, b}, false, true, error), error.c_str());
        Near(a->GetWorldPosition(), {-9, 0, 0});
        Near(b->GetWorldPosition(), {13, 0, 0});
        unchanged();
        auto *empty = scene.AddEntity(std::make_unique<Entity>());
        const auto before = a->GetWorldPosition();
        Require(!SetSelectionPivotsToMeshBounds({a, empty}, false, false, error), "Empty bounds accepted");
        Near(a->GetWorldPosition(), before);
        b->SetScale({0, 1, 1});
        Require(!SetSelectionPivotsToMeshBounds({a, b}, true, false, error), "Singular selection accepted");
        Near(a->GetWorldPosition(), before);
        b->SetScale({3, 1, 1});
        // Selecting both a group and its child must retain each requested pivot,
        // regardless of the selection order.
        auto *group = scene.AddEntity(std::make_unique<Entity>());
        a->SetParent(group);
        b->SetParent(group);
        for (bool childFirst : {false, true})
        {
            const std::vector<Entity *> selected = childFirst ? std::vector<Entity *>{a, group} : std::vector<Entity *>{group, a};
            Require(SetSelectionPivotsToMeshBounds(selected, false, false, error), error.c_str());
            Near(group->GetWorldPosition(), {3, 2, 0});
            Near(a->GetWorldPosition(), {-9, 2, 0});
            unchanged();
            Require(SetSelectionPivotsToMeshBounds(selected, true, true, error), error.c_str());
            Near(group->GetWorldPosition(), {3, 0, 0});
            Near(a->GetWorldPosition(), {3, 0, 0});
            unchanged();
        }
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
    try { Selection(); Transforms(); HierarchyPivots(); SelectionPivots(); PropertiesAndHistory(); std::cout << "PASS: multi-selection, group transforms, common properties and history\n"; }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

