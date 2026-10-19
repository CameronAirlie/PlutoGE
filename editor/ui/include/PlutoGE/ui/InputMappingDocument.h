#pragma once

#include "PlutoGE/assets/Json.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::ui
{
    // Binding kinds and value names match PlutoGE.ScriptCore.InputActionMap's JSON.
    enum class InputBindingKind { Key, GamepadButton, GamepadAxis, MouseButton, MouseAxis };
    // Matches PlutoGE.ScriptCore.InputTriggerKind; Press is the default and is not written.
    enum class InputTriggerKind { Press, Tap, Hold, MultiTap };

    struct InputNamedCode
    {
        const char *name;
        int code; // GLFW key code, gamepad button/axis index, or mouse button/axis index.
    };

    std::span<const InputNamedCode> InputKeyNames();
    std::span<const InputNamedCode> InputGamepadButtonNames();
    std::span<const InputNamedCode> InputGamepadAxisNames();
    std::span<const InputNamedCode> InputMouseButtonNames();
    std::span<const InputNamedCode> InputMouseAxisNames();
    std::span<const InputNamedCode> InputNamesFor(InputBindingKind kind);
    const char *InputKindName(InputBindingKind kind);
    const char *InputTriggerKindName(InputTriggerKind kind);
    inline constexpr int InputTriggerKindCount = 4;
    // Index of name in table, or -1 for a value this editor does not list (kept verbatim).
    int FindInputName(std::span<const InputNamedCode> table, std::string_view name);

    // How a binding's control becomes action activity ("Trigger" in the JSON). Only the fields of the
    // current kind are written; defaults match PlutoGE.ScriptCore.InputTrigger.
    struct InputMappingTrigger
    {
        InputTriggerKind kind = InputTriggerKind::Press;
        float duration = 0.2f; // Tap: longest hold that still taps. Hold: time to hold.
        int count = 2;         // MultiTap: presses required.
        float window = 0.25f;  // MultiTap: longest gap between presses.
        bool exclusive = false; // MultiTap: fire after the window only if no further press followed.
        assets::JsonValue source = assets::JsonValue::MakeObject(); // Unknown fields survive saving.

        bool IsDefault() const { return kind == InputTriggerKind::Press; }
        // "", "tap", "hold 0.2 s", "double tap", "single tap (exclusive)"...
        std::string Description() const;
    };

    struct InputMappingBinding
    {
        InputBindingKind kind = InputBindingKind::Key;
        std::string value = "Space"; // Key/Button/Axis/MouseButton/MouseAxis name for the kind.
        int gamepad = 0;
        float scale = 1.0f;
        float deadZone = 0.0f;
        InputMappingTrigger trigger;
        assets::JsonValue source = assets::JsonValue::MakeObject(); // Unknown fields survive saving.

        // "Key:M", "GamepadButton:RightStick@0" - identifies the physical input.
        // Label() includes the trigger: "Pad 0 B (double tap)".
        std::string InputId() const;
        std::string Label() const;
    };

    struct InputMappingAction
    {
        std::string name;
        std::vector<InputMappingBinding> bindings;
        assets::JsonValue source = assets::JsonValue::MakeObject();
    };

    // Physical input bound by more than one action. Often intentional (a menu action sharing a
    // gameplay button), so it is reported for review rather than rejected.
    struct InputMappingSharedInput
    {
        std::string inputId;
        std::string label;
        std::vector<std::string> actions;
        // Every binding on the input uses a different trigger (tap vs hold, single vs double tap):
        // the sharing is designed, not a conflict.
        bool disambiguatedByTriggers = false;
    };

    class InputMappingDocument
    {
    public:
        bool Parse(std::string_view json, std::string *error = nullptr);
        std::string Serialize() const;

        std::vector<InputMappingAction> &Actions() { return m_actions; }
        const std::vector<InputMappingAction> &Actions() const { return m_actions; }

        std::vector<InputMappingSharedInput> FindSharedInputs() const;
        // Gamepad buttons (pad 0) and listed keys that no action uses: candidates for new actions.
        std::vector<std::string> FindUnusedGamepadButtons() const;
        std::vector<std::string> FindUnusedKeys() const;
        // Action names that are empty or repeated; the runtime resolves actions by name.
        std::vector<std::string> FindInvalidActionNames() const;

    private:
        std::vector<InputMappingAction> m_actions;
        assets::JsonValue m_root = assets::JsonValue::MakeObject();
    };
}
