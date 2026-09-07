#include "PlutoGE/scene/components/CameraRigComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/ui/SceneSnapshots.h"
#include "PlutoGE/ui/SceneHistory.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace PlutoGE::scene;
    void Require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
    void Near(glm::vec3 actual, glm::vec3 expected, float tolerance = 0.002f)
    { Require(glm::length(actual - expected) < tolerance, "Unexpected camera pose"); }
    struct Fixture
    {
        Scene scene;
        Entity *target = scene.AddEntity(std::make_unique<Entity>());
        Entity *camera = scene.AddEntity(std::make_unique<Entity>());
        CameraRigComponent *rig;
        Fixture()
        {
            camera->CreateComponent<CameraComponent>(nullptr, false);
            rig = camera->CreateComponent<CameraRigComponent>();
            CameraRigSettings settings;
            settings.target = target->GetID(); settings.focusOffset = glm::vec3(0);
            settings.offset = {0, 0, 6}; settings.collisionRadius = 0;
            Require(rig->SetSettings(settings), "Settings failed");
        }
    };
    void MotionAndLifetime()
    {
        Fixture f;
        f.rig->UpdateRig(0);
        Near(f.camera->GetWorldPosition(), {0, 0, 0});
        f.rig->UpdateRig(0.1f);
        Near(f.camera->GetWorldPosition(), {0, 0, 6});
        Near(-glm::normalize(glm::vec3(f.camera->GetWorldTransform()[2])), {0, 0, -1});
        auto settings = f.rig->GetSettings();
        settings.smoothing = 0; settings.mode = CameraRigMode::Orbit;
        settings.yaw = 90; settings.pitch = 0; settings.distance = 4;
        Require(f.rig->SetSettings(settings), "Orbit settings failed");
        f.rig->UpdateRig(0.1f);
        Near(f.camera->GetWorldPosition(), {4, 0, 0});
        Near(-glm::normalize(glm::vec3(f.camera->GetWorldTransform()[2])), {-1, 0, 0});
        auto *parent = f.scene.AddEntity(std::make_unique<Entity>());
        parent->SetPosition({8, 2, 3}); parent->SetRotation({0, 45, 0});
        f.camera->SetParent(parent);
        f.rig->UpdateRig(0.1f);
        Near(f.camera->GetWorldPosition(), {4, 0, 0});
        Near(-glm::normalize(glm::vec3(f.camera->GetWorldTransform()[2])), {-1, 0, 0});
        auto unchanged = f.camera->GetWorldPosition();
        f.rig->SetEnabled(false); f.target->SetPosition({20, 0, 0});
        f.rig->UpdateRig(1); Near(f.camera->GetWorldPosition(), unchanged);
        f.rig->SetEnabled(true); f.rig->UpdateRig(0.1f);
        Near(f.camera->GetWorldPosition(), {24, 0, 0});
        f.scene.RemoveEntity(f.target);
        f.rig->UpdateRig(1); Near(f.camera->GetWorldPosition(), {24, 0, 0});
        settings.yaw = std::numeric_limits<float>::quiet_NaN();
        Require(!f.rig->SetSettings(settings), "Nonfinite settings accepted");
        Require(!f.rig->BlendTo(999999, 1), "Missing blend target accepted");
        Require(!f.rig->Shake(-1, 1), "Negative shake accepted");
        f.rig->Deserialize({{"Distance", PropertyType::Float, "nan"}});
        Require(std::isfinite(f.rig->GetSettings().distance), "Malformed data poisoned the rig");
    }
    glm::vec3 Smoothed(int steps)
    {
        Fixture f;
        f.rig->UpdateRig(0.01f);
        f.target->SetPosition({10, 0, 0});
        for (int i = 0; i < steps; ++i) f.rig->UpdateRig(1.0f / steps);
        return f.camera->GetWorldPosition();
    }
    void BlendsAndShake()
    {
        Near(Smoothed(30), Smoothed(120));
        Fixture f;
        f.rig->UpdateRig(0.01f);
        auto *other = f.scene.AddEntity(std::make_unique<Entity>());
        other->SetPosition({10, 0, 0});
        Require(f.rig->BlendTo(other->GetID(), 1), "Blend rejected");
        f.rig->UpdateRig(0.5f); Near(f.camera->GetWorldPosition(), {5, 0, 6});
        f.rig->UpdateRig(0); Near(f.camera->GetWorldPosition(), {5, 0, 6});
        f.camera->SetWorldPosition({1000, 0, 0}); f.camera->SetScale(glm::vec3(0));
        Require(!f.rig->BlendTo(f.target->GetID(), 1), "Singular camera accepted a blend");
        f.camera->SetScale(glm::vec3(1)); f.camera->SetWorldPosition({5, 0, 6});
        f.rig->UpdateRig(0.25f); Near(f.camera->GetWorldPosition(), {8.4375f, 0, 6});
        f.rig->UpdateRig(0.25f); Near(f.camera->GetWorldPosition(), {10, 0, 6});
        Require(f.rig->Shake(0.5f, 1, 3), "Shake rejected");
        f.rig->UpdateRig(0.1f);
        const auto shaken = f.camera->GetWorldPosition();
        Require(glm::length(shaken - glm::vec3(10, 0, 6)) > 0.01f, "Shake did not move camera");
        f.rig->UpdateRig(0); Near(f.camera->GetWorldPosition(), shaken);
        f.rig->UpdateRig(1); Near(f.camera->GetWorldPosition(), {10, 0, 6});
        f.rig->Shake(1, 10); f.rig->ResetRuntime();
        f.rig->UpdateRig(0.1f); Near(f.camera->GetWorldPosition(), {10, 0, 6});
    }
    void RuntimePhase()
    {
        Fixture f;
        f.scene.Update(0.01f);
        Near(f.camera->GetWorldPosition(), {0, 0, 0}); // Authoring never drives the rig.
        f.scene.StartRuntime();
        f.target->SetPosition({2, 0, 0});
        f.scene.Update(0.01f);
        Near(f.camera->GetWorldPosition(), {2, 0, 6});
        f.scene.SetTimeScale(0);
        f.target->SetPosition({8, 0, 0});
        f.scene.Update(1);
        Near(f.camera->GetWorldPosition(), {2, 0, 6});
        f.scene.StopRuntime();
        f.scene.SetTimeScale(1);
        f.scene.StartRuntime();
        f.scene.Update(0.01f);
        Near(f.camera->GetWorldPosition(), {8, 0, 6});
        f.scene.StopRuntime();
    }
    void Collision()
    {
        Fixture f;
        auto settings = f.rig->GetSettings(); settings.collisionRadius = 0.25f;
        Require(f.rig->SetSettings(settings), "Collision settings rejected");
        auto *wall = f.scene.AddEntity(std::make_unique<Entity>());
        wall->SetPosition({0, 0, 3});
        wall->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {8, 8, 0.5f}});
        f.target->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {1, 1, 1}});
        auto *child = f.scene.AddEntity(std::make_unique<Entity>());
        child->SetParent(f.target); child->SetPosition({0, 0, 1});
        child->CreateComponent<ColliderComponent>();
        f.rig->UpdateRig(0.1f);
        Require(f.camera->GetWorldPosition().z > 2.3f && f.camera->GetWorldPosition().z < 2.55f, "Sphere sweep failed or hit target hierarchy");
        f.rig->Shake(0.2f, 1); f.rig->UpdateRig(0.1f);
        Require(f.camera->GetWorldPosition().z < 2.55f, "Shake escaped collision constraints");
        wall->GetComponent<ColliderComponent>()->SetTrigger(true);
        f.rig->ResetRuntime(); f.rig->UpdateRig(0.1f);
        Near(f.camera->GetWorldPosition(), {0, 0, 6});
    }
    void Persistence()
    {
        using namespace PlutoGE::ui;
        Fixture f;
        std::string before, after, error;
        const auto id = f.camera->GetID();
        Require(SceneSerializer::SaveToString(f.scene, before, &error), "Save failed");
        f.rig->Deserialize({{"Yaw", PropertyType::Float, "42"}});
        Require(SceneSerializer::SaveToString(f.scene, after, &error), "Save after edit failed");
        std::vector<SceneHistoryEntry> undo{{.beforeState = before, .afterState = after}}, redo;
        std::unique_ptr<Scene> restored;
        const auto restore = [&](const std::string &state) { restored = LoadSceneSnapshot(state, error); return restored != nullptr; };
        Require(TransferSceneHistory(undo, redo, [&](const auto &e) { return restore(e.beforeState); }), "Undo failed");
        Require(restored->FindEntityByID(id)->GetComponent<CameraRigComponent>()->GetSettings().yaw == 0, "Undo lost settings");
        Require(TransferSceneHistory(redo, undo, [&](const auto &e) { return restore(e.afterState); }), "Redo failed");
        auto *rig = restored->FindEntityByID(id)->GetComponent<CameraRigComponent>();
        Require(rig && rig->GetSettings().yaw == 42 && rig->GetSettings().target == f.target->GetID(), "Rig serialization failed");
        rig->UpdateRig(0.1f);
        Near(restored->FindEntityByID(id)->GetWorldPosition(), {0, 0, 6});
        // Prefab cloning must map a rig's target to the cloned hierarchy.
        f.camera->SetParent(f.target);
        auto *copy = Prefab::DuplicateEntity(f.scene, *f.target, nullptr, true);
        Require(copy && !copy->GetChildren().empty(), "Hierarchy duplication failed");
        auto *copyRig = copy->GetChildren().front()->GetComponent<CameraRigComponent>();
        Require(copyRig && copyRig->GetSettings().target == copy->GetID(), "Cloned camera still follows original target");
    }
}
int main()
{
    try { MotionAndLifetime(); BlendsAndShake(); RuntimePhase(); Collision(); Persistence(); std::cout << "PASS: camera rigs, collision, lifecycle, serialization and history\n"; }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
