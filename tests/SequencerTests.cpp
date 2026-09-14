#include "PlutoGE/ui/TimelinePreview.h"
#include "PlutoGE/scene/components/SequencerComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/Prefab.h"
#include <iostream>
#include <stdexcept>

using namespace PlutoGE::scene;
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
        preview.Stop();
        Check(target->GetPosition().x == 5, "Preview stop preserves authoring");
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
        std::cout << "Sequencer scene tests passed\n";
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
