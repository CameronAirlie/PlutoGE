// Draws the Outfit and Wearable inspectors (docs/SKELETAL_IK.md, "Clothing and outfits") in a hidden OpenGL window over
// a real outfit: a fitting item, one made for a taller body, a missing prefab and a row not chosen yet; and a wearable
// with a covered joint its skeleton lacks, checked against a taller body. Every state must draw without asserting and
// drawing alone must edit nothing. With an output prefix argument, writes a .ppm of each view for visual review.
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/OutfitComponent.h"
#include "PlutoGE/scene/components/WearableComponent.h"
#include "PlutoGE/ui/ComponentInspectors.h"
#include "PlutoGE/ui/OutfitInspectors.h"
#include "ImGuiTestWindow.h"
#include "OutfitTestFixtures.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace PlutoGE;
    using ui::testing::kHeight;
    using ui::testing::Window;
    constexpr float kInspectorWidth = 440.0f;

    void Require(bool value, const std::string &message)
    {
        if (!value) throw std::runtime_error(message);
    }

    struct RecordingHost final : ui::OutfitInspectorHost
    {
        std::vector<std::string> steps;
        std::string fitBody;
        void EditStep(const char *label, const std::function<void()> &edit) override
        {
            steps.emplace_back(label);
            edit();
        }
        const assets::Project *Project() const override { return nullptr; }
        const render::Mesh *LoadMesh(const std::string &reference) override
        {
            return core::Engine::GetInstance().GetAssetManager().LoadMeshAsset(reference);
        }
        std::string &FitCheckBody(std::uint32_t) override { return fitBody; }
    };

    std::string WritePrefab(const std::filesystem::path &path, const char *slot, std::vector<std::string> covers, const char *mesh)
    {
        scene::Scene authoring;
        auto *entity = authoring.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = path.stem().string()}));
        auto *wearable = entity->CreateComponent<scene::WearableComponent>();
        wearable->SetSlot(slot);
        wearable->SetCoveredJoints(std::move(covers));
        entity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{})
            ->Deserialize({{"MeshAssetReference", scene::PropertyType::String, mesh}});
        std::string error;
        Require(scene::Prefab::SaveFromEntity(*entity, path, &error), error);
        return path.string();
    }

    void TestViews(const std::filesystem::path &root, const std::string &capturePrefix)
    {
        Window window;
        if (!window.Create())
        {
            std::cout << "OutfitInspectorTests: no OpenGL context, skipping views\n";
            return;
        }
        auto &manager = core::Engine::GetInstance().GetAssetManager();
        manager.SetProjectContext(root.string(), ".");
        const std::vector<std::string> material{std::string(assets::Project::kBuiltinDefaultMaterialReference)};
        std::string error;
        Require(manager.SaveMeshAsset("project://Body.plutomesh", outfit_fixtures::SkinnedConfig(outfit_fixtures::Body()), material, &error), error);
        Require(manager.SaveMeshAsset("project://Tall.plutomesh", outfit_fixtures::SkinnedConfig(outfit_fixtures::Body(1.08f)), material, &error), error);
        const auto coat = WritePrefab(root / "Coat.plutoprefab", "Body", {"spine"}, "project://Body.plutomesh");
        const auto tall = WritePrefab(root / "TallHood.plutoprefab", "Head", {"head"}, "project://Tall.plutomesh");

        scene::Scene world;
        auto *character = world.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Character"}));
        character->CreateComponent<scene::AnimationComponent>();
        character->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = manager.LoadMeshAsset("project://Body.plutomesh")});
        auto *outfit = character->CreateComponent<scene::OutfitComponent>();
        outfit->Deserialize({{"Wearable Count", scene::PropertyType::Int, "4"},
                             {"Wearables.0", scene::PropertyType::String, coat},
                             {"Wearables.1", scene::PropertyType::String, tall},
                             {"Wearables.2", scene::PropertyType::String, (root / "Missing.plutoprefab").string()},
                             {"Wearables.3", scene::PropertyType::String, ""}});
        world.Update(0.0f);
        Require(outfit->GetWornItems().size() == 4, "the outfit lists four rows");

        scene::Scene authoring;
        auto *garment = authoring.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Coat"}));
        auto *wearable = garment->CreateComponent<scene::WearableComponent>();
        wearable->SetCoveredJoints({"spine", "tail"});
        garment->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = manager.LoadMeshAsset("project://Body.plutomesh")});

        RecordingHost host;
        host.fitBody = "project://Tall.plutomesh";
        const auto outfitBefore = outfit->Serialize();
        const auto wearableBefore = wearable->Serialize();
        const auto draw = [&](const std::function<void()> &inspector)
        {
            return [&, inspector]
            {
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImVec2(kInspectorWidth, float(kHeight)));
                ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoDecoration);
                inspector();
                ImGui::End();
            };
        };
        const auto frames = [&](const char *name, const std::function<void()> &frame)
        {
            for (int index = 0; index < 4; ++index) window.Frame(frame);
            if (!capturePrefix.empty()) window.Capture(capturePrefix + name + ".ppm");
        };
        frames("outfit", draw([&] { ui::DrawOutfitInspector(*outfit, *character, host); }));
        frames("wearable", draw([&] { ui::DrawWearableInspector(*wearable, *garment, host); }));
        host.fitBody = "project://Body.plutomesh";
        frames("wearable-fits", draw([&] { ui::DrawWearableInspector(*wearable, *garment, host); }));

        Require(host.steps.empty(), "drawing the inspectors untouched reports no edits");
        const auto same = [](const std::vector<scene::Property> &a, const std::vector<scene::Property> &b)
        {
            if (a.size() != b.size()) return false;
            for (std::size_t index = 0; index < a.size(); ++index)
                if (a[index].name != b[index].name || a[index].value != b[index].value) return false;
            return true;
        };
        Require(same(outfit->Serialize(), outfitBefore), "drawing the outfit inspector keeps the list");
        Require(same(wearable->Serialize(), wearableBefore), "drawing the wearable inspector keeps its settings");
    }

    void TestRegistered()
    {
        Require(ui::FindComponentInspector(scene::GetComponentTypeID<scene::OutfitComponent>()) != nullptr, "the outfit has a custom inspector");
        Require(ui::FindComponentInspector(scene::GetComponentTypeID<scene::WearableComponent>()) != nullptr, "the wearable has a custom inspector");
    }
}

int main(int argc, char **argv)
{
    const auto root = std::filesystem::temp_directory_path() / "PlutoGEOutfitInspectorTests";
    try
    {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        TestRegistered();
        TestViews(root, argc > 1 ? argv[1] : "");
    }
    catch (const std::exception &error)
    {
        std::cerr << "OutfitInspectorTests failed: " << error.what() << '\n';
        return 1;
    }
    std::filesystem::remove_all(root);
    std::cout << "OutfitInspectorTests passed\n";
    return 0;
}
