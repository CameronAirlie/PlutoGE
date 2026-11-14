#include "PlutoGE/render/DetailGrass.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/DetailGrassComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/ui/ComponentInspectors.h"
#include "PlutoGE/ui/DetailGrassInspector.h"
#include "PlutoGE/ui/EditorFonts.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "ImGuiTestWindow.h"

#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Draws the detail grass inspector (docs/DETAIL_GRASS.md) in a hidden OpenGL window: it must draw every section
// without asserting, report nothing while untouched, and route list edits through its host as labelled steps. With an
// output prefix argument, writes a .ppm of each view for visual QA.
namespace
{
    using namespace PlutoGE;

    using ui::testing::kWidth;
    using ui::testing::kHeight;
    using ui::testing::Window;
    constexpr float kInspectorWidth = 420.0f;

    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    struct RecordingHost final : ui::DetailGrassInspectorHost
    {
        std::vector<std::string> steps;
        int changes = 0;
        void EditStep(const char *label, const std::function<void()> &edit) override
        {
            steps.emplace_back(label);
            edit();
        }
        void Changed() override { ++changes; }
        const assets::Project *Project() const override { return nullptr; }
    };

    // A terrain with two painted layers and grass with three layers, one naming a terrain layer that does not exist.
    struct Fixture
    {
        scene::Scene scene;
        scene::Entity *entity = nullptr;
        scene::DetailGrassComponent *grass = nullptr;

        Fixture()
        {
            entity = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Terrain"}));
            auto *terrain = entity->CreateComponent<scene::TerrainComponent>(scene::TerrainComponentConfig{.width = 9, .depth = 9, .cellSize = 2.0f});
            terrain->GetLayers().AddLayer({.name = "Forest floor"});
            terrain->GetLayers().AddLayer({.name = "Leaf litter"});
            grass = entity->CreateComponent<scene::DetailGrassComponent>();
            scene::DetailGrassComponent::Layer meadow{render::DetailGrassPresetLayer(render::DetailGrassPreset::Meadow), "Forest floor", ""};
            scene::DetailGrassComponent::Layer tufts{render::DetailGrassPresetLayer(render::DetailGrassPreset::DryTufts), "Leaf litter", ""};
            scene::DetailGrassComponent::Layer lost{render::DetailGrassPresetLayer(render::DetailGrassPreset::Weeds), "Gravel", ""};
            lost.settings.enabled = false;
            grass->SetLayers({meadow, tufts, lost});
        }
    };

    // Opens the inspector's collapsed sections, as a user would by clicking their headers.
    void OpenAllSections()
    {
        ImGui::PushID("SelectedGrassLayer");
        for (const char *title : {"Where It Grows", "Size And Density", "Colour", "Shading", "Wind", "Clump Shape", "Distance And Cost"})
            ImGui::GetStateStorage()->SetInt(ImGui::GetID(title), 1);
        ImGui::PopID();
    }

    void TestViews(const std::string &capturePrefix)
    {
        Window window;
        if (!window.Create())
        {
            std::cout << "DetailGrassInspectorTests: no OpenGL context, skipping views\n";
            return;
        }
        Fixture fixture;
        RecordingHost host;
        const auto before = fixture.grass->Serialize();
        bool openAll = false;
        float scroll = 0.0f;
        const auto draw = [&]
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(kInspectorWidth, float(kHeight)));
            ImGui::SetNextWindowScroll(ImVec2(0.0f, scroll));
            ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoDecoration);
            if (openAll) OpenAllSections();
            ui::DrawDetailGrassInspector(*fixture.grass, *fixture.entity, host);
            ImGui::End();
        };
        const auto frames = [&](const char *name)
        {
            for (int frame = 0; frame < 4; ++frame) window.Frame(draw);
            if (!capturePrefix.empty()) window.Capture(capturePrefix + name + ".ppm");
        };

        frames("default");
        openAll = true;
        frames("sections");
        scroll = 820.0f;
        frames("lower");
        scroll = 2000.0f;
        frames("bottom");
        Require(host.changes == 0 && host.steps.empty(), "Drawing the inspector untouched reports no edits");
        Require(fixture.grass->Serialize().size() == before.size(), "Drawing the inspector untouched keeps the layers");
        for (std::size_t index = 0; index < before.size(); ++index)
            Require(fixture.grass->Serialize()[index].value == before[index].value,
                    "Drawing the inspector untouched keeps " + before[index].name);
    }

    void TestRegistered()
    {
        Require(ui::FindComponentInspector(scene::GetComponentTypeID<scene::DetailGrassComponent>()) != nullptr,
                "Detail grass has a custom inspector");
    }
}

int main(int argc, char **argv)
{
    try
    {
        TestRegistered();
        TestViews(argc > 1 ? argv[1] : "");
    }
    catch (const std::exception &error)
    {
        std::cerr << "DetailGrassInspectorTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "DetailGrassInspectorTests passed\n";
    return 0;
}
