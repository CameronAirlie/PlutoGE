#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/UISystem.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/UIDragDropComponents.h"
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
#include <optional>
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

        // Presses at `from`, moves to `to` in steps (past the drag threshold), releases at `to`.
        // Returns the events of every frame, in order.
        std::vector<UIEventRecord> Drag(glm::vec2 from, glm::vec2 to, int steps = 3)
        {
            std::vector<UIEventRecord> events;
            const auto collect = [&]
            {
                const auto &frame = UI().GetFrameEvents();
                events.insert(events.end(), frame.begin(), frame.end());
            };
            auto down = Frame(from);
            down.pointerDown = down.pointerPressed = true;
            Run(down);
            collect();
            glm::vec2 previous = from;
            for (int step = 1; step <= steps; ++step)
            {
                const glm::vec2 at = glm::mix(from, to, static_cast<float>(step) / static_cast<float>(steps));
                auto move = Frame(at);
                move.pointerDown = true;
                move.pointerDelta = at - previous;
                previous = at;
                Run(move);
                collect();
            }
            auto up = Frame(to);
            up.pointerReleased = true;
            Run(up);
            collect();
            return events;
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

    // A menu shows a hidden panel and focuses its button in one frame (scripts run after the UI update): the button is
    // selected, and takes the Selected look, once the next update collects it.
    void TestFocusWhileHidden()
    {
        Harness h;
        auto *panel = h.Element("Panel", {0, 0}, {400, 400}, nullptr, false);
        auto *button = h.Element("Button", {100, 100}, {100, 40}, panel);
        button->CreateComponent<UIButtonComponent>();
        panel->SetActive(false);
        h.Run(h.Frame());
        panel->SetActive(true);
        h.UI().SetSelected(button->GetID());
        Require(!button->GetComponent<UIButtonComponent>()->IsSelected(), "Not collected yet, so not selected yet");
        h.Run(h.Frame());
        Require(h.UI().GetFocusedEntity() == button->GetID(), "Focus set before collection is kept");
        Require(button->GetComponent<UIButtonComponent>()->IsSelected(), "The button is selected once collected");
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

    std::optional<UIEventRecord> Find(const std::vector<UIEventRecord> &events, UIEventType type, const Entity *entity)
    {
        for (const auto &record : events)
            if (record.type == type && record.entity == entity->GetID())
                return record;
        return std::nullopt;
    }

    std::ptrdiff_t IndexOf(const std::vector<UIEventRecord> &events, UIEventType type)
    {
        for (std::size_t index = 0; index < events.size(); ++index)
            if (events[index].type == type)
                return static_cast<std::ptrdiff_t>(index);
        return -1;
    }

    bool NearVec(glm::vec2 a, glm::vec2 b, float epsilon = 0.01f) { return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon); }

    std::string Text(glm::vec2 value) { return "(" + std::to_string(value.x) + ", " + std::to_string(value.y) + ")"; }

    void TestDragAndDrop()
    {
        Harness h;
        // Canvas units are 2 pixels: an 800x600 target against a 400x300 reference.
        auto *canvas = h.canvas->GetComponent<CanvasComponent>();
        canvas->SetScaleMode(CanvasScaleMode::ScaleWithScreenSize);
        canvas->SetReferenceResolution({400.0f, 300.0f});
        auto *source = h.Element("Source", {10, 10}, {50, 50});        // pixels (20,20)-(120,120)
        auto *target = h.Element("Target", {200, 100}, {50, 50});      // pixels (400,200)-(500,300)
        source->CreateComponent<UIDragSourceComponent>();
        target->CreateComponent<UIDropTargetComponent>();
        h.Run(h.Frame());
        Require(Near(h.UI().GetCanvases().front().scale, 2.0f), "The test canvas scales by 2");

        const auto events = h.Drag({70, 70}, {450, 250});
        const auto begin = Find(events, UIEventType::BeginDrag, source);
        const auto drag = Find(events, UIEventType::Drag, source);
        const auto drop = Find(events, UIEventType::Drop, target);
        const auto end = Find(events, UIEventType::EndDrag, source);
        Require(begin && drag && drop && end, "A drag onto a target records BeginDrag, Drag, Drop and EndDrag");
        // BeginDrag fires on the first move, at pixel (70,70) + (380,180)/3.
        const glm::vec2 firstMove = glm::vec2(70, 70) + glm::vec2(380, 180) / 3.0f;
        Require(NearVec(begin->position, firstMove * 0.5f), "BeginDrag is in canvas units: " + Text(begin->position));
        Require(NearVec(drop->position, {225, 125}) && NearVec(end->position, {225, 125}),
                "Drop and EndDrag carry the release position in canvas units: " + Text(drop->position));
        Require(drop->related == source->GetID(), "Drop's related element is the drag source");
        Require(end->related == target->GetID(), "EndDrag's related element is the accepting target");
        Require(begin->related == 0 && drag->related == 0, "Begin/Drag have no related element");
        Require(IndexOf(events, UIEventType::Drop) < IndexOf(events, UIEventType::EndDrag), "Drop is recorded before EndDrag");
        std::size_t drags = 0;
        for (const auto &record : events)
            drags += record.type == UIEventType::Drag ? 1 : 0;
        Require(drags == 3, "Every pointer move while dragging records Drag: " + std::to_string(drags));

        // The contract: a bottom-left anchored element under a full-canvas parent, placed at the
        // recorded position, has its pivot under the pointer.
        auto *overlay = h.Element("Overlay", {0, 0}, {0, 0}, nullptr, false);
        auto *overlayRect = overlay->GetComponent<RectTransformComponent>();
        overlayRect->SetAnchorMin({0, 0});
        overlayRect->SetAnchorMax({1, 1});
        auto *ghost = h.Element("Ghost", drop->position, {20, 20}, overlay, false);
        ghost->GetComponent<RectTransformComponent>()->SetPivot({0.5f, 0.5f});
        h.Run(h.Frame());
        const auto *placed = h.UI().FindElement(ghost->GetID());
        Require(placed, "The ghost is laid out");
        const glm::vec2 centre = (placed->rect.min + placed->rect.max) * 0.5f;
        Require(NearVec(centre, {450, 250}), "A ghost placed at the position sits under the pointer: " + Text(centre));

        // Released over nothing: EndDrag with no related element, and no Drop.
        const auto missed = h.Drag({70, 70}, {700, 500});
        const auto missedEnd = Find(missed, UIEventType::EndDrag, source);
        Require(missedEnd && missedEnd->related == 0, "A drag released outside any target ends with related 0");
        Require(IndexOf(missed, UIEventType::Drop) < 0, "No Drop is recorded without a target");
        Require(NearVec(missedEnd->position, {350, 250}), "EndDrag carries the release position: " + Text(missedEnd->position));

        // A non-interactable target refuses the drop.
        target->GetComponent<UIDropTargetComponent>()->SetInteractable(false);
        const auto refused = h.Drag({70, 70}, {450, 250});
        const auto refusedEnd = Find(refused, UIEventType::EndDrag, source);
        Require(IndexOf(refused, UIEventType::Drop) < 0 && refusedEnd && refusedEnd->related == 0,
                "A non-interactable drop target does not accept drops");
        target->GetComponent<UIDropTargetComponent>()->SetInteractable(true);

        // A graphic that is not a raycast target, lying over the drop target, does not block it.
        auto *cover = h.Element("Cover", {190, 90}, {80, 80});
        cover->GetComponent<RectTransformComponent>()->SetRaycastTarget(false);
        const auto covered = h.Drag({70, 70}, {450, 250});
        const auto coveredDrop = Find(covered, UIEventType::Drop, target);
        Require(coveredDrop && coveredDrop->related == source->GetID(), "A non-raycast element does not block the drop");

        // A non-interactable source does not start drags.
        source->GetComponent<UIDragSourceComponent>()->SetInteractable(false);
        const auto still = h.Drag({70, 70}, {450, 250});
        Require(IndexOf(still, UIEventType::BeginDrag) < 0 && IndexOf(still, UIEventType::EndDrag) < 0,
                "A non-interactable source records no drag");
    }

    void TestDragSourceButton()
    {
        Harness h;
        auto *tile = h.Element("Tile", {0, 0}, {100, 100});
        auto *button = tile->CreateComponent<UIButtonComponent>();
        tile->CreateComponent<UIDragSourceComponent>();

        auto down = h.Frame({50, 50});
        down.pointerDown = down.pointerPressed = true;
        h.Run(down);
        auto nudge = h.Frame({53, 52}); // within the drag threshold
        nudge.pointerDown = true;
        nudge.pointerDelta = {3, 2};
        h.Run(nudge);
        Require(!h.Emitted(UIEventType::BeginDrag, tile), "A press within the threshold does not drag");
        auto up = h.Frame({53, 52});
        up.pointerReleased = true;
        h.Run(up);
        Require(button->WasClicked() && h.Emitted(UIEventType::Click, tile), "A short press on a draggable button clicks");
        Require(!h.Emitted(UIEventType::EndDrag, tile), "A click ends no drag");

        const auto events = h.Drag({50, 50}, {300, 300});
        Require(Find(events, UIEventType::BeginDrag, tile) && Find(events, UIEventType::EndDrag, tile),
                "The button's element drags");
        Require(IndexOf(events, UIEventType::Click) < 0 && !button->WasClicked(), "A drag does not click");
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
        TestFocusWhileHidden();
        TestPointerCapture();
        TestDragAndDrop();
        TestDragSourceButton();
    }
    catch (const std::exception &error)
    {
        std::cerr << "NativeUIControlsTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "NativeUIControlsTests passed\n";
    return 0;
}
