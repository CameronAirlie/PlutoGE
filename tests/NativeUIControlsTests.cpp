#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/UISystem.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/UIDropdownComponent.h"
#include "PlutoGE/scene/components/UIInputFieldComponent.h"
#include "PlutoGE/scene/components/UILayoutComponents.h"
#include "PlutoGE/scene/components/UIScrollViewComponent.h"
#include "PlutoGE/scene/components/UIShapeComponent.h"
#include "PlutoGE/scene/components/UISliderComponent.h"
#include "PlutoGE/scene/components/UIToggleComponent.h"

#include <cmath>
#include <iostream>
#include <memory>
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

    // A scene with one pixel-scaled canvas and helpers to drive frames.
    struct Harness
    {
        Scene scene;
        Entity *canvas = nullptr;
        std::string clipboard;

        Harness()
        {
            canvas = Add("Canvas");
            auto *component = canvas->CreateComponent<CanvasComponent>();
            component->SetBackend(UIRenderBackend::Native);
            component->SetScaleMode(CanvasScaleMode::ConstantPixels);
            UI().SetClipboard([this] { return clipboard; }, [this](const std::string &text) { clipboard = text; });
        }

        UISystem &UI() { return scene.GetUISystem(); }

        Entity *Add(const char *name, Entity *parent = nullptr)
        {
            return scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = name}), parent);
        }

        // An element anchored at the bottom-left with the given rect.
        Entity *Element(const char *name, glm::vec2 position, glm::vec2 size, Entity *parent = nullptr, bool graphic = true)
        {
            auto *entity = Add(name, parent ? parent : canvas);
            auto *rect = entity->CreateComponent<RectTransformComponent>();
            rect->SetAnchorPreset(UIAnchorPreset::BottomLeft);
            rect->SetAnchoredPosition(position);
            rect->SetSizeDelta(size);
            if (graphic)
                entity->CreateComponent<UIShapeComponent>();
            return entity;
        }

        UIInputContext Frame(glm::vec2 pointer = {-1, -1}) const
        {
            return {.viewportSize = kViewport, .pointerPosition = pointer, .pointerInside = pointer.x >= 0.0f};
        }

        void Run(UIInputContext input) { UI().Update(scene, input, 1.0f / 60.0f); }

        void Click(glm::vec2 pointer)
        {
            auto down = Frame(pointer);
            down.pointerDown = down.pointerPressed = true;
            Run(down);
            auto up = Frame(pointer);
            up.pointerReleased = true;
            Run(up);
        }

        void Type(std::u32string text, std::vector<UIKeyPress> keys = {})
        {
            auto input = Frame();
            input.text = std::move(text);
            input.keys = std::move(keys);
            Run(input);
        }

        bool Emitted(UIEventType type, const Entity *entity) const
        {
            for (const auto &record : scene.GetUISystem().GetFrameEvents())
                if (record.type == type && record.entity == entity->GetID())
                    return true;
            return false;
        }
    };

    void TestToggles()
    {
        Harness h;
        auto *group = h.Element("Group", {0, 0}, {400, 100}, nullptr, false);
        group->CreateComponent<UIToggleGroupComponent>();
        auto *first = h.Element("First", {0, 0}, {50, 50}, group);
        auto *second = h.Element("Second", {100, 0}, {50, 50}, group);
        auto *check = h.Element("Check", {5, 5}, {10, 10}, first);
        auto *a = first->CreateComponent<UIToggleComponent>();
        auto *b = second->CreateComponent<UIToggleComponent>();
        a->SetGroup(group->GetID());
        b->SetGroup(group->GetID());
        a->SetGraphic(check->GetID());

        h.Run(h.Frame());
        Require(Near(check->GetComponent<RectTransformComponent>()->GetOpacity(), 0.0f), "Checkmark hidden while off");
        h.Click({25, 25});
        Require(a->IsOn() && h.Emitted(UIEventType::ValueChanged, first), "Clicking a toggle turns it on");
        h.Run(h.Frame());
        Require(Near(check->GetComponent<RectTransformComponent>()->GetOpacity(), 1.0f), "Checkmark shown while on");
        h.Click({125, 25});
        Require(b->IsOn() && !a->IsOn(), "A group keeps only one toggle on");
        h.Click({125, 25});
        Require(b->IsOn(), "A group without switch-off keeps its toggle on");
    }

    void TestSlider()
    {
        Harness h;
        auto *slider = h.Element("Slider", {100, 100}, {200, 20});
        auto *fill = h.Element("Fill", {0, 0}, {0, 0}, slider);
        auto *component = slider->CreateComponent<UISliderComponent>();
        component->SetRange(0, 10);
        component->SetFill(fill->GetID());

        h.Click({250, 110});
        Require(Near(component->GetValue(), 7.5f, 0.05f), "Pointer sets the value: " + std::to_string(component->GetValue()));
        Require(h.UI().GetFocusedEntity() == slider->GetID(), "Clicking a slider focuses it");

        component->SetWholeNumbers(true);
        auto right = h.Frame();
        right.navigation.move = {1, 0};
        h.Run(right);
        Require(Near(component->GetValue(), 9.0f), "Navigation steps a whole-number slider: " + std::to_string(component->GetValue()));
        Require(h.UI().GetFocusedEntity() == slider->GetID(), "Moving along the axis keeps focus on the slider");
        h.Run(h.Frame());
        Require(Near(fill->GetComponent<RectTransformComponent>()->GetAnchorMax().x, 0.9f), "Fill anchors follow the value");
    }

    void TestScrollView()
    {
        Harness h;
        auto *view = h.Element("View", {0, 0}, {200, 100});
        view->GetComponent<RectTransformComponent>()->SetClipChildren(true);
        auto *content = h.Element("Content", {0, 0}, {200, 400}, view);
        auto *rect = content->GetComponent<RectTransformComponent>();
        rect->SetAnchorPreset(UIAnchorPreset::TopLeft);
        rect->SetAnchoredPosition({0, 0});
        auto *scroll = view->CreateComponent<UIScrollViewComponent>();
        scroll->SetContent(content->GetID());
        scroll->SetMovement(UIScrollMovement::Clamped);

        h.Run(h.Frame());
        h.Run(h.Frame());
        Require(Near(scroll->GetMaxScroll().y, 300.0f), "Max scroll is content minus view");
        auto wheel = h.Frame({100, 50});
        wheel.scrollDelta = {0, -2};
        h.Run(wheel);
        Require(Near(scroll->GetScrollPosition().y, 60.0f), "Wheel scrolls down by the sensitivity");
        h.Run(h.Frame());
        Require(Near(rect->GetAnchoredPosition().y, 60.0f), "Content moves up as the view scrolls down");
        wheel.scrollDelta = {0, -100};
        h.Run(wheel);
        Require(Near(scroll->GetScrollPosition().y, 300.0f), "Clamped views stop at the end");
    }

    void TestInputField()
    {
        Harness h;
        auto *field = h.Element("Field", {100, 100}, {300, 40});
        field->CreateComponent<UITextComponent>();
        auto *input = field->CreateComponent<UIInputFieldComponent>();

        h.Click({120, 120});
        Require(input->IsEditing() && h.UI().IsKeyboardCaptured(), "Clicking an input field starts editing");
        h.Type(U"hello");
        Require(input->GetText() == "hello" && h.Emitted(UIEventType::ValueChanged, field), "Typing inserts text");
        h.Type({}, {{UIKey::Backspace}});
        Require(input->GetText() == "hell", "Backspace deletes before the caret");
        h.Type({}, {{UIKey::Left, true}, {UIKey::Left, true}});
        h.Type(U"LP");
        Require(input->GetText() == "heLP", "Typing replaces the selection: " + input->GetText());
        h.Type({}, {{UIKey::SelectAll}, {UIKey::Cut}});
        Require(input->GetText().empty() && h.clipboard == "heLP", "Cut moves the selection to the clipboard");
        h.Type({}, {{UIKey::Paste}, {UIKey::Paste}});
        Require(input->GetText() == "heLPheLP", "Paste inserts the clipboard");
        h.Run(h.Frame());
        Require(field->GetComponent<UITextComponent>()->GetText() == "heLPheLP", "The text component shows the value");

        auto cancel = h.Frame();
        cancel.navigation.cancel = true;
        h.Run(cancel);
        Require(!input->IsEditing() && input->GetText().empty(), "Cancel restores the text from before editing");

        input->SetContentType(UIInputContentType::Integer);
        input->SetCharacterLimit(4);
        h.Click({120, 120});
        h.Type(U"-12a.345");
        Require(input->GetText() == "-123", "Integer fields filter input and respect the limit: " + input->GetText());

        auto submit = h.Frame();
        submit.navigation.submit = true;
        h.Run(submit);
        Require(!input->IsEditing() && h.Emitted(UIEventType::EndEdit, field), "Submit ends single-line editing");
    }

    void TestDropdown()
    {
        Harness h;
        auto *dropdown = h.Element("Dropdown", {100, 400}, {200, 40});
        dropdown->CreateComponent<UITextComponent>();
        auto *component = dropdown->CreateComponent<UIDropdownComponent>();
        component->SetOptions({"Low", "Medium", "High"});

        h.Click({150, 420});
        Require(component->IsPopupOpen(), "Clicking the dropdown opens its list");
        // Items are 32 units tall starting 6 units below the control (2 gap + 4 padding).
        h.Click({150, 400 - 6 - 32 - 16});
        Require(!component->IsPopupOpen() && component->GetValue() == 1, "Clicking an item chooses it");
        Require(h.Emitted(UIEventType::ValueChanged, dropdown), "Choosing raises ValueChanged");
        h.Run(h.Frame());
        Require(dropdown->GetComponent<UITextComponent>()->GetText() == "Medium", "The caption shows the choice");

        h.Click({150, 420});
        auto down = h.Frame();
        down.navigation.move = {0, -1};
        h.Run(down);
        auto submit = h.Frame();
        submit.navigation.submit = true;
        h.Run(submit);
        Require(component->GetValue() == 2 && !component->IsPopupOpen(), "Navigation and submit choose from the list");

        h.Click({150, 420});
        h.Click({700, 50});
        Require(!component->IsPopupOpen(), "Pressing outside closes the list");
    }

    void TestNavigation()
    {
        Harness h;
        auto *top = h.Element("Top", {100, 300}, {100, 40});
        auto *bottom = h.Element("Bottom", {100, 200}, {100, 40});
        auto *right = h.Element("Right", {300, 300}, {100, 40});
        for (auto *entity : {top, bottom, right})
            entity->CreateComponent<UIButtonComponent>();

        auto move = h.Frame();
        move.navigation.move = {0, -1};
        h.Run(move);
        Require(h.UI().GetFocusedEntity() == top->GetID(), "The first navigation selects the top-left control");
        h.Run(move);
        Require(h.UI().GetFocusedEntity() == bottom->GetID(), "Down moves to the control below");
        move.navigation.move = {1, 0};
        h.Run(move);
        Require(h.UI().GetFocusedEntity() == right->GetID(), "Right moves to the nearest control on the right");

        auto submit = h.Frame();
        submit.navigation.submit = true;
        h.Run(submit);
        Require(right->GetComponent<UIButtonComponent>()->WasClicked(), "Submit clicks the focused button");
        Require(h.Emitted(UIEventType::Click, right), "Clicks are recorded for scripts");
        h.Run(h.Frame());
        Require(!right->GetComponent<UIButtonComponent>()->WasClicked(), "Click results last one frame");

        top->GetComponent<UIButtonComponent>()->SetNavigationMode(UINavigationMode::Explicit);
        top->GetComponent<UIButtonComponent>()->SetNeighbour(UISelectable::Right, bottom->GetID());
        h.UI().SetSelected(top->GetID());
        h.Run(move);
        Require(h.UI().GetFocusedEntity() == bottom->GetID(), "Explicit neighbours override spatial navigation");
    }

    void TestPointerCapture()
    {
        Harness h;
        h.Element("Label", {0, 0}, {100, 100});
        auto *button = h.Element("Button", {200, 0}, {100, 100});
        button->CreateComponent<UIButtonComponent>();
        h.Run(h.Frame({50, 50}));
        Require(!h.UI().IsPointerOverUI(), "Non-interactive graphics do not capture the pointer");
        h.Run(h.Frame({250, 50}));
        Require(h.UI().IsPointerOverUI(), "Interactive controls capture the pointer");
    }
}

int main()
{
    try
    {
        TestToggles();
        TestSlider();
        TestScrollView();
        TestInputField();
        TestDropdown();
        TestNavigation();
        TestPointerCapture();
    }
    catch (const std::exception &error)
    {
        std::cerr << "NativeUIControlsTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "NativeUIControlsTests passed\n";
    return 0;
}
