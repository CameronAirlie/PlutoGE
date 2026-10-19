#include "PlutoGE/ui/panels/InputMappingEditorPanel.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/ui/EditorShell.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        std::string ReadFile(const std::filesystem::path &path)
        { std::ifstream in(path, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }

        bool ContainsInsensitive(std::string_view text, std::string_view needle)
        {
            if (needle.empty()) return true;
            return std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }) != text.end();
        }

        // Combo over a name table that also shows a value this editor does not list, unchanged.
        bool NameCombo(const char *label, std::string &value, std::span<const InputNamedCode> names)
        {
            bool changed = false;
            const int current = FindInputName(names, value);
            const std::string preview = current >= 0 ? value : value + " (unlisted)";
            if (ImGui::BeginCombo(label, preview.c_str()))
            {
                for (int i = 0; i < static_cast<int>(names.size()); ++i)
                    if (ImGui::Selectable(names[i].name, i == current)) { value = names[i].name; changed = true; }
                ImGui::EndCombo();
            }
            return changed;
        }

        // Trigger kind and the settings that kind reads; returns true when anything changed.
        bool TriggerEditor(InputMappingTrigger &trigger)
        {
            bool changed = false;
            int kind = static_cast<int>(trigger.kind);
            const char *kinds[InputTriggerKindCount];
            for (int i = 0; i < InputTriggerKindCount; ++i) kinds[i] = InputTriggerKindName(static_cast<InputTriggerKind>(i));
            if (ImGui::Combo("Trigger", &kind, kinds, InputTriggerKindCount))
            {
                trigger.kind = static_cast<InputTriggerKind>(kind);
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Press: fires on press, active while held.\nTap: fires on a quick release.\n"
                                  "Hold: fires after holding, active until release.\nMultiTap: fires on repeated presses.");
            switch (trigger.kind)
            {
            case InputTriggerKind::Tap:
                changed |= ImGui::SliderFloat("Max Hold (s)", &trigger.duration, 0.05f, 1.0f, "%.2f");
                break;
            case InputTriggerKind::Hold:
                changed |= ImGui::SliderFloat("Hold Time (s)", &trigger.duration, 0.0f, 2.0f, "%.2f");
                break;
            case InputTriggerKind::MultiTap:
                changed |= ImGui::SliderInt("Presses", &trigger.count, 1, 4);
                changed |= ImGui::SliderFloat("Window (s)", &trigger.window, 0.1f, 0.6f, "%.2f");
                changed |= ImGui::Checkbox("Exclusive", &trigger.exclusive);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Fire only after the window passes with no further press, so a single tap\n"
                                      "and a double tap can share this input (adds the window's latency).");
                break;
            case InputTriggerKind::Press:
                break;
            }
            return changed;
        }

        std::string Join(const std::vector<std::string> &values)
        {
            std::string text;
            for (const auto &value : values) text += (text.empty() ? "" : ", ") + value;
            return text;
        }
    }

    void InputMappingEditorPanel::Load()
    {
        auto &shell = EditorShell::GetInstance();
        m_loadedReference = shell.GetActiveInputMappingAssetReference();
        m_document = {};
        m_loadError.clear();
        m_saveError.clear();
        m_captureAction = m_captureBinding = -1;
        const auto *project = shell.GetProject();
        if (!project) return;
        const std::string json = ReadFile(project->ResolveAssetReference(m_loadedReference));
        if (!m_document.Parse(json, &m_loadError))
            m_loadError = "Cannot edit this mapping (" + m_loadError + "). Saving is disabled so the file is not overwritten.";
        m_dirty = false;
    }

    bool InputMappingEditorPanel::Save()
    {
        const auto *project = EditorShell::GetInstance().GetProject();
        if (!project || !m_loadError.empty()) return false;
        const auto path = project->ResolveAssetReference(m_loadedReference);
        // Write beside the target first so a failed write cannot truncate the mapping.
        const auto temporary = std::filesystem::path(path).concat(".saving");
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out << m_document.Serialize();
            if (!out) { m_saveError = "Could not write " + temporary.string(); return false; }
        }
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error) { m_saveError = "Could not replace mapping: " + error.message(); return false; }
        m_saveError.clear();
        m_dirty = false;
        return true;
    }

    void InputMappingEditorPanel::UpdateCapture()
    {
        auto &actions = m_document.Actions();
        if (m_captureAction < 0 || m_captureAction >= static_cast<int>(actions.size()) || m_captureBinding < 0 ||
            m_captureBinding >= static_cast<int>(actions[m_captureAction].bindings.size()))
        {
            m_captureAction = m_captureBinding = -1;
            return;
        }
        const auto &input = core::Engine::GetInstance().GetWindow().GetInputState();
        auto &binding = actions[m_captureAction].bindings[m_captureBinding];
        const auto finish = [&](InputBindingKind kind, const char *value, int gamepad) {
            binding.kind = kind;
            binding.value = value;
            binding.gamepad = gamepad;
            m_captureAction = m_captureBinding = -1;
            m_dirty = true;
        };
        for (const auto &key : InputKeyNames())
            if (key.code >= 0 && key.code < static_cast<int>(input.keys.size()) && input.keys[key.code] && !input.previousKeys[key.code])
            {
                // Escape cancels; bind Escape from the Key list instead.
                if (std::string_view(key.name) == "Escape") { m_captureAction = m_captureBinding = -1; return; }
                return finish(InputBindingKind::Key, key.name, 0);
            }
        for (std::size_t pad = 0; pad < input.gamepads.size(); ++pad)
            for (const auto &button : InputGamepadButtonNames())
                if (input.IsGamepadButtonPressed(static_cast<std::uint16_t>(pad), static_cast<std::uint16_t>(button.code)))
                    return finish(InputBindingKind::GamepadButton, button.name, static_cast<int>(pad));
    }

    void InputMappingEditorPanel::RenderReport()
    {
        if (!ImGui::CollapsingHeader("Mapping Report")) return;
        const auto invalid = m_document.FindInvalidActionNames();
        if (!invalid.empty())
            ImGui::TextColored(ImVec4(1, .45f, .35f, 1), "Empty or duplicate action names: %s", Join(invalid).c_str());
        ImGui::TextWrapped("Unused gamepad buttons (pad 0): %s", Join(m_document.FindUnusedGamepadButtons()).c_str());
        const auto keys = m_document.FindUnusedKeys();
        if (ImGui::TreeNode("UnusedKeys", "Unused keys (%zu)", keys.size()))
        {
            ImGui::TextWrapped("%s", Join(keys).c_str());
            ImGui::TreePop();
        }
        const auto shared = m_document.FindSharedInputs();
        if (ImGui::TreeNode("SharedInputs", "Inputs used by several actions (%zu)", shared.size()))
        {
            ImGui::TextDisabled("Often intentional, e.g. a menu action and a gameplay action on the same button.");
            for (const auto &input : shared)
                ImGui::BulletText("%s: %s%s", input.label.c_str(), Join(input.actions).c_str(),
                                  input.disambiguatedByTriggers ? " (told apart by triggers)" : "");
            ImGui::TreePop();
        }
    }

    void InputMappingEditorPanel::Render()
    {
        auto &shell = EditorShell::GetInstance();
        const auto &reference = shell.GetActiveInputMappingAssetReference();
        if (reference.empty()) { ImGui::TextDisabled("No input mapping selected."); return; }
        if (reference != m_loadedReference) Load();
        ImGui::TextWrapped("Input Mapping: %s", reference.c_str());
        if (!m_loadError.empty()) { ImGui::TextColored(ImVec4(1, .45f, .35f, 1), "%s", m_loadError.c_str()); return; }
        ImGui::SameLine();
        if (ImGui::Button(m_dirty ? "Save *" : "Save")) Save();
        ImGui::SameLine();
        if (ImGui::Button("Revert")) Load();
        if (!m_saveError.empty()) ImGui::TextColored(ImVec4(1, .45f, .35f, 1), "%s", m_saveError.c_str());
        UpdateCapture();
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Live Controller Diagnostics"))
        {
            const auto &gamepads = core::Engine::GetInstance().GetWindow().GetInputState().gamepads;
            bool anyConnected = false;
            for (std::size_t index = 0; index < gamepads.size(); ++index)
            {
                const auto &gamepad = gamepads[index];
                if (!gamepad.connected) continue;
                anyConnected = true;
                ImGui::Text("Gamepad %zu", index);
                ImGui::Text("Axes: %.2f  %.2f  %.2f  %.2f  %.2f  %.2f",
                            gamepad.axes[0], gamepad.axes[1], gamepad.axes[2],
                            gamepad.axes[3], gamepad.axes[4], gamepad.axes[5]);
                std::string pressed = "Buttons:";
                for (const auto &button : InputGamepadButtonNames())
                    if (button.code < static_cast<int>(gamepad.buttons.size()) && gamepad.buttons[button.code]) pressed += std::string(" ") + button.name;
                ImGui::TextUnformatted(pressed.c_str());
            }
            if (!anyConnected) ImGui::TextDisabled("No controller detected by GLFW.");
        }
        RenderReport();
        ImGui::Separator();
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##filter", "Filter actions or inputs", m_filter, sizeof(m_filter));
        ImGui::SameLine();
        if (ImGui::Button("Add Action"))
        {
            m_document.Actions().push_back({"NewAction", {}, assets::JsonValue::MakeObject()});
            m_dirty = true;
        }

        auto &actions = m_document.Actions();
        int removeAction = -1, duplicateAction = -1;
        for (int a = 0; a < static_cast<int>(actions.size()); ++a)
        {
            auto &action = actions[a];
            bool matches = ContainsInsensitive(action.name, m_filter);
            for (const auto &binding : action.bindings) matches = matches || ContainsInsensitive(binding.Label(), m_filter);
            if (!matches) continue;
            ImGui::PushID(a);
            const std::string header = (action.name.empty() ? std::string("Unnamed Action") : action.name) + "###action";
            if (ImGui::CollapsingHeader(header.c_str()))
            {
                char name[128]{};
                action.name.copy(name, sizeof(name) - 1);
                if (ImGui::InputText("Action", name, sizeof(name))) { action.name = name; m_dirty = true; }
                if (ImGui::Button("Duplicate Action")) duplicateAction = a;
                ImGui::SameLine();
                if (ImGui::Button("Remove Action")) removeAction = a;
                int removeBinding = -1;
                for (int b = 0; b < static_cast<int>(action.bindings.size()); ++b)
                {
                    auto &binding = action.bindings[b];
                    ImGui::PushID(b);
                    ImGui::SeparatorText(binding.Label().c_str());
                    int kind = static_cast<int>(binding.kind);
                    const char *kinds[] = {"Key", "GamepadButton", "GamepadAxis", "MouseButton", "MouseAxis"};
                    if (ImGui::Combo("Device", &kind, kinds, 5))
                    {
                        binding.kind = static_cast<InputBindingKind>(kind);
                        binding.value = InputNamesFor(binding.kind)[0].name;
                        m_dirty = true;
                    }
                    if (NameCombo("Input", binding.value, InputNamesFor(binding.kind))) m_dirty = true;
                    if ((binding.kind == InputBindingKind::GamepadButton || binding.kind == InputBindingKind::GamepadAxis) &&
                        ImGui::SliderInt("Gamepad", &binding.gamepad, 0, 15))
                        m_dirty = true;
                    if (ImGui::DragFloat("Scale", &binding.scale, .05f, -10, 10)) m_dirty = true;
                    if (ImGui::SliderFloat("Dead Zone", &binding.deadZone, 0, .99f)) m_dirty = true;
                    if (TriggerEditor(binding.trigger)) m_dirty = true;
                    const bool capturing = m_captureAction == a && m_captureBinding == b;
                    if (ImGui::Button(capturing ? "Press a key or pad button... (Esc cancels)" : "Listen"))
                    {
                        m_captureAction = capturing ? -1 : a;
                        m_captureBinding = capturing ? -1 : b;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Remove Binding")) removeBinding = b;
                    ImGui::PopID();
                }
                if (removeBinding >= 0) { action.bindings.erase(action.bindings.begin() + removeBinding); m_dirty = true; m_captureAction = -1; }
                if (ImGui::Button("Add Binding")) { action.bindings.emplace_back(); m_dirty = true; }
            }
            ImGui::PopID();
        }
        if (duplicateAction >= 0)
        {
            auto copy = actions[duplicateAction];
            copy.name += "Copy";
            actions.insert(actions.begin() + duplicateAction + 1, std::move(copy));
            m_dirty = true;
            m_captureAction = -1;
        }
        if (removeAction >= 0) { actions.erase(actions.begin() + removeAction); m_dirty = true; m_captureAction = -1; }
    }
}
