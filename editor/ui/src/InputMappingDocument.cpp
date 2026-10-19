#include "PlutoGE/ui/InputMappingDocument.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace PlutoGE::ui
{
    namespace
    {
        // Mirrors PlutoGE.ScriptCore.KeyCode (GLFW codes).
        constexpr InputNamedCode Keys[] = {
            {"Space", 32}, {"Apostrophe", 39}, {"Comma", 44}, {"Minus", 45}, {"Period", 46}, {"Slash", 47},
            {"D0", 48}, {"D1", 49}, {"D2", 50}, {"D3", 51}, {"D4", 52}, {"D5", 53}, {"D6", 54}, {"D7", 55}, {"D8", 56}, {"D9", 57},
            {"Semicolon", 59}, {"Equal", 61},
            {"A", 65}, {"B", 66}, {"C", 67}, {"D", 68}, {"E", 69}, {"F", 70}, {"G", 71}, {"H", 72}, {"I", 73}, {"J", 74},
            {"K", 75}, {"L", 76}, {"M", 77}, {"N", 78}, {"O", 79}, {"P", 80}, {"Q", 81}, {"R", 82}, {"S", 83}, {"T", 84},
            {"U", 85}, {"V", 86}, {"W", 87}, {"X", 88}, {"Y", 89}, {"Z", 90},
            {"LeftBracket", 91}, {"Backslash", 92}, {"RightBracket", 93}, {"GraveAccent", 96},
            {"Escape", 256}, {"Enter", 257}, {"Tab", 258}, {"Backspace", 259}, {"Insert", 260}, {"Delete", 261},
            {"Right", 262}, {"Left", 263}, {"Down", 264}, {"Up", 265}, {"PageUp", 266}, {"PageDown", 267}, {"Home", 268}, {"End", 269},
            {"CapsLock", 280}, {"ScrollLock", 281}, {"NumLock", 282}, {"PrintScreen", 283}, {"Pause", 284},
            {"F1", 290}, {"F2", 291}, {"F3", 292}, {"F4", 293}, {"F5", 294}, {"F6", 295}, {"F7", 296}, {"F8", 297},
            {"F9", 298}, {"F10", 299}, {"F11", 300}, {"F12", 301},
            {"LeftShift", 340}, {"LeftControl", 341}, {"LeftAlt", 342}, {"RightShift", 344}, {"RightControl", 345}, {"RightAlt", 346},
        };
        constexpr InputNamedCode Buttons[] = {
            {"A", 0}, {"B", 1}, {"X", 2}, {"Y", 3}, {"LeftBumper", 4}, {"RightBumper", 5}, {"Back", 6}, {"Start", 7},
            {"Guide", 8}, {"LeftStick", 9}, {"RightStick", 10}, {"DpadUp", 11}, {"DpadRight", 12}, {"DpadDown", 13}, {"DpadLeft", 14},
        };
        constexpr InputNamedCode Axes[] = {
            {"LeftX", 0}, {"LeftY", 1}, {"RightX", 2}, {"RightY", 3}, {"LeftTrigger", 4}, {"RightTrigger", 5},
        };
        constexpr InputNamedCode MouseButtons[] = {
            {"Left", 0}, {"Right", 1}, {"Middle", 2}, {"Button4", 3}, {"Button5", 4}, {"Button6", 5}, {"Button7", 6}, {"Button8", 7},
        };
        constexpr InputNamedCode MouseAxes[] = {{"X", 0}, {"Y", 1}};
        constexpr const char *KindNames[] = {"Key", "GamepadButton", "GamepadAxis", "MouseButton", "MouseAxis"};
        constexpr const char *ValueFields[] = {"Key", "Button", "Axis", "MouseButton", "MouseAxis"};
        constexpr const char *TriggerKindNames[] = {"Press", "Tap", "Hold", "MultiTap"};
        static_assert(std::size(TriggerKindNames) == InputTriggerKindCount);

        double Rounded(float value) { return std::round(static_cast<double>(value) * 10000.0) / 10000.0; }
        bool UsesGamepad(InputBindingKind kind) { return kind == InputBindingKind::GamepadButton || kind == InputBindingKind::GamepadAxis; }

        InputMappingTrigger ParseTrigger(const assets::JsonValue &binding)
        {
            InputMappingTrigger trigger;
            const auto *value = binding.Find("Trigger");
            if (!value || !value->IsObject()) return trigger;
            trigger.source = *value;
            const auto kind = value->GetString("Kind", "Press");
            const auto found = std::find(std::begin(TriggerKindNames), std::end(TriggerKindNames), kind);
            trigger.kind = found == std::end(TriggerKindNames) ? InputTriggerKind::Press
                                                               : static_cast<InputTriggerKind>(found - std::begin(TriggerKindNames));
            trigger.duration = static_cast<float>(value->GetNumber("Duration", trigger.duration));
            trigger.count = static_cast<int>(value->GetNumber("Count", trigger.count));
            trigger.window = static_cast<float>(value->GetNumber("Window", trigger.window));
            trigger.exclusive = value->GetBool("Exclusive", false);
            return trigger;
        }

        // Writes only the current kind's fields; a plain press drops the object so mappings stay minimal.
        void WriteTrigger(assets::JsonValue &binding, const InputMappingTrigger &trigger)
        {
            if (trigger.IsDefault())
            {
                binding.Remove("Trigger");
                return;
            }
            auto value = trigger.source.IsObject() ? trigger.source : assets::JsonValue::MakeObject();
            value.Set("Kind", assets::JsonValue::MakeString(InputTriggerKindName(trigger.kind)));
            // Set updates fields in place, so an unedited trigger saves unchanged; fields another kind
            // reads are dropped, like stale value fields after a binding kind change.
            if (trigger.kind == InputTriggerKind::Tap || trigger.kind == InputTriggerKind::Hold)
                value.Set("Duration", assets::JsonValue::MakeNumber(Rounded(trigger.duration)));
            else
                value.Remove("Duration");
            if (trigger.kind == InputTriggerKind::MultiTap)
            {
                value.Set("Count", assets::JsonValue::MakeNumber(std::max(1, trigger.count)));
                value.Set("Window", assets::JsonValue::MakeNumber(Rounded(trigger.window)));
                if (trigger.exclusive || value.Find("Exclusive"))
                    value.Set("Exclusive", assets::JsonValue::MakeBool(trigger.exclusive));
            }
            else
                for (const char *field : {"Count", "Window", "Exclusive"}) value.Remove(field);
            binding.Set("Trigger", std::move(value));
        }
    }

    const char *InputTriggerKindName(InputTriggerKind kind) { return TriggerKindNames[static_cast<int>(kind)]; }

    std::string InputMappingTrigger::Description() const
    {
        char seconds[32];
        switch (kind)
        {
        case InputTriggerKind::Press: return {};
        case InputTriggerKind::Tap: return "tap";
        case InputTriggerKind::Hold:
            std::snprintf(seconds, sizeof(seconds), "hold %.2g s", duration);
            return seconds;
        case InputTriggerKind::MultiTap:
        {
            const int presses = std::max(1, count);
            std::string text = presses == 1 ? "single tap" : presses == 2 ? "double tap" : presses == 3 ? "triple tap"
                                                                                                      : std::to_string(presses) + "x tap";
            return exclusive ? text + " (exclusive)" : text;
        }
        }
        return {};
    }

    std::span<const InputNamedCode> InputKeyNames() { return Keys; }
    std::span<const InputNamedCode> InputGamepadButtonNames() { return Buttons; }
    std::span<const InputNamedCode> InputGamepadAxisNames() { return Axes; }
    std::span<const InputNamedCode> InputMouseButtonNames() { return MouseButtons; }
    std::span<const InputNamedCode> InputMouseAxisNames() { return MouseAxes; }

    std::span<const InputNamedCode> InputNamesFor(InputBindingKind kind)
    {
        switch (kind)
        {
        case InputBindingKind::Key: return Keys;
        case InputBindingKind::GamepadButton: return Buttons;
        case InputBindingKind::GamepadAxis: return Axes;
        case InputBindingKind::MouseButton: return MouseButtons;
        case InputBindingKind::MouseAxis: return MouseAxes;
        }
        return Keys;
    }

    const char *InputKindName(InputBindingKind kind) { return KindNames[static_cast<int>(kind)]; }

    int FindInputName(std::span<const InputNamedCode> table, std::string_view name)
    {
        for (std::size_t i = 0; i < table.size(); ++i)
            if (name == table[i].name) return static_cast<int>(i);
        return -1;
    }

    std::string InputMappingBinding::InputId() const
    {
        std::string id = std::string(InputKindName(kind)) + ":" + value;
        if (UsesGamepad(kind)) id += "@" + std::to_string(gamepad);
        return id;
    }

    std::string InputMappingBinding::Label() const
    {
        std::string label = value;
        switch (kind)
        {
        case InputBindingKind::Key: label = "Key " + value; break;
        case InputBindingKind::GamepadButton: label = "Pad " + std::to_string(gamepad) + " " + value; break;
        case InputBindingKind::GamepadAxis: label = "Pad " + std::to_string(gamepad) + " axis " + value; break;
        case InputBindingKind::MouseButton: label = "Mouse " + value; break;
        case InputBindingKind::MouseAxis: label = "Mouse axis " + value; break;
        }
        const auto trigger = this->trigger.Description();
        return trigger.empty() ? label : label + " (" + trigger + ")";
    }

    bool InputMappingDocument::Parse(std::string_view json, std::string *error)
    {
        assets::JsonValue root;
        if (!assets::ParseJson(json, root, error)) return false;
        const auto *actions = root.Find("Actions");
        if (!root.IsObject() || !actions || !actions->IsArray())
        {
            if (error) *error = "Input mapping needs an object with an \"Actions\" array.";
            return false;
        }
        std::vector<InputMappingAction> parsed;
        for (const auto &actionValue : actions->items)
        {
            if (!actionValue.IsObject()) continue;
            InputMappingAction action;
            action.source = actionValue;
            action.name = actionValue.GetString("Name");
            if (const auto *bindings = actionValue.Find("Bindings"); bindings && bindings->IsArray())
                for (const auto &bindingValue : bindings->items)
                {
                    if (!bindingValue.IsObject()) continue;
                    InputMappingBinding binding;
                    binding.source = bindingValue;
                    const auto kind = bindingValue.GetString("Kind", "Key");
                    const auto found = std::find(std::begin(KindNames), std::end(KindNames), kind);
                    binding.kind = found == std::end(KindNames) ? InputBindingKind::Key
                                                                : static_cast<InputBindingKind>(found - std::begin(KindNames));
                    binding.value = bindingValue.GetString(ValueFields[static_cast<int>(binding.kind)], InputNamesFor(binding.kind)[0].name);
                    binding.gamepad = static_cast<int>(bindingValue.GetNumber("Gamepad", 0));
                    binding.scale = static_cast<float>(bindingValue.GetNumber("Scale", 1));
                    binding.deadZone = static_cast<float>(bindingValue.GetNumber("DeadZone", 0));
                    binding.trigger = ParseTrigger(bindingValue);
                    action.bindings.push_back(std::move(binding));
                }
            parsed.push_back(std::move(action));
        }
        m_root = std::move(root);
        m_actions = std::move(parsed);
        return true;
    }

    std::string InputMappingDocument::Serialize() const
    {
        auto root = m_root;
        auto actions = assets::JsonValue::MakeArray();
        for (const auto &action : m_actions)
        {
            auto actionValue = action.source.IsObject() ? action.source : assets::JsonValue::MakeObject();
            actionValue.Set("Name", assets::JsonValue::MakeString(action.name));
            auto bindings = assets::JsonValue::MakeArray();
            for (const auto &binding : action.bindings)
            {
                auto value = binding.source.IsObject() ? binding.source : assets::JsonValue::MakeObject();
                value.Set("Kind", assets::JsonValue::MakeString(InputKindName(binding.kind)));
                // Only the field for the current kind is meaningful; drop stale ones from kind changes.
                for (int field = 0; field < 5; ++field)
                    if (field != static_cast<int>(binding.kind)) value.Remove(ValueFields[field]);
                value.Set(ValueFields[static_cast<int>(binding.kind)], assets::JsonValue::MakeString(binding.value));
                if (UsesGamepad(binding.kind) && (binding.gamepad != 0 || value.Find("Gamepad")))
                    value.Set("Gamepad", assets::JsonValue::MakeNumber(binding.gamepad));
                else if (!UsesGamepad(binding.kind))
                    value.Remove("Gamepad");
                value.Set("Scale", assets::JsonValue::MakeNumber(Rounded(binding.scale)));
                value.Set("DeadZone", assets::JsonValue::MakeNumber(Rounded(binding.deadZone)));
                WriteTrigger(value, binding.trigger);
                bindings.items.push_back(std::move(value));
            }
            actionValue.Set("Bindings", std::move(bindings));
            actions.items.push_back(std::move(actionValue));
        }
        root.Set("Actions", std::move(actions));
        return assets::WriteJson(root);
    }

    std::vector<InputMappingSharedInput> InputMappingDocument::FindSharedInputs() const
    {
        std::map<std::string, InputMappingSharedInput> inputs;
        std::map<std::string, std::vector<std::string>> triggers; // Trigger description per binding on each input.
        for (const auto &action : m_actions)
            for (const auto &binding : action.bindings)
            {
                // Axes are commonly shared by design (menus reuse movement sticks); report buttons and keys.
                if (binding.kind == InputBindingKind::GamepadAxis || binding.kind == InputBindingKind::MouseAxis) continue;
                const auto id = binding.InputId();
                auto &entry = inputs[id];
                entry.inputId = id;
                entry.label = binding.Label();
                if (std::find(entry.actions.begin(), entry.actions.end(), action.name) == entry.actions.end())
                    entry.actions.push_back(action.name);
                triggers[id].push_back(binding.trigger.Description());
            }
        std::vector<InputMappingSharedInput> shared;
        for (auto &[id, entry] : inputs)
        {
            if (entry.actions.size() <= 1) continue;
            auto kinds = triggers[id];
            std::sort(kinds.begin(), kinds.end());
            entry.disambiguatedByTriggers = std::adjacent_find(kinds.begin(), kinds.end()) == kinds.end();
            entry.label = id.substr(id.find(':') + 1); // The physical input, without one binding's trigger.
            shared.push_back(std::move(entry));
        }
        return shared;
    }

    std::vector<std::string> InputMappingDocument::FindUnusedGamepadButtons() const
    {
        std::set<std::string> used;
        for (const auto &action : m_actions)
            for (const auto &binding : action.bindings)
                if (binding.kind == InputBindingKind::GamepadButton && binding.gamepad == 0) used.insert(binding.value);
        std::vector<std::string> unused;
        for (const auto &button : Buttons)
            if (!used.contains(button.name) && std::string_view(button.name) != "Guide") unused.emplace_back(button.name);
        return unused;
    }

    std::vector<std::string> InputMappingDocument::FindUnusedKeys() const
    {
        std::set<std::string> used;
        for (const auto &action : m_actions)
            for (const auto &binding : action.bindings)
                if (binding.kind == InputBindingKind::Key) used.insert(binding.value);
        std::vector<std::string> unused;
        for (const auto &key : Keys)
            if (!used.contains(key.name)) unused.emplace_back(key.name);
        return unused;
    }

    std::vector<std::string> InputMappingDocument::FindInvalidActionNames() const
    {
        std::map<std::string, int> counts;
        for (const auto &action : m_actions) ++counts[action.name];
        std::vector<std::string> invalid;
        for (const auto &[name, count] : counts)
            if (name.empty() || count > 1) invalid.push_back(name.empty() ? std::string("(unnamed)") : name);
        return invalid;
    }
}
