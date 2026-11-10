#include "PlutoGE/render/SceneOutline.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/OutlineComponent.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace PlutoGE;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool Near(float a, float b, float tolerance = 1.0e-4f) { return std::abs(a - b) <= tolerance; }

    bool Contains(const render::OutlineLayer &layer, scene::EntityID id)
    {
        return std::ranges::find(layer.entities, id) != layer.entities.end();
    }

    bool Contains(const render::SceneOutline &outline, scene::EntityID id)
    {
        return std::ranges::any_of(outline.layers, [&](const auto &layer) { return Contains(layer, id); });
    }

    scene::Entity *Child(scene::Scene &world, scene::Entity &parent)
    {
        auto *child = world.AddEntity(std::make_unique<scene::Entity>());
        child->SetParent(&parent);
        return child;
    }

    // Values are clamped and malformed text keeps the current value, so a bad scene line cannot break loading.
    void TestComponentValues()
    {
        scene::OutlineComponent outline;
        outline.SetWidth(40.0f);
        Require(Near(outline.GetWidth(), scene::OutlineComponent::MaxWidth), "Width should clamp to what the pass samples");
        outline.SetHiddenOpacity(-1.0f);
        Require(Near(outline.GetHiddenOpacity(), 0.0f), "Hidden opacity should clamp to 0..1");
        outline.SetColor({0.2f, 0.4f, 0.6f, 3.0f});
        Require(Near(outline.GetColor().a, 1.0f), "Colour opacity should clamp to 1");

        scene::OutlineComponent loaded;
        loaded.Deserialize(outline.Serialize());
        Require(Near(loaded.GetColor().g, 0.4f) && Near(loaded.GetWidth(), outline.GetWidth()) && loaded.GetIncludeChildren(),
                "Outline properties should round-trip");
        loaded.Deserialize({{"Width", scene::PropertyType::Float, "wide"}, {"Color", scene::PropertyType::Color, "x"},
                            {"Include Children", scene::PropertyType::Bool, "false"}});
        Require(Near(loaded.GetWidth(), outline.GetWidth()) && Near(loaded.GetColor().g, 0.4f) && !loaded.GetIncludeChildren(),
                "Malformed values should keep the current ones");
    }

    // The builder outlines enabled components on active entities, with their active descendants unless told not to,
    // follows components added after the first lookup and gives each style its own layer, scaled to the output.
    void TestSceneExtraction()
    {
        scene::Scene world;
        render::SceneOutline outline;
        render::BuildSceneOutline(&world, 1080.0f, outline);
        Require(outline.layers.empty(), "A scene without outlines should outline nothing");

        auto *pickup = world.AddEntity(std::make_unique<scene::Entity>());
        auto *visual = Child(world, *pickup);
        auto *hiddenPart = Child(world, *pickup);
        hiddenPart->SetActive(false);
        auto *component = pickup->CreateComponent<scene::OutlineComponent>();
        component->SetColor({0.1f, 0.9f, 0.3f, 1.0f});
        component->SetWidth(2.0f);
        component->SetHiddenOpacity(0.25f);

        render::BuildSceneOutline(&world, 2160.0f, outline);
        Require(Contains(outline, pickup->GetID()) && Contains(outline, visual->GetID()),
                "An outline added after the first lookup should cover its entity and children");
        Require(!Contains(outline, hiddenPart->GetID()), "Inactive children should not be outlined");
        Require(outline.layers.size() == 1, "One outline should make one layer");
        Require(Near(outline.layers[0].style.width, 4.0f), "A 2160-line output should double the 1080-line width");
        Require(Near(outline.layers[0].style.color.g, 0.9f) && Near(outline.layers[0].style.hiddenOpacity, 0.25f),
                "The style should come from the outline");

        render::BuildSceneOutline(&world, 4320.0f, outline);
        Require(Near(outline.layers[0].style.width, scene::OutlineComponent::MaxWidth),
                "Scaled widths should stay within the pass's reach");

        component->SetIncludeChildren(false);
        render::BuildSceneOutline(&world, 1080.0f, outline);
        Require(outline.layers.size() == 1 && outline.layers[0].entities.size() == 1 && Contains(outline, pickup->GetID()),
                "Without children only the entity is outlined");

        // Each style draws as its own layer, in hierarchy order.
        auto *second = world.AddEntity(std::make_unique<scene::Entity>());
        auto *secondOutline = second->CreateComponent<scene::OutlineComponent>();
        secondOutline->SetColor({1.0f, 0.0f, 0.0f, 1.0f});
        render::BuildSceneOutline(&world, 1080.0f, outline);
        Require(outline.layers.size() == 2 && Contains(outline.layers[0], pickup->GetID()) &&
                    Contains(outline.layers[1], second->GetID()) && Near(outline.layers[0].style.color.r, 0.1f) &&
                    Near(outline.layers[1].style.color.r, 1.0f),
                "Outlines with different styles should keep their own styles");

        // The same style shares a layer.
        auto *third = world.AddEntity(std::make_unique<scene::Entity>());
        third->CreateComponent<scene::OutlineComponent>()->SetColor({1.0f, 0.0f, 0.0f, 1.0f});
        render::BuildSceneOutline(&world, 1080.0f, outline);
        Require(outline.layers.size() == 2 && Contains(outline.layers[1], third->GetID()),
                "Outlines with the same style should share a layer");

        // Past the layer budget, further outlines join the last layer.
        render::BuildSceneOutline(&world, 1080.0f, outline, 1);
        Require(outline.layers.size() == 1 && Contains(outline.layers[0], second->GetID()) &&
                    Near(outline.layers[0].style.color.r, 0.1f),
                "Outlines past the layer budget should join the last layer");
        render::BuildSceneOutline(&world, 1080.0f, outline, 0);
        Require(outline.layers.empty(), "No layer budget should outline nothing");
        third->RemoveComponent(third->GetComponent<scene::OutlineComponent>());

        component->SetEnabled(false);
        render::BuildSceneOutline(&world, 1080.0f, outline);
        Require(!Contains(outline, pickup->GetID()) && outline.layers.size() == 1 && Near(outline.layers[0].style.color.r, 1.0f),
                "A disabled outline should be skipped");

        second->SetActive(false);
        render::BuildSceneOutline(&world, 1080.0f, outline);
        Require(outline.layers.empty(), "An outline on an inactive entity should be skipped");

        second->RemoveComponent(secondOutline);
        component->SetEnabled(true);
        render::BuildSceneOutline(&world, 1080.0f, outline);
        Require(outline.layers.size() == 1 && outline.layers[0].entities.size() == 1 && Contains(outline, pickup->GetID()),
                "A removed outline should no longer be drawn");
    }
}

int main()
try
{
    TestComponentValues();
    TestSceneExtraction();
    std::cout << "Scene outline tests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
