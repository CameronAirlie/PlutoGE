#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/UISystem.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/UIDropdownComponent.h"
#include "PlutoGE/scene/components/UIInputFieldComponent.h"
#include "PlutoGE/scene/components/UIScrollViewComponent.h"
#include "PlutoGE/scene/components/UISliderComponent.h"
#include "PlutoGE/scene/components/UIToggleComponent.h"
#include "PlutoGE/ui/ComponentInspectors.h"
#include "PlutoGE/ui/hierarchy/UIWidgetPresets.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    using namespace PlutoGE;

    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    bool InsideCanvas(const scene::Entity *entity)
    {
        for (; entity; entity = entity->GetParent())
            if (entity->GetComponent<scene::CanvasComponent>())
                return true;
        return false;
    }

    void RequireReference(scene::Scene &scene, scene::UIEntityID id, const std::string &what)
    {
        Require(id != 0 && scene.FindEntityByID(id) != nullptr, what + " reference is not wired");
    }

    void TestWidgetPresets()
    {
        for (const auto &preset : ui::GetUIWidgetPresets())
        {
            scene::Scene scene;
            const ui::EntityPresetContext context{.scene = scene, .assets = core::Engine::GetInstance().GetAssetManager()};
            auto *created = preset.create(context);
            const std::string name = preset.menuLabel;
            Require(created != nullptr, name + " created nothing");
            Require(InsideCanvas(created), name + " must end up inside a canvas");

            if (auto *slider = created->GetComponent<scene::UISliderComponent>())
            {
                RequireReference(scene, slider->GetFill(), "Slider fill");
                RequireReference(scene, slider->GetHandle(), "Slider handle");
            }
            if (auto *toggle = created->GetComponent<scene::UIToggleComponent>())
                RequireReference(scene, toggle->GetGraphic(), "Toggle checkmark");
            if (auto *scroll = created->GetComponent<scene::UIScrollViewComponent>())
                RequireReference(scene, scroll->GetContent(), "Scroll view content");
            if (auto *input = created->GetComponent<scene::UIInputFieldComponent>())
                RequireReference(scene, input->GetPlaceholder(), "Input field placeholder");
            if (auto *dropdown = created->GetComponent<scene::UIDropdownComponent>())
                Require(dropdown->GetOptions().size() == 3, "Dropdown needs sample options");

            auto &ui = scene.GetUISystem();
            const auto &canvases = ui.BuildDrawLists(scene, {1920.0f, 1080.0f});
            Require(!canvases.empty(), name + " has no canvas to draw");
            const auto *element = ui.FindElement(created->GetID());
            Require(element != nullptr, name + " was not laid out");
            if (name != "Canvas" && name != "Row" && name != "Column" && name != "Grid")
                Require(!canvases.front().drawList.Empty(), name + " painted nothing");
            Require(element->rect.max.x > element->rect.min.x && element->rect.max.y > element->rect.min.y,
                    name + " has an empty rect");

            // A second widget goes into the same canvas when created under it.
            const ui::EntityPresetContext nested{.scene = scene, .parent = created,
                                                 .assets = core::Engine::GetInstance().GetAssetManager()};
            auto *child = ui::GetUIWidgetPresets()[2].create(nested);
            Require(child && child->GetParent() == created, name + ": widgets created inside a canvas must not add another");
        }
    }

    void TestInspectorRegistry()
    {
        Require(ui::FindComponentInspector(scene::GetComponentTypeID<scene::RectTransformComponent>()) != nullptr,
                "RectTransform needs its anchor inspector");
        Require(ui::FindComponentInspector(scene::GetComponentTypeID<scene::UITextComponent>()) != nullptr,
                "Text needs its multiline inspector");
        Require(ui::FindComponentInspector(scene::GetComponentTypeID<scene::MeshComponent>()) == nullptr,
                "Unregistered components fall back to the generic editor");
    }
}

int main()
{
    try
    {
        TestWidgetPresets();
        TestInspectorRegistry();
    }
    catch (const std::exception &error)
    {
        std::cerr << "NativeUIEditorTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "NativeUIEditorTests passed\n";
    return 0;
}
