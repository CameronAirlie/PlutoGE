#include "PlutoGE/ui/TimelinePreview.h"
#include "PlutoGE/scene/components/SequencerComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/Prefab.h"
#include <iostream>
#include <stdexcept>

using namespace PlutoGE::scene;
struct EventScript : PlutoGE::scripting::ScriptInstance
{
    inline static std::vector<std::string> received;
    inline static float parameter = 0;
    void OnAnimationEvent(std::string_view name, std::string_view, float value, int) override
    {
        received.emplace_back(name); parameter = value;
        if (name == "stop") PlutoGE::core::Engine::GetInstance().StopRuntime();
    }
};
struct EventRuntimeScope
{
    PlutoGE::core::Engine &engine = PlutoGE::core::Engine::GetInstance();
    explicit EventRuntimeScope(Scene &scene) { engine.SetScene(&scene); engine.StartRuntime(); }
    ~EventRuntimeScope() { engine.StopRuntime(); engine.SetScene(nullptr); }
};
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main()
{
    try
    {
        Scene scene;
        auto *root = scene.AddEntity(std::make_unique<Entity>());
        auto *target = scene.AddEntity(std::make_unique<Entity>(), root);
        auto *sequence = root->CreateComponent<SequencerComponent>();
        auto *light = target->CreateComponent<LightComponent>();
        Timeline timeline;
        timeline.tracks = {{target->GetID(), TimelineChannel::Position, TimelineInterpolation::Linear,
                            {{0, {0, 0, 0}, {}}, {1, {10, 0, 0}, {}}}},
                           {target->GetID(), TimelineChannel::LightIntensity, TimelineInterpolation::Linear,
                            {{0, {2, 0, 0}, {}}, {1, {4, 0, 0}, {}}}}};
        Check(sequence->SetTimeline(timeline), "Set timeline");
        Check(sequence->MissingBindings().empty(), "Valid bindings");
        Check(sequence->Scrub(0.5), "Scrub");
        Check(target->GetPosition().x == 5 && light->GetLight().intensity == 3, "Applied values");
        Check(!sequence->IsPlaying(), "Scrub is silent/stopped");
        // Render-only preview must leave byte-identical authoring snapshots.
        const auto beforeLightPosition = light->GetLight().position;
        std::string beforePreview, afterPreview;
        SceneSerializer::SaveToString(scene, beforePreview);
        PlutoGE::ui::TimelinePreview preview;
        Check(preview.Begin(scene, root->GetID(), false) && preview.Seek(0.75), "Begin preview");
        {
            auto pose = preview.RenderPose(scene, 0);
            Check(pose && target->GetPosition().x == 7.5f, "Preview pose");
            Check(light->GetLight().position.x == 7.5f, "Preview light transform");
        }
        SceneSerializer::SaveToString(scene, afterPreview);
        Check(beforePreview == afterPreview, "Preview polluted authoring state");
        Check(target->GetPosition().x == 5 && light->GetLight().position == beforeLightPosition, "Preview restoration");
        Check(sequence->SetTimeline(timeline), "Edit timeline during preview");
        Check(!preview.RenderPose(scene, 0) && !preview.Owner(), "Stale preview invalidation");
        Check(preview.Begin(scene, root->GetID(), true), "Preview playback");
        { auto pose = preview.RenderPose(scene, 0.25); Check(pose && target->GetPosition().x == 2.5f, "Preview advances"); }
        preview.Pause();
        Check(!preview.IsPlaying() && preview.Time() == 0.25, "Pause preserves playhead");
        { auto pose = preview.RenderPose(scene, 0.25); Check(pose && preview.Time() == 0.25, "Paused preview advanced"); }
        Check(preview.Resume() && preview.Time() == 0.25, "Resume reset playhead");
        { auto pose = preview.RenderPose(scene, 0.25); Check(pose && preview.Time() == 0.5, "Resume did not advance from pause"); }
        preview.Stop();
        Check(!preview.Resume(), "Stopped preview resumed without initialization");
        Check(target->GetPosition().x == 5, "Preview stop preserves authoring");
        // All continuous channels restore their authoring values. Preview events
        // must remain silent, even when their keys fall inside the rendered interval.
        auto *camera = target->CreateComponent<CameraComponent>(new PlutoGE::render::Camera({}), false);
        auto *audio = target->CreateComponent<SoundEmitterComponent>();
        auto previewTimeline = timeline;
        previewTimeline.tracks.push_back({target->GetID(), TimelineChannel::CameraFov, TimelineInterpolation::Step, {{0, {80, 0, 0}, {}}}});
        previewTimeline.tracks.push_back({target->GetID(), TimelineChannel::AudioVolume, TimelineInterpolation::Step, {{0, {0.25f, 0, 0}, {}}}});
        previewTimeline.tracks.push_back({target->GetID(), TimelineChannel::AudioPlay, TimelineInterpolation::Step, {{0, {}, {}}}});
        previewTimeline.tracks.push_back({target->GetID(), TimelineChannel::Rotation, TimelineInterpolation::Step, {{0, {10, 20, 30}, {}}}});
        previewTimeline.tracks.push_back({target->GetID(), TimelineChannel::Scale, TimelineInterpolation::Step, {{0, {2, 3, 4}, {}}}});
        previewTimeline.tracks.push_back({target->GetID(), TimelineChannel::LightColor, TimelineInterpolation::Step, {{0, {0.5f, 0.25f, 0.1f}, {}}}});
        Check(sequence->SetTimeline(previewTimeline), "Continuous preview setup");
        SceneSerializer::SaveToString(scene, beforePreview);
        Check(preview.Begin(scene, root->GetID(), true), "Continuous preview start");
        {
            auto pose = preview.RenderPose(scene, 0.5);
            Check(pose && camera->GetCamera()->GetFOV() == 80 && audio->GetVolume() == 0.25f && !audio->IsPlaying(), "Camera/audio preview mismatch");
            Check(target->GetScale() == glm::vec3(2, 3, 4) && target->GetRotation() == glm::vec3(10, 20, 30), "Transform preview mismatch");
        }
        SceneSerializer::SaveToString(scene, afterPreview);
        Check(beforePreview == afterPreview, "Continuous channels polluted authoring snapshot");
        sequence->SetEnabled(false);
        Check(!preview.RenderPose(scene, 0), "Disabled sequencer retained preview");
        sequence->SetEnabled(true);
        Check(sequence->SetTimeline(timeline), "Restore original timeline");
        std::string serialized, error;
        Check(SceneSerializer::SaveToString(scene, serialized, &error), "Serialize");
        auto restored = SceneSerializer::LoadFromString(serialized, &error);
        Check(restored != nullptr, "Load");
        auto *copySequence = restored->FindEntityByID(root->GetID())->GetComponent<SequencerComponent>();
        Check(copySequence && copySequence->GetTimeline().Encode() == timeline.Encode(), "Timeline persisted");
        auto *copy = Prefab::DuplicateEntity(scene, *root);
        Check(copy != nullptr && copy->GetChildren().size() == 1, "Duplicate");
        Check(copy->GetComponent<SequencerComponent>()->GetTimeline().tracks[0].entity == copy->GetChildren()[0]->GetID(), "Remap binding");
        bool malformedRejected = false;
        try { sequence->Deserialize({{"Timeline", PropertyType::String, "broken"}}); }
        catch (const std::invalid_argument &) { malformedRejected = true; }
        Check(malformedRejected, "Malformed serialized timeline was silently accepted");
        Check(sequence->GetTimeline().Encode() == timeline.Encode(), "Invalid data rejected atomically");
        scene.StartRuntime();
        sequence->Update(0.25f);
        Check(target->GetPosition().x == 2.5f, "Runtime advance");
        sequence->Update(0);
        Check(target->GetPosition().x == 2.5f, "Pause");
        scene.StopRuntime();
        Check(!sequence->IsPlaying(), "Stop runtime");
        scene.StartRuntime();
        sequence->Update(0.5f);
        Check(target->GetPosition().x == 5, "Runtime restart");
        scene.StopRuntime();
        scene.RemoveEntity(target);
        Check(sequence->MissingBindings().size() == 2, "Deleted binding reported");
        Check(sequence->Scrub(0.75), "Missing binding tolerated");
        {
            Scene eventScene;
            auto &engine = PlutoGE::core::Engine::GetInstance();
            engine.GetScriptEngine().RegisterNativeClass({.className = "SequenceEventTest"}, [] { return std::make_unique<EventScript>(); });
            auto *eventOwner = eventScene.AddEntity(std::make_unique<Entity>());
            auto *events = eventOwner->CreateComponent<SequencerComponent>();
            eventOwner->CreateComponent<ScriptComponent>(ScriptComponentConfig{.scriptClass = "SequenceEventTest"});
            auto *sound = eventOwner->CreateComponent<SoundEmitterComponent>();
            Timeline eventTimeline;
            eventTimeline.tracks = {
                {eventOwner->GetID(), TimelineChannel::ScriptEvent, TimelineInterpolation::Step,
                    {{0.25, {1, 0, 0}, "mark"}, {0.5, {2, 0, 0}, "stop"}}},
                {eventOwner->GetID(), TimelineChannel::AudioPlay, TimelineInterpolation::Step, {{0.75, {}, {}}}}};
            Check(events->SetTimeline(eventTimeline), "Runtime event setup");
            EventRuntimeScope runtime(eventScene);
            sound->SetClipReference("test-event.wav"); // No audio device or file is needed to test playback intent.
            events->Update(1);
            Check(EventScript::received == std::vector<std::string>{"mark", "stop"} && EventScript::parameter == 2, "Script event ordering/payload mismatch");
            Check(!sound->IsPlaying() && !eventScene.IsRuntimeStarted(), "Events continued after a script stopped runtime");
        }
        std::cout << "Sequencer scene tests passed\n";
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
