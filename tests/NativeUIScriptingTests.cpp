#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/UISystem.h"
#include "PlutoGE/scene/components/UIComponent.h"
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
