#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/UISystem.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/UIDragDropComponents.h"
#include "PlutoGE/scene/components/UIShapeComponent.h"
#include "PlutoGE/scene/components/UISliderComponent.h"
#include "PlutoGE/scripting/ScriptEngine.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

using namespace PlutoGE;

namespace
{
    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    template <typename T>
    T Field(const std::unordered_map<std::string, scripting::ScriptFieldValue> &values, const char *name)
    {
        const auto found = values.find(name);
        Require(found != values.end(), std::string("Probe field missing: ") + name);
        return std::get<T>(found->second);
    }
}

int main()
try
{
    auto &engine = core::Engine::GetInstance();
    auto &scripts = engine.GetScriptEngine();
    struct Reset
    {
        ~Reset()
        {
            core::Engine::GetInstance().SetScene(nullptr);
            core::Engine::GetInstance().GetScriptEngine().Shutdown();
        }
    } reset;

    scene::Scene scene;
    auto *canvasEntity = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Canvas"}));
    auto *canvas = canvasEntity->CreateComponent<scene::CanvasComponent>();
    canvas->SetBackend(scene::UIRenderBackend::Native);
    canvas->SetScaleMode(scene::CanvasScaleMode::ConstantPixels);
    auto *slider = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Slider"}), canvasEntity);
    auto *rect = slider->CreateComponent<scene::RectTransformComponent>();
    rect->SetAnchorPreset(scene::UIAnchorPreset::BottomLeft);
    rect->SetAnchoredPosition({100, 100});
    rect->SetSizeDelta({200, 20});
    slider->CreateComponent<scene::UISliderComponent>();
    // Drag and drop: the probe adds a drag source to Tile; Slot is a drop target.
    const auto box = [&](const char *name, glm::vec2 position)
    {
        auto *entity = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = name}), canvasEntity);
        auto *boxRect = entity->CreateComponent<scene::RectTransformComponent>();
        boxRect->SetAnchorPreset(scene::UIAnchorPreset::BottomLeft);
        boxRect->SetAnchoredPosition(position);
        boxRect->SetSizeDelta({50, 50});
        entity->CreateComponent<scene::UIShapeComponent>();
        return entity;
    };
    auto *tile = box("Tile", {400, 300});
    auto *slot = box("Slot", {600, 300});
    slot->CreateComponent<scene::UIDropTargetComponent>();
    auto *owner = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "ScriptOwner"}));

    engine.SetScene(&scene);
    scripts.Initialize();
    Require(scripts.LoadAssembly(PLUTOGE_NATIVE_UI_PROBE_ASSEMBLY), scripts.GetLastError());
    auto instance = scripts.CreateInstance("PlutoGE.NativeUIProbe.UIProbe");
    Require(instance != nullptr, scripts.GetLastError());
    instance->SetOwner(owner);
    instance->OnCreate();

    auto values = instance->GetFieldValuesSnapshot();
    Require(Field<std::string>(values, "Failure").empty(), "Probe failed: " + Field<std::string>(values, "Failure"));
    Require(Field<bool>(values, "PropertiesWork"), "Typed properties did not round-trip through the bridge");
    Require(Field<bool>(values, "ComponentsWork"), "Has/Get/Add component or Enabled failed for registered components");
    Require(Field<bool>(values, "HierarchyWorks"), "Create/SetParent/Children/FindChild did not build the expected tree");
    auto *built = scene.FindEntityByName("Built Panel");
    Require(built && built->GetParent() == canvasEntity && built->GetChildren().size() == 2 &&
                built->GetChildren()[0]->GetName() == "Second" && built->GetComponent<scene::UITextComponent>() &&
                built->GetComponent<scene::UITextComponent>()->GetText() == "Built",
            "The scripted hierarchy did not reach the scene");
    auto *shape = slider->GetComponent<scene::UIShapeComponent>();
    Require(shape && shape->GetCornerRadii().topLeft == 6.0f, "AddComponent<UIShapeComponent> did not reach the scene");

    // A pointer press at 75% of the slider: the script's ValueChanged handler and binding see it.
    auto &ui = scene.GetUISystem();
    const auto frame = [&](bool down, bool pressed, bool released)
    {
        ui.Update(scene,
                  {.viewportSize = {800, 600}, .pointerPosition = {250, 110}, .pointerInside = true,
                   .pointerDown = down, .pointerPressed = pressed, .pointerReleased = released},
                  1.0f / 60.0f);
        instance->OnUpdate(1.0f / 60.0f); // scripts run after the UI each frame
    };
    frame(true, true, false);
    frame(false, false, true);
    values = instance->GetFieldValuesSnapshot();
    Require(Field<std::string>(values, "Failure").empty(), "Probe failed: " + Field<std::string>(values, "Failure"));
    Require(Field<int32_t>(values, "ObservedEvents") == 1, "Expected one ValueChanged event, got " +
            std::to_string(Field<int32_t>(values, "ObservedEvents")));
    Require(std::abs(Field<float>(values, "ObservedValue") - 7.5f) < 0.05f, "ValueChanged reported the wrong value");

    Require(tile->GetComponent<scene::UIDragSourceComponent>(), "AddComponent<UIDragSourceComponent> did not reach the scene");
    // Drag the tile onto the slot.
    const auto pointer = [&](glm::vec2 at, glm::vec2 delta, bool down, bool pressed, bool released)
    {
        ui.Update(scene,
                  {.viewportSize = {800, 600}, .pointerPosition = at, .pointerDelta = delta, .pointerInside = true,
                   .pointerDown = down, .pointerPressed = pressed, .pointerReleased = released},
                  1.0f / 60.0f);
        instance->OnUpdate(1.0f / 60.0f);
    };
    pointer({425, 325}, {0, 0}, true, true, false);
    pointer({500, 325}, {75, 0}, true, false, false);
    pointer({625, 330}, {125, 5}, true, false, false);
    pointer({625, 330}, {0, 0}, false, false, true);
    values = instance->GetFieldValuesSnapshot();
    Require(Field<std::string>(values, "Failure").empty(), "Probe failed: " + Field<std::string>(values, "Failure"));
    Require(Field<int32_t>(values, "DragEvents") == 2, "Expected two Drag events, got " +
            std::to_string(Field<int32_t>(values, "DragEvents")));
    Require(Field<bool>(values, "DragBegan") && Field<bool>(values, "DragEndedOnSlot"), "BeginDrag/EndDrag(related = slot) not seen");
    Require(Field<bool>(values, "DroppedFromTile"), "Drop(related = tile) not seen by the slot's handler");
    Require(std::abs(Field<float>(values, "DropX") - 625.0f) < 0.01f && std::abs(Field<float>(values, "DropY") - 330.0f) < 0.01f,
            "Drop position is wrong: " + std::to_string(Field<float>(values, "DropX")) + ", " +
                std::to_string(Field<float>(values, "DropY")));
    Require(Field<int32_t>(values, "UnsubscribedCalls") == 0, "An unsubscribed UIEventData handler still ran");

    instance->OnDestroy();
    instance.reset();
    std::cout << "Native UI scripting checks passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
