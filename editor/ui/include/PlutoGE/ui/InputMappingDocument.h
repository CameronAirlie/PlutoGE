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
    // Index of name in table, or -1 for a value this editor does not list (kept verbatim).
    int FindInputName(std::span<const InputNamedCode> table, std::string_view name);

    struct InputMappingBinding
    {
        InputBindingKind kind = InputBindingKind::Key;
        std::string value = "Space"; // Key/Button/Axis/MouseButton/MouseAxis name for the kind.
        int gamepad = 0;
        float scale = 1.0f;
        float deadZone = 0.0f;
        assets::JsonValue source = assets::JsonValue::MakeObject(); // Unknown fields survive saving.

        // "Key:M", "GamepadButton:RightStick@0" - identifies the physical input.
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
