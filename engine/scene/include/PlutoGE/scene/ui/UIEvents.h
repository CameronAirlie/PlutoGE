#pragma once

#include "PlutoGE/ui/Geometry.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace PlutoGE::scene
{
    class Entity;
    struct UIResolvedElement;
    using UIEntityID = std::uint32_t;

    enum class UIEventType : std::uint8_t
    {
        PointerEnter,
        PointerExit,
        PointerDown,
        PointerUp,
        Click,
        BeginDrag,
        Drag,
        EndDrag,
        Drop,
        Scroll,
        Select,       // gained focus
        Deselect,     // lost focus
        Move,         // keyboard/gamepad navigation while focused
        Submit,       // Enter / gamepad A while focused
        Cancel,       // Escape / gamepad B
        ValueChanged, // emitted by controls; never dispatched to handlers
        EndEdit,      // input field finished editing; emitted only
        PointerMove,  // pointer moved over an open popup
    };

    struct UIEvent
    {
        UIEventType type = UIEventType::PointerEnter;
        UIEntityID target = 0;         // element the event started at
        UIEntityID current = 0;        // element whose handlers are running
        glm::vec2 position{0.0f};      // pointer, target pixels (y-up)
        glm::vec2 delta{0.0f};         // pointer movement since the last event
        glm::vec2 scroll{0.0f};        // wheel steps, y positive away from the user
        glm::ivec2 direction{0};       // Move: one of the four directions (y-up)
        int clickCount = 1;
        bool consumed = false;

        void Consume() { consumed = true; }
    };

    // What event handlers may ask of the UI system.
    class UIEventContext
    {
    public:
        virtual ~UIEventContext() = default;
        [[nodiscard]] virtual const UIResolvedElement *FindElement(UIEntityID entity) const = 0;
        [[nodiscard]] virtual Entity *FindEntity(UIEntityID entity) const = 0;
        // Pointer pixels to the element's layout units (inverse of its transform).
        [[nodiscard]] virtual glm::vec2 ToLocal(UIEntityID entity, glm::vec2 pixel) const = 0;
        virtual void SetSelected(UIEntityID entity) = 0;
        [[nodiscard]] virtual UIEntityID GetSelected() const = 0;
        // Records a semantic event (ValueChanged, Click, ...) for scripts.
        virtual void Emit(UIEventType type, UIEntityID source) = 0;
        [[nodiscard]] virtual float GetDeltaTime() const = 0;
        // Routes pointer and navigation input to the owner's IUIPopup until it closes.
        virtual void OpenPopup(UIEntityID owner) = 0;
        [[nodiscard]] virtual std::string GetClipboardText() const = 0;
        virtual void SetClipboardText(const std::string &text) = 0;
    };

    // Implemented by components that react to UI input. Events bubble from
    // the target to its ancestors until a handler consumes them.
    class IUIEventHandler
    {
    public:
        virtual ~IUIEventHandler() = default;
        virtual void OnUIEvent(UIEvent &event, UIEventContext &context) = 0;
    };

    enum class UIKey : std::uint8_t
    {
        Backspace,
        Delete,
        Left,
        Right,
        Up,
        Down,
        Home,
        End,
        Enter,
        SelectAll,
        Copy,
        Cut,
        Paste,
    };

    struct UIKeyPress
    {
        UIKey key = UIKey::Enter;
        bool shift = false;
        bool control = false;
    };

    // Implemented by components that edit text while focused. While
    // IsEditing() is true the focused element receives typed characters and
    // editing keys, and gameplay keyboard input is suppressed.
    class IUITextInput
    {
    public:
        virtual ~IUITextInput() = default;
        [[nodiscard]] virtual bool IsEditing() const = 0;
        virtual void OnTextInput(std::u32string_view text, std::span<const UIKeyPress> keys, UIEventContext &context) = 0;
    };

    // Implemented by components that position other elements (a slider's
    // handle, a scroll view's content). Runs before layout each frame, in
    // edit mode as well as at runtime.
    class IUIRectDriver
    {
    public:
        virtual ~IUIRectDriver() = default;
        virtual void DriveRects(UIEventContext &context) = 0;
    };

    // Runtime state that is not serialized (a scroll position, whether a field
    // is being edited) exposed by name, so scripting can reach it through the
    // same generic property bridge as serialized properties.
    class IUIRuntimeProperties
    {
    public:
        virtual ~IUIRuntimeProperties() = default;
        // Returns false when the name is not a runtime property.
        virtual bool GetRuntimeProperty(std::string_view name, std::string &value) const = 0;
        virtual bool SetRuntimeProperty(std::string_view name, std::string_view value) = 0;
    };

    // Implemented by components with per-frame runtime behaviour (inertia,
    // caret blink, transitions). Runs only while the scene is playing.
    class IUIUpdatable
    {
    public:
        virtual ~IUIUpdatable() = default;
        virtual void UpdateUI(UIEventContext &context, float deltaTime) = 0;
    };
}
