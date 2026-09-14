#include "PlutoGE/scene/SceneStreaming.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/SequencerComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace PlutoGE::scene;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void Await(SceneStreaming &streaming, SceneSectionID id)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (streaming.Status(id).state == SceneSectionState::Reading)
    {
        Check(std::chrono::steady_clock::now() < deadline, "Streaming timed out");
        streaming.Pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
int main()
{
    const auto path = std::filesystem::temp_directory_path() / ("PlutoGE-section-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".plutoscene");
    try
    {
        Scene section;
        auto *first = section.AddEntity(std::make_unique<Entity>(EntityConfig{.name="first"}));
        auto *second = section.AddEntity(std::make_unique<Entity>(EntityConfig{.name="second"}));
        second->CreateComponent<ColliderComponent>(ColliderComponentConfig{});
        auto *sequence = first->CreateComponent<SequencerComponent>();
        Timeline timeline;
        timeline.tracks.push_back({second->GetID(), TimelineChannel::Position, TimelineInterpolation::Linear, {{0, {1, 2, 3}, {}}}});
        Check(sequence->SetTimeline(timeline), "Timeline setup");
        Check(SceneSerializer::Save(section, path.string()), "Save section");
        Scene world;
        auto *persistent = world.AddEntity(std::make_unique<Entity>());
        const auto persistentID = persistent->GetID();
        auto &streaming = world.GetStreaming();
        const auto id = streaming.Load(path, false);
        Check(id != 0, "Request section");
        Await(streaming, id);
        Check(streaming.Status(id).state == SceneSectionState::Ready && world.GetRootEntities().size() == 1, "Deferred activation");
        Check(streaming.Activate(id), "Request activation");
        streaming.Pump();
        Check(streaming.Status(id).state == SceneSectionState::Active, "Activation failed");
        auto *loadedFirst = world.FindEntityByName("first");
        auto *loadedSecond = world.FindEntityByName("second");
        Check(loadedFirst && loadedSecond && loadedFirst->GetID() != first->GetID(), "Fresh entity IDs");
        Check(loadedFirst->GetComponent<SequencerComponent>() != nullptr, "Loaded sequencer missing");
        Check(!loadedFirst->GetComponent<SequencerComponent>()->GetTimeline().tracks.empty(), "Loaded timeline empty");
        Check(loadedFirst->GetComponent<SequencerComponent>()->GetTimeline().tracks[0].entity == loadedSecond->GetID(), "Cross-root remapping");
        Check(world.GetSectionOwner(loadedSecond->GetID()) == id, "Section ownership");
        PhysicsRaycastHit hit;
        world.SynchronizePhysicsQueries();
        Check(world.Raycast({0, 3, 0}, {0, -1, 0}, 10, hit) && hit.entityId == loadedSecond->GetID(), "Section collision registration");
        persistent->SetParent(loadedFirst);
        Check(persistent->GetParent() == nullptr, "Cross-section parenting allowed");
        auto *spawned = world.AddEntity(std::make_unique<Entity>(), loadedFirst);
        const auto spawnedID = spawned->GetID(), loadedID = loadedFirst->GetID();
        Check(world.GetSectionOwner(spawnedID) == id, "Spawn ownership inheritance");
        Check(streaming.Unload(id), "Unload");
        Check(!world.Raycast({0, 3, 0}, {0, -1, 0}, 10, hit), "Unloaded collision remained queryable");
        world.Update(0);
        Check(!world.FindEntityByID(loadedID) && !world.FindEntityByID(spawnedID) && world.FindEntityByID(persistentID), "Unload cleanup");
        Check(streaming.Forget(id), "Forget terminal request");
        const auto cancelled = streaming.Load(path);
        Check(streaming.Cancel(cancelled), "Cancellation");
        Check(streaming.Status(cancelled).state == SceneSectionState::Cancelled && !streaming.Activate(cancelled), "Cancelled activation");
        const auto failed = streaming.Load(path.string() + ".missing");
        Await(streaming, failed);
        Check(streaming.Status(failed).state == SceneSectionState::Failed && !streaming.Status(failed).error.empty(), "Failure reporting");
        streaming.Reset();
        const auto reloaded = streaming.Load(path);
        Check(reloaded > failed, "Handles reused after reset");
        Await(streaming, reloaded);
        Check(streaming.Status(reloaded).state == SceneSectionState::Active, "Reload");
        streaming.Reset();
        world.Update(0);
        Check(world.GetRootEntities().size() == 1, "Reset ownership cleanup");
        const auto generation = streaming.Generation();
        for (int request = 0; request < 4; ++request)
            Check(streaming.Load(path, false) != 0, "Pending capacity rejected too early");
        Check(streaming.Load(path, false) == 0, "Pending read capacity exceeded");
        streaming.Reset();
        Check(streaming.Generation() != generation, "Reset reused scene generation");
        { std::ofstream malformed(path, std::ios::binary | std::ios::trunc); malformed << "not a scene"; }
        const auto malformed = streaming.Load(path);
        Await(streaming, malformed);
        Check(streaming.Status(malformed).state == SceneSectionState::Failed && world.GetRootEntities().size() == 1, "Malformed section changed persistent scene");
        std::filesystem::remove(path);
        std::cout << "Scene streaming tests passed\n";
    }
    catch (const std::exception &e) { std::error_code ignored; std::filesystem::remove(path, ignored); std::cerr << e.what() << '\n'; return 1; }
}
