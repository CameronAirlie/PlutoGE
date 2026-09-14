#include "PlutoGE/scene/SceneStreaming.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/SequencerComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/NavAgentComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/platform/ContentPack.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace PlutoGE::scene;
struct SectionScript : PlutoGE::scripting::ScriptInstance
{
    inline static int created = 0, destroyed = 0;
    inline static bool validLifetime = true;
    void OnCreate() override
    {
        ++created;
        validLifetime &= GetOwner()->GetScene() && GetOwner()->GetScene()->IsRuntimeStarted();
    }
    void OnDestroy() override { ++destroyed; }
};
struct RuntimeScope
{
    PlutoGE::core::Engine &engine = PlutoGE::core::Engine::GetInstance();
    explicit RuntimeScope(Scene &scene) { engine.SetScene(&scene); engine.StartRuntime(); }
    ~RuntimeScope() { engine.StopRuntime(); engine.SetScene(nullptr); }
};
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
    const auto packSource = std::filesystem::path(path.string() + ".pack-source");
    const auto packPath = std::filesystem::path(path.string() + ".pack");
    const auto virtualRoot = std::filesystem::path(path.string() + ".virtual");
    try
    {
        Scene section;
        PlutoGE::core::Engine::GetInstance().GetScriptEngine().RegisterNativeClass(
            {.className = "SectionLifetimeTest"}, [] { return std::make_unique<SectionScript>(); });
        auto *first = section.AddEntity(std::make_unique<Entity>(EntityConfig{.name="first"}));
        auto *second = section.AddEntity(std::make_unique<Entity>(EntityConfig{.name="second"}));
        second->CreateComponent<ColliderComponent>(ColliderComponentConfig{});
        second->SetScale({10, 1, 10});
        second->CreateComponent<LightComponent>();
        second->CreateComponent<ScriptComponent>(ScriptComponentConfig{.scriptClass = "SectionLifetimeTest"});
        auto *sequence = first->CreateComponent<SequencerComponent>();
        Timeline timeline;
        timeline.tracks.push_back({second->GetID(), TimelineChannel::Position, TimelineInterpolation::Linear, {{0, {1, 2, 3}, {}}}});
        Check(sequence->SetTimeline(timeline), "Timeline setup");
        Check(SceneSerializer::Save(section, path.string()), "Save section");
        Scene world;
        auto *persistent = world.AddEntity(std::make_unique<Entity>());
        const auto persistentID = persistent->GetID();
        NavigationBakeSettings navigationSettings;
        navigationSettings.boundsMin = {-3, -2, -3};
        navigationSettings.boundsMax = {3, 3, 3};
        navigationSettings.cellSize = 1;
        auto *navigation = persistent->CreateComponent<NavigationMeshComponent>(navigationSettings);
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
        Check(world.GetLights().size() == 1, "Section light was not registered");
        Check(navigation->Bake(), "Persistent navigation did not see section floor");
        glm::vec3 projected;
        Check(navigation->GetNavigation().ProjectPoint({0, 1, 0}, projected), "Section navigation missing");
        NavAgentConfig agentConfig;
        agentConfig.navigationMeshEntityId = persistentID;
        agentConfig.agentRadius = 0;
        agentConfig.agentHeight = 0;
        auto *agent = persistent->CreateComponent<NavAgentComponent>(agentConfig);
        Check(agent->SetDestination({2, 0.5f, 2}) && agent->HasPath(), "Persistent agent path setup failed");
        PhysicsRaycastHit hit;
        world.SynchronizePhysicsQueries();
        Check(world.Raycast({0, 3, 0}, {0, -1, 0}, 10, hit) && hit.entityId == loadedSecond->GetID(), "Section collision registration");
        persistent->SetParent(loadedFirst);
        Check(persistent->GetParent() == nullptr, "Cross-section parenting allowed");
        auto *spawned = world.AddEntity(std::make_unique<Entity>(), loadedFirst);
        const auto spawnedID = spawned->GetID(), loadedID = loadedFirst->GetID();
        Check(world.GetSectionOwner(spawnedID) == id, "Spawn ownership inheritance");
        Check(streaming.Unload(id), "Unload");
        Check(world.GetLights().empty(), "Unloaded section light remained active");
        Check(!navigation->GetNavigation().ProjectPoint({0, 1, 0}, projected), "Unloaded section left stale navigation");
        Check(!agent->HasPath(), "Agent retained a path through an unloaded section");
        Check(!world.Raycast({0, 3, 0}, {0, -1, 0}, 10, hit), "Unloaded collision remained queryable");
        world.Update(0);
        Check(!world.FindEntityByID(loadedID) && !world.FindEntityByID(spawnedID) && world.FindEntityByID(persistentID), "Unload cleanup");
        Check(!navigation->GetNavigation().IsBaked(), "Empty navigation was reported as baked");
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
        world.Update(0);
        Check(navigation->GetNavigation().ProjectPoint({0, 1, 0}, projected), "Reload did not restore persistent navigation");
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
        Check(SceneSerializer::Save(section, path.string()), "Restore section for runtime checks");
        {
            persistent->CreateComponent<ScriptComponent>(ScriptComponentConfig{.scriptClass = "SectionLifetimeTest"});
            RuntimeScope runtime(world);
            Check(SectionScript::created == 1 && SectionScript::validLifetime, "OnCreate saw an inactive runtime lifetime");
            const auto runtimeSection = streaming.Load(path);
            Await(streaming, runtimeSection);
            world.Update(0.01f);
            Check(SectionScript::created == 2 && SectionScript::validLifetime, "Streamed script startup failed");
            Check(streaming.Unload(runtimeSection), "Runtime section unload failed");
            world.Update(0.01f);
            Check(SectionScript::destroyed == 1, "Streamed script OnDestroy was not called exactly once");
        }
        Check(SectionScript::destroyed == 2 && world.GetRootEntities().size() == 1, "Runtime stop script/section cleanup failed");
        std::filesystem::create_directory(packSource);
        std::filesystem::copy_file(path, packSource / "Section.plutoscene");
        std::string packError;
        Check(PlutoGE::content::WritePack(packSource, packPath, {}, &packError), packError.c_str());
        Check(PlutoGE::content::Mount(packPath, virtualRoot, &packError), packError.c_str());
        const auto packedSection = streaming.Load(virtualRoot / "Section.plutoscene");
        Await(streaming, packedSection);
        Check(streaming.Status(packedSection).state == SceneSectionState::Active, "Packed section streaming failed");
        Check(!std::filesystem::exists(virtualRoot / "Section.plutoscene"), "Packed section was unnecessarily materialized");
        streaming.Reset(); world.Update(0);
        PlutoGE::content::UnmountAll();
        std::filesystem::remove(packSource / "Section.plutoscene");
        std::filesystem::remove(packSource); std::filesystem::remove(packPath);
        std::filesystem::remove(path);
        std::cout << "Scene streaming tests passed\n";
    }
    catch (const std::exception &e)
    {
        PlutoGE::content::UnmountAll();
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        std::filesystem::remove(packSource / "Section.plutoscene", ignored);
        std::filesystem::remove(packSource, ignored); std::filesystem::remove(packPath, ignored);
        std::cerr << e.what() << '\n'; return 1;
    }
}
