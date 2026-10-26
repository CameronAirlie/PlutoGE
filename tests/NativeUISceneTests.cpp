#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/TimelineBindings.h"
#include "PlutoGE/scene/UISystem.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/UILayoutComponents.h"
#include "PlutoGE/scene/components/UIShapeComponent.h"
#include "PlutoGE/scene/components/UIStyleComponents.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
    using namespace PlutoGE;
    using namespace PlutoGE::scene;

    const glm::vec2 kViewport{800.0f, 600.0f};

    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    bool Near(float a, float b, float epsilon = 0.01f) { return std::abs(a - b) <= epsilon; }

    std::string Describe(const RectTransformLayout &rect)
    {
        std::ostringstream text;
        text << "(" << rect.min.x << ", " << rect.min.y << ")-(" << rect.max.x << ", " << rect.max.y << ")";
        return text.str();
    }

    void RequireRect(const UIResolvedElement *element, glm::vec2 min, glm::vec2 max, const std::string &what)
    {
        Require(element != nullptr, what + ": element missing");
        const auto &rect = element->rect;
        if (!Near(rect.min.x, min.x) || !Near(rect.min.y, min.y) || !Near(rect.max.x, max.x) || !Near(rect.max.y, max.y))
            throw std::runtime_error(what + ": got " + Describe(rect));
    }

    Entity *Add(Scene &scene, const char *name, Entity *parent = nullptr)
    {
        return scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = name}), parent);
    }

    Entity *AddCanvas(Scene &scene, int sortingOrder = 0)
    {
        auto *entity = Add(scene, "Canvas");
        auto *canvas = entity->CreateComponent<CanvasComponent>();
        canvas->SetBackend(UIRenderBackend::Native);
        canvas->SetScaleMode(CanvasScaleMode::ConstantPixels);
        canvas->SetSortingOrder(sortingOrder);
        return entity;
    }

    RectTransformComponent *AddRect(Entity *entity, glm::vec2 size, UIAnchorPreset preset = UIAnchorPreset::MiddleCenter)
    {
        auto *rect = entity->CreateComponent<RectTransformComponent>();
        rect->SetAnchorPreset(preset);
        rect->SetSizeDelta(size);
        return rect;
    }

    void TestFlexPanel()
    {
        Scene scene;
        auto *canvas = AddCanvas(scene);
        auto *panel = Add(scene, "Panel", canvas);
        AddRect(panel, {0, 0}, UIAnchorPreset::Stretch);
        auto *layout = panel->CreateComponent<UILayoutComponent>();
        layout->SetDirection(ui::FlexDirection::Column);
        layout->SetGap({0, 10});
        layout->SetPadding(ui::Edges::All(10));
        panel->CreateComponent<UIShapeComponent>();

        auto *a = Add(scene, "A", panel);
        AddRect(a, {100, 30});
        auto *b = Add(scene, "B", panel);
        AddRect(b, {200, 0});
        b->CreateComponent<UILayoutItemComponent>()->SetHeightMode(ui::SizeMode::Content);
        b->CreateComponent<UITextComponent>()->SetText("Hello");

        auto &ui = scene.GetUISystem();
        ui.RebuildLayout(scene, kViewport);
        RequireRect(ui.FindElement(panel->GetID()), {0, 0}, {800, 600}, "Stretched panel");
        RequireRect(ui.FindElement(a->GetID()), {10, 560}, {110, 590}, "First column item");
        const auto *label = ui.FindElement(b->GetID());
        Require(label != nullptr, "Text element missing");
        const float height = label->rect.max.y - label->rect.min.y;
        Require(height > 10.0f && height < 60.0f, "Content-sized text height " + std::to_string(height));
        Require(Near(label->rect.max.y, 550.0f), "Text follows the first item and the gap: " + Describe(label->rect));

        const auto &canvases = ui.BuildDrawLists(scene, kViewport);
        Require(canvases.size() == 1 && !canvases[0].drawList.Empty(), "Canvas painted nothing");
        bool glyphs = false;
        for (const auto &vertex : canvases[0].drawList.GetVertices())
            glyphs |= vertex.params.z == static_cast<float>(ui::PrimitiveKind::Glyph);
        Require(glyphs, "Text was not painted (no default font?)");
    }

    void TestLegacyLayoutGroup()
    {
        Scene scene;
        auto *canvas = AddCanvas(scene);
        auto *row = Add(scene, "Row", canvas);
        auto *rect = AddRect(row, {300, 50}, UIAnchorPreset::BottomLeft);
        rect->SetLayoutMode(UILayoutMode::Horizontal);
        rect->SetLayoutSpacing({10, 0});
        rect->SetControlChildHeight(true);
        auto *first = Add(scene, "First", row);
        AddRect(first, {100, 20});
        auto *second = Add(scene, "Second", row);
        AddRect(second, {50, 20});

        auto &ui = scene.GetUISystem();
        ui.RebuildLayout(scene, kViewport);
        RequireRect(ui.FindElement(first->GetID()), {0, 0}, {100, 50}, "Legacy horizontal first child");
        RequireRect(ui.FindElement(second->GetID()), {110, 0}, {160, 50}, "Legacy horizontal second child");
    }

    void TestTransformsAndClipping()
    {
        Scene scene;
        auto *canvas = AddCanvas(scene);
        auto *rotated = Add(scene, "Rotated", canvas);
        AddRect(rotated, {200, 100})->SetRotation(90.0f);
        auto *clipper = Add(scene, "Clipper", canvas);
        auto *clipRect = AddRect(clipper, {100, 100}, UIAnchorPreset::BottomLeft);
        clipRect->SetClipChildren(true);
        auto *outside = Add(scene, "Outside", clipper);
        AddRect(outside, {20, 20}, UIAnchorPreset::BottomLeft)->SetAnchoredPosition({500, 500});

        auto &ui = scene.GetUISystem();
        ui.RebuildLayout(scene, kViewport);
        RequireRect(ui.FindElement(rotated->GetID()), {350, 200}, {450, 400}, "Rotation swaps the bounds");
        Require(ui.FindElement(outside->GetID())->clipped, "Child outside a clipping parent must be clipped");
    }

    void TestCanvasOrderAndButtons()
    {
        Scene scene;
        auto *top = AddCanvas(scene, 5);
        auto *bottom = AddCanvas(scene, -1);

        auto *button = Add(scene, "Button", top);
        AddRect(button, {200, 100});
        button->CreateComponent<UIButtonComponent>();
        button->CreateComponent<UIImageComponent>();
        auto *label = Add(scene, "Label", button);
        AddRect(label, {0, 0}, UIAnchorPreset::Stretch);
        label->CreateComponent<UITextComponent>()->SetText("Play");

        auto *blocker = Add(scene, "Blocker", bottom);
        AddRect(blocker, {0, 0}, UIAnchorPreset::Stretch);
        blocker->CreateComponent<UIShapeComponent>();

        auto &ui = scene.GetUISystem();
        ui.RebuildLayout(scene, kViewport);
        Require(ui.GetCanvases().size() == 2 && ui.GetCanvases()[0].sortingOrder == -1,
                "Canvases must be ordered by sorting order");
        Require(ui.FindElement(label->GetID())->paintOrder > ui.FindElement(blocker->GetID())->paintOrder,
                "Higher sorting order paints later");

        const glm::vec2 centre = kViewport * 0.5f;
        const auto *hit = ui.HitTest(centre);
        Require(hit && hit->entity == label, "The label is the topmost element under the pointer");

        UIInputContext input{.viewportSize = kViewport, .pointerPosition = centre, .pointerInside = true};
        input.pointerDown = input.pointerPressed = true;
        ui.Update(scene, input, 0.016f);
        auto *buttonComponent = button->GetComponent<UIButtonComponent>();
        Require(buttonComponent->IsHovered() && buttonComponent->WasPressed(), "Pressing the label presses its button");
        input.pointerDown = input.pointerPressed = false;
        input.pointerReleased = true;
        ui.Update(scene, input, 0.016f);
        Require(buttonComponent->WasClicked(), "Releasing over the label clicks its button");

        input.pointerReleased = false;
        input.pointerPosition = {5, 5};
        ui.Update(scene, input, 0.016f);
        Require(!buttonComponent->IsHovered(), "Pointer elsewhere must not hover the button");
    }

    void TestPropertyAnimation()
    {
        Scene scene;
        auto *canvas = AddCanvas(scene);
        auto *panel = Add(scene, "Panel", canvas);
        AddRect(panel, {100, 100});
        auto *shape = panel->CreateComponent<UIShapeComponent>();
        shape->SetColor({1, 0, 0, 1});

        Timeline timeline;
        timeline.tracks.push_back({panel->GetID(), TimelineChannel::Property, TimelineInterpolation::Linear,
                                   {{0, {1, 0, 0, 1}, {}}, {1, {0, 0, 1, 0}, {}}}, "UIShapeComponent.Color"});
        Require(timeline.Validate(), "Property track should validate");
        const std::string encoded = timeline.Encode();
        Require(encoded.rfind("PLUTO_TIMELINE 2", 0) == 0, "Property tracks need format version 2");
        Timeline decoded;
        Require(Timeline::Decode(encoded, decoded) && decoded.tracks[0].property == "UIShapeComponent.Color" &&
                    decoded.tracks[0].keys[1].value[2] == 1.0f,
                "Property track did not round-trip");

        Timeline classic;
        classic.tracks.push_back({panel->GetID(), TimelineChannel::Position, TimelineInterpolation::Linear, {{0, {1, 2, 3}, {}}}});
        Require(classic.Encode().rfind("PLUTO_TIMELINE 1", 0) == 0, "Timelines without new features keep format version 1");

        TimelinePlayer player;
        Require(player.Start(timeline) && player.Seek(0.5), "Player failed to start");
        {
            ScopedTimelinePose pose(scene, player);
            const glm::vec4 color = shape->GetColor();
            Require(Near(color.r, 0.5f) && Near(color.b, 0.5f) && Near(color.a, 0.5f), "Colour was not interpolated");
        }
        Require(shape->GetColor() == glm::vec4(1, 0, 0, 1), "The pose scope must restore the authored colour");

        Require(GetTimelinePropertyWidth(scene, timeline.tracks[0]) == 4, "Colour properties have four components");
        auto missing = timeline.tracks[0];
        missing.property = "UIShapeComponent.NoSuchProperty";
        Require(GetTimelinePropertyWidth(scene, missing) == 0, "Unknown properties must not bind");
    }

    void TestStyleSheets()
    {
        std::vector<std::string> errors;
        const auto parsed = UIStyleSheet::Parse(R"(
            # theme tokens
            @accent = 0.25,0.5,1,1
            .button { UIShapeComponent.Color = @accent; UIShapeComponent.BorderWidth = 2 }
            .button:hover { UIShapeComponent.Color = 1,1,1,1 }
            .button.danger { UIShapeComponent.Color = 1,0,0,1 }
            .bogus:unknown { }
            stray = 1
        )", &errors);
        Require(parsed.GetRules().size() == 4, "Expected four rules, got " + std::to_string(parsed.GetRules().size()));
        Require(errors.size() == 2, "Unknown states and stray declarations must be reported");
        Require(errors[0].rfind("Line 7", 0) == 0, "Errors must name the source line: " + errors[0]);
        Require(parsed.GetRules()[0].declarations[0].value == "0.25,0.5,1,1", "Tokens must be substituted");

        Scene scene;
        auto *canvas = AddCanvas(scene);
        auto *sheet = canvas->CreateComponent<UIStyleSheetComponent>();
        sheet->SetRules(".button { UIShapeComponent.Color = 0,1,0,1 } .button:hover { UIShapeComponent.Color = 1,1,0,1 }");
        auto *button = Add(scene, "Button", canvas);
        AddRect(button, {200, 100});
        auto *shape = button->CreateComponent<UIShapeComponent>();
        button->CreateComponent<UIButtonComponent>();
        button->CreateComponent<UIStyleComponent>()->SetClasses("button");

        auto &ui = scene.GetUISystem();
        ui.Update(scene, {.viewportSize = kViewport}, 0.016f);
        Require(shape->GetColor() == glm::vec4(0, 1, 0, 1), "Base rule must apply");
        UIInputContext hover{.viewportSize = kViewport, .pointerPosition = kViewport * 0.5f, .pointerInside = true};
        ui.Update(scene, hover, 0.016f);
        ui.Update(scene, hover, 0.016f); // styles read the state the previous update set
        Require(shape->GetColor() == glm::vec4(1, 1, 0, 1), "Hover rule must apply while hovered");
        ui.Update(scene, {.viewportSize = kViewport}, 0.016f);
        ui.Update(scene, {.viewportSize = kViewport}, 0.016f);
        Require(shape->GetColor() == glm::vec4(0, 1, 0, 1), "Leaving hover must restore the base style");

        sheet->SetRules(".button { UIShapeComponent.Color = 0,0,1,1 }");
        ui.Update(scene, {.viewportSize = kViewport}, 0.016f);
        Require(shape->GetColor() == glm::vec4(0, 0, 1, 1), "Changing the sheet re-themes the UI");
    }

    void TestComponentRoundTrips()
    {
        UILayoutComponent layout;
        layout.SetMode(ui::LayoutMode::Grid);
        layout.SetColumns({ui::GridTrack::Fixed(120), ui::GridTrack::Auto(), ui::GridTrack::Fraction(2)});
        layout.SetPadding({1, 2, 3, 4});
        layout.SetJustify(ui::JustifyContent::SpaceEvenly);
        UILayoutComponent restoredLayout;
        restoredLayout.Deserialize(layout.Serialize());
        Require(restoredLayout.GetColumns() == layout.GetColumns(), "Grid tracks did not round-trip");
        Require(restoredLayout.GetPadding() == layout.GetPadding() && restoredLayout.GetJustify() == layout.GetJustify(),
                "Layout settings did not round-trip");
        Require(ParseGridTracks("1fr  100px,auto fr") ==
                    std::vector<ui::GridTrack>{ui::GridTrack::Fraction(1), ui::GridTrack::Fixed(100), ui::GridTrack::Auto(),
                                               ui::GridTrack::Fraction(1)},
                "Track parsing failed");

        UIShapeComponent shape;
        shape.SetFill(ui::Paint::Linear({1, 0, 0, 1}, {0, 0, 1, 1}, 45));
        shape.SetCornerRadii({1, 2, 3, 4});
        shape.SetShadowEnabled(true);
        UIShapeComponent restoredShape;
        restoredShape.Deserialize(shape.Serialize());
        Require(restoredShape.GetFill() == shape.GetFill() && restoredShape.GetCornerRadii() == shape.GetCornerRadii() &&
                    restoredShape.HasShadow(),
                "Shape did not round-trip");

        UILayoutComponent hostile;
        hostile.Deserialize({{"Mode", PropertyType::Enum, "not a number"}, {"Gap", PropertyType::Vec2, "1,"}});
        Require(hostile.GetMode() == ui::LayoutMode::Flex && hostile.GetGap() == glm::vec2(0.0f),
                "Malformed values must keep defaults");
    }
}

int main()
{
    try
    {
        TestFlexPanel();
        TestLegacyLayoutGroup();
        TestTransformsAndClipping();
        TestCanvasOrderAndButtons();
        TestComponentRoundTrips();
        TestPropertyAnimation();
        TestStyleSheets();
    }
    catch (const std::exception &error)
    {
        std::cerr << "NativeUISceneTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "NativeUISceneTests passed\n";
    return 0;
}
