// How the cost of editing a scene grows with the scene. Machines differ, so it asserts on how many saves an edit takes
// and on ratios between scene sizes, never on milliseconds; the times it prints are the baseline to compare runs by.
//
// Real scenes:  PlutoGESceneEditScalingTests <project .plutoproject> <scene file>...
// Reports their numbers rather than asserting. Loading with project context may write to the project (imports, library
// entries), so point this at a copy.
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/ui/SceneEditHistory.h"
#include "PlutoGE/ui/SceneSnapshots.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE;
using namespace PlutoGE::scene;
using namespace PlutoGE::ui;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    double MeasureMs(const std::function<void()> &work)
    {
        const auto start = std::chrono::steady_clock::now();
        work();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    double BestOfMs(int runs, const std::function<void()> &work)
    {
        double best = std::numeric_limits<double>::max();
        for (int run = 0; run < runs; ++run) best = (std::min)(best, MeasureMs(work));
        return best;
    }

    class CountingHost final : public ISceneHistoryHost
    {
    public:
        explicit CountingHost(std::unique_ptr<Scene> initial) : scene(std::move(initial)) {}
        std::unique_ptr<Scene> scene;
        std::vector<EntityID> selection;
        std::uint64_t selectionRevision = 1;
        int captures = 0;

        Scene *GetHistoryScene() override { return scene.get(); }
        bool CaptureSceneSnapshot(SceneSnapshot &snapshot, std::string &error) override
        {
            ++captures;
            std::string text;
            if (!SceneSerializer::SaveToString(*scene, text, &error)) return false;
            snapshot.text = MakeSceneStateText(std::move(text));
            return true;
        }
        bool RestoreSceneSnapshot(const SceneSnapshot &snapshot, bool &exact, std::string &error) override
        {
            auto restored = LoadSceneSnapshot(*snapshot.text, error);
            if (!restored) return false;
            scene = std::move(restored);
            exact = true;
            return true;
        }
        void OnEntitiesChangedInPlace() override {}
        void OnSceneEdited() override {}
        bool IsHistorySuspended() const override { return false; }
        std::vector<EntityID> GetEditableSelection() const override { return selection; }
        std::uint64_t GetSelectionRevision() const override { return selectionRevision; }
        void LogHistoryWarning(const std::string &message) override { throw std::runtime_error(message); }
    };

    std::unique_ptr<Scene> MakeSyntheticScene(int entityCount)
    {
        auto scene = std::make_unique<Scene>();
        Entity *group = nullptr;
        for (int index = 0; index < entityCount; ++index)
        {
            if (index % 32 == 0) group = nullptr;
            auto *entity = scene->AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Entity " + std::to_string(index)}), group);
            entity->SetPosition({static_cast<float>(index), 0.0f, 0.0f});
            entity->CreateComponent<ColliderComponent>();
            if (index % 4 == 0) entity->CreateComponent<LightComponent>();
            if (!group) group = entity;
        }
        return scene;
    }

    struct SizeResult
    {
        int entities = 0;
        std::size_t bytes = 0;
        double saveMs = 0.0;
        double loadMs = 0.0;
        double snapshotEditMs = 0.0;
        double snapshotUndoMs = 0.0;
        double scopedEditMs = 0.0;
        double scopedUndoMs = 0.0;
        double structuralEditMs = 0.0;
        double structuralUndoMs = 0.0;
    };

    SizeResult MeasureSize(int entityCount)
    {
        SizeResult result{.entities = entityCount};
        CountingHost host(MakeSyntheticScene(entityCount));
        SceneEditHistory history(host);
        history.Resynchronize();
        auto *target = host.scene->GetRootEntities().front();
        const auto targetId = target->GetID();
        host.selection = {targetId};

        std::string text, error;
        result.saveMs = BestOfMs(3, [&] { Require(SceneSerializer::SaveToString(*host.scene, text, &error), error); });
        result.bytes = text.size();
        result.loadMs = BestOfMs(2, [&] { Require(LoadSceneSnapshot(text, error) != nullptr, error); });

        std::string label;
        const auto undo = [&]
        {
            Require(history.Undo(label, error), "Undo failed: " + error);
        };

        // Snapshot edit: the path every edit took before scoped recording.
        host.captures = 0;
        float step = 1.0f;
        result.snapshotEditMs = MeasureMs([&] { history.ExecuteEdit("Snapshot", [&] { host.scene->GetRootEntities().front()->SetName("A" + std::to_string(step)); }); });
        Require(host.captures == 1, "A snapshot edit took " + std::to_string(host.captures) + " saves, expected 1");
        host.captures = 0;
        result.snapshotUndoMs = MeasureMs(undo);
        Require(host.captures == 0, "A snapshot undo took " + std::to_string(host.captures) + " saves, expected 0");

        // Gesture.
        history.PrepareForEdits();
        host.captures = 0;
        history.BeginGesture("Gesture");
        host.scene->GetRootEntities().front()->SetName("Gesture");
        history.NoteEdit(SceneEditReach::Scene);
        history.EndGesture();
        Require(host.captures == 1, "A gesture took " + std::to_string(host.captures) + " saves, expected 1");
        undo();

        // A property or transform edit, as the inspector or gizmo makes one.
        host.captures = 0;
        result.scopedEditMs = BestOfMs(9, [&]
        {
            history.PrepareForEdits();
            step += 1.0f;
            host.scene->FindEntityByID(targetId)->SetPosition({step, 0.0f, 0.0f});
            history.NoteEdit(SceneEditReach::Selection);
            history.Flush();
        });
        Require(host.captures == 0, "A scoped edit saved the scene " + std::to_string(host.captures) + " times");
        result.scopedUndoMs = BestOfMs(9, undo);
        Require(host.captures == 0, "A scoped undo saved the scene " + std::to_string(host.captures) + " times");

        // A structural edit: deleting a leaf.
        history.PrepareForEdits();
        host.captures = 0;
        const auto leaf = host.scene->FindEntityByID(targetId)->GetChildren().back()->GetID();
        result.structuralEditMs = MeasureMs([&]
        {
            history.ExecuteEdit("Delete", [&] { host.scene->RemoveEntity(host.scene->FindEntityByID(leaf)); }, SceneEditScope::Subtrees({leaf}));
        });
        result.structuralUndoMs = MeasureMs(undo);
        Require(host.captures == 0, "A structural edit saved the scene " + std::to_string(host.captures) + " times");
        Require(host.scene->FindEntityByID(leaf) != nullptr, "Structural undo did not restore the entity");
        return result;
    }

    void RunScalingChecks()
    {
        std::vector<SizeResult> results;
        for (const int size : {1000, 5000, 20000})
            results.push_back(MeasureSize(size));

        std::cout << std::fixed << std::setprecision(3)
                  << "entities     MB    save ms   load ms  snap-edit snap-undo scoped-edit scoped-undo struct-edit struct-undo\n";
        for (const auto &result : results)
            std::cout << std::setw(8) << result.entities << ' ' << std::setw(6) << result.bytes / 1e6
                      << std::setw(11) << result.saveMs << std::setw(10) << result.loadMs
                      << std::setw(11) << result.snapshotEditMs << std::setw(10) << result.snapshotUndoMs
                      << std::setw(12) << result.scopedEditMs << std::setw(12) << result.scopedUndoMs
                      << std::setw(12) << result.structuralEditMs << std::setw(12) << result.structuralUndoMs << '\n';

        // Twenty times the entities. Saves should scale with them and scoped edits should not, so these hold with room
        // to spare for timer noise on these small numbers.
        const auto &small = results.front();
        const auto &large = results.back();
        constexpr double kNoiseFloorMs = 0.25;
        const auto ratio = [](double to, double from) { return (std::max)(to, kNoiseFloorMs) / (std::max)(from, kNoiseFloorMs); };
        Require(ratio(large.saveMs, small.saveMs) > 5.0, "Saves should grow with the scene, or the test is not measuring it");
        Require(ratio(large.scopedEditMs, small.scopedEditMs) < 5.0, "A scoped edit's cost grew with the scene");
        Require(ratio(large.scopedUndoMs, small.scopedUndoMs) < 5.0, "A scoped undo's cost grew with the scene");
        // Finding what a structural edit added walks the hierarchy, which must stay far below what a save costs.
        Require(large.structuralEditMs < large.saveMs * 0.25, "A structural edit should cost a fraction of a save");
        Require(large.structuralUndoMs < large.saveMs * 0.25, "A structural undo should cost a fraction of a save");
    }

    // Repeated saves should reuse encoded heavy data, however large it is.
    void RunHeavyDataChecks()
    {
        CountingHost host(MakeSyntheticScene(10));
        auto *owner = host.scene->GetRootEntities().front();
        auto *foliage = owner->CreateComponent<FoliageComponent>();
        foliage->AddType("Type");
        FoliageInstanceSnapshot instances(1);
        constexpr int kInstances = 400000;
        instances[0].reserve(kInstances);
        for (int index = 0; index < kInstances; ++index)
            instances[0].push_back({.id = static_cast<std::uint64_t>(index + 1),
                                    .position = {index * 0.137f, index * 0.011f, index * 0.293f},
                                    .rotationDegrees = {0.0f, index * 0.77f, 0.0f},
                                    .scale = {1.1f, 1.1f, 1.1f}});
        foliage->RestoreInstanceSnapshot(instances);

        std::string text, error;
        const double encodeMs = MeasureMs([&] { Require(SceneSerializer::SaveToString(*host.scene, text, &error), error); });
        const double cachedMs = BestOfMs(3, [&] { Require(SceneSerializer::SaveToString(*host.scene, text, &error), error); });
        double loadMs = 0.0;
        loadMs = MeasureMs([&] { Require(LoadSceneSnapshot(text, error) != nullptr, error); });
        std::cout << std::fixed << std::setprecision(1) << kInstances << " foliage instances, " << text.size() / 1e6
                  << " MB: first save " << encodeMs << " ms, later saves " << cachedMs << " ms, load " << loadMs << " ms\n";
        Require(cachedMs < encodeMs * 0.5, "A save that did not touch foliage should reuse its encoded text");
    }

    // The encoders the plan left as they were, timed alone. Reported, not asserted: this is what would justify changing them.
    void ReportUnchangedEncoders()
    {
        Entity terrainOwner;
        auto *terrain = terrainOwner.CreateComponent<TerrainComponent>();
        constexpr int kSamplesPerSide = 1025;
        terrain->SetSize(kSamplesPerSide, kSamplesPerSide);
        std::string samples;
        samples.reserve(kSamplesPerSide * kSamplesPerSide * 10);
        for (int index = 0; index < kSamplesPerSide * kSamplesPerSide; ++index)
        {
            if (index) samples += ',';
            samples += std::to_string(std::fmod(index * 0.0137f, 10.0f));
        }
        double readMs = MeasureMs([&] { terrain->Deserialize({{"Width", PropertyType::Int, std::to_string(kSamplesPerSide)},
                                                              {"Depth", PropertyType::Int, std::to_string(kSamplesPerSide)},
                                                              {"HeightSamples", PropertyType::String, samples}}); });
        std::size_t encodedBytes = 0;
        double writeMs = MeasureMs([&] { for (const auto &property : terrain->SerializeShared()) if (property.sharedValue) encodedBytes = property.sharedValue->size(); });
        std::cout << std::fixed << std::setprecision(1) << "Terrain heights, " << encodedBytes / 1e6 << " MB: encode " << writeMs << " ms, decode " << readMs << " ms" << std::endl;

        Entity captureOwner;
        auto *capture = captureOwner.CreateComponent<IblCaptureComponent>();
        constexpr int kResolution = 512;
        capture->SetResolution(kResolution);
        std::vector<float> pixels(static_cast<std::size_t>(kResolution) * kResolution * 4 * 6);
        for (std::size_t index = 0; index < pixels.size(); ++index) pixels[index] = static_cast<float>(index % 251) * 0.01f;
        Require(capture->SetCapturePixels(pixels), "Set capture pixels");
        std::vector<Property> encoded;
        writeMs = MeasureMs([&] { encoded = capture->SerializeShared(); });
        std::string base64;
        for (const auto &property : encoded) if (property.sharedValue) base64 = *property.sharedValue;
        readMs = MeasureMs([&] { capture->Deserialize({{"Capture Pixels", PropertyType::String, base64}}); });
        std::cout << "IBL capture Base64, " << base64.size() / 1e6 << " MB: encode " << writeMs << " ms, decode and upload " << readMs << " ms" << std::endl;
    }

    // ------------------------------------------------------------------ Real scenes

    void ReportRealScene(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        Require(input.is_open(), "Cannot read " + path.string());
        std::ostringstream buffer;
        buffer << input.rdbuf();
        const std::string fileText = buffer.str();

        std::string error;
        std::unique_ptr<Scene> scene;
        const double fileLoadMs = MeasureMs([&] { scene = SceneSerializer::LoadFromString(fileText, &error); });
        Require(scene != nullptr, "Cannot load " + path.string() + ": " + error);
        if (!error.empty()) std::cout << "  load reported: " << error << '\n';

        // Measured before anything else reads the scene, so this one pays for encoding its heavy data.
        std::string text;
        const double firstSaveMs = MeasureMs([&] { Require(SceneSerializer::SaveToString(*scene, text, &error), error); });

        std::size_t entityCount = 0;
        std::vector<const Entity *> pending(scene->GetRootEntities().begin(), scene->GetRootEntities().end());
        std::map<std::string, std::size_t> propertyBytes;
        while (!pending.empty())
        {
            const auto *entity = pending.back();
            pending.pop_back();
            ++entityCount;
            pending.insert(pending.end(), entity->GetChildren().begin(), entity->GetChildren().end());
            for (const auto &bucket : entity->GetComponentBuckets())
                for (const auto *component : bucket)
                    for (const auto &property : component->SerializeShared())
                        if (property.Text().size() >= 256 * 1024)
                        {
                            auto name = property.name;
                            for (auto &character : name) if (character >= '0' && character <= '9') character = 'N';
                            propertyBytes[SceneSerializer::GetComponentTypeName(*component) + "." + name] += property.Text().size();
                        }
        }

        const double cachedSaveMs = BestOfMs(3, [&] { Require(SceneSerializer::SaveToString(*scene, text, &error), error); });
        std::unique_ptr<Scene> reloaded;
        const double loadMs = MeasureMs([&] { reloaded = LoadSceneSnapshot(text, error); });
        Require(reloaded != nullptr, "Cannot reload the saved text: " + error);
        std::string secondText;
        Require(SceneSerializer::SaveToString(*reloaded, secondText, &error), error);

        std::cout << std::fixed << std::setprecision(1) << path.filename().string() << ": " << entityCount << " entities, "
                  << text.size() / 1e6 << " MB (file " << fileText.size() / 1e6 << " MB)\n"
                  << "  file load " << fileLoadMs << " ms, first save " << firstSaveMs << " ms, cached save " << cachedSaveMs
                  << " ms, reload " << loadMs << " ms\n"
                  << "  Save(Load(x)) == x: " << (secondText == text ? "yes" : "NO") << '\n';
        for (const auto &[name, bytes] : propertyBytes)
            std::cout << "  heavy: " << name << ' ' << bytes / 1e6 << " MB\n";
        if (secondText != text)
        {
            const auto mismatch = std::mismatch(text.begin(), text.end(), secondText.begin(), secondText.end());
            const auto offset = static_cast<std::size_t>(mismatch.first - text.begin());
            const auto start = offset > 60 ? offset - 60 : 0;
            std::cout << "  first difference at byte " << offset << ": '" << text.substr(start, 120) << "' vs '" << secondText.substr(start, 120) << "'\n";
        }
    }

    int RunRealScenes(int argc, char **argv)
    {
        const std::filesystem::path projectPath = argv[1];
        std::string error;
        const auto project = assets::Project::Load(projectPath, &error);
        Require(project != nullptr, "Cannot load project: " + error);
        core::Engine::GetInstance().GetAssetManager().SetProjectContext(project->GetRootDirectory().string(), project->GetManifest().assetDirectory, project->GetManifest().assetPipelineVersion);
        for (int index = 2; index < argc; ++index)
            ReportRealScene(argv[index]);
        return 0;
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc > 2) return RunRealScenes(argc, argv);
        RunScalingChecks();
        RunHeavyDataChecks();
        ReportUnchangedEncoders();
        std::cout << "PASS: scene edit scaling\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
