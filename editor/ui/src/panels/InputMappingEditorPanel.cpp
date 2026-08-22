#include "PlutoGE/ui/panels/InputMappingEditorPanel.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/ui/EditorShell.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <regex>
#include <sstream>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr const char *Kinds[] = {"Key", "GamepadButton", "GamepadAxis"};
        constexpr const char *Buttons[] = {"A","B","X","Y","LeftBumper","RightBumper","Back","Start","Guide","LeftStick","RightStick","DpadUp","DpadRight","DpadDown","DpadLeft"};
        constexpr const char *Axes[] = {"LeftX","LeftY","RightX","RightY","LeftTrigger","RightTrigger"};
        constexpr const char *Keys[] = {"Space","A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z","Escape","Enter","Tab","Backspace"};
        constexpr int KeyValues[] = {32,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,256,257,258,259};

        std::string Escape(std::string_view value)
        {
            std::string result;
            for (char c : value) { if (c == '"' || c == '\\') result += '\\'; result += c; }
            return result;
        }
        std::string ReadFile(const std::filesystem::path &path)
        { std::ifstream in(path, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
        std::string ExtractString(const std::string &text, const char *name, std::string fallback = {})
        {
            std::smatch match;
            const std::regex expression(std::string("\\\"") + name + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
            return std::regex_search(text, match, expression) ? match[1].str() : fallback;
        }
        float ExtractFloat(const std::string &text, const char *name, float fallback)
        {
            std::smatch match;
            const std::regex expression(std::string("\\\"") + name + "\\\"\\s*:\\s*(-?[0-9.]+)");
            return std::regex_search(text, match, expression) ? std::stof(match[1].str()) : fallback;
        }
        int IndexOf(const char *const *values, int count, const std::string &value)
        { for (int i=0;i<count;++i) if (value == values[i]) return i; return 0; }
    }

    void InputMappingEditorPanel::Load()
    {
        auto &shell = EditorShell::GetInstance();
        m_loadedReference = shell.GetActiveInputMappingAssetReference();
        m_actions.clear();
        const auto *project = shell.GetProject();
        if (!project) return;
        const std::string json = ReadFile(project->ResolveAssetReference(m_loadedReference));
        std::regex actionRegex("\\{\\s*\\\"Name\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"\\s*,\\s*\\\"Bindings\\\"\\s*:\\s*\\[([\\s\\S]*?)\\]\\s*\\}");
        for (auto it = std::sregex_iterator(json.begin(), json.end(), actionRegex); it != std::sregex_iterator(); ++it)
        {
            InputMappingAction action; action.name = (*it)[1].str();
            const std::string bindings = (*it)[2].str();
            std::regex bindingRegex("\\{([^{}]*)\\}");
            for (auto bit = std::sregex_iterator(bindings.begin(), bindings.end(), bindingRegex); bit != std::sregex_iterator(); ++bit)
            {
                const std::string body = (*bit)[1].str();
                InputMappingBinding binding;
                binding.kind = IndexOf(Kinds, 3, ExtractString(body, "Kind", "Key"));
                binding.key = IndexOf(Keys, static_cast<int>(std::size(Keys)), ExtractString(body, "Key", "Space"));
                binding.button = IndexOf(Buttons, 15, ExtractString(body, "Button", "A"));
                binding.axis = IndexOf(Axes, 6, ExtractString(body, "Axis", "LeftX"));
                binding.gamepad = static_cast<int>(ExtractFloat(body, "Gamepad", 0));
                binding.scale = ExtractFloat(body, "Scale", 1.0f);
                binding.deadZone = ExtractFloat(body, "DeadZone", 0.15f);
                action.bindings.push_back(binding);
            }
            m_actions.push_back(std::move(action));
        }
        m_dirty = false;
    }

    bool InputMappingEditorPanel::Save()
    {
        const auto *project = EditorShell::GetInstance().GetProject();
        if (!project) return false;
        std::ofstream out(project->ResolveAssetReference(m_loadedReference), std::ios::binary | std::ios::trunc);
        out << "{\n  \"Actions\": [\n";
        for (size_t a=0;a<m_actions.size();++a)
        {
            const auto &action=m_actions[a];
            out << "    {\n      \"Name\": \"" << Escape(action.name) << "\",\n      \"Bindings\": [\n";
            for (size_t b=0;b<action.bindings.size();++b)
            {
                const auto &binding=action.bindings[b];
                out << "        { \"Kind\": \"" << Kinds[binding.kind] << "\", \"Key\": \"" << Keys[binding.key]
                    << "\", \"Button\": \"" << Buttons[binding.button] << "\", \"Axis\": \"" << Axes[binding.axis]
                    << "\", \"Gamepad\": " << binding.gamepad << ", \"Scale\": " << binding.scale << ", \"DeadZone\": " << binding.deadZone << " }"
                    << (b+1<action.bindings.size()?",":"") << "\n";
            }
            out << "      ]\n    }" << (a+1<m_actions.size()?",":"") << "\n";
        }
        out << "  ]\n}\n";
        if (out) { m_dirty=false; return true; } return false;
    }

    void InputMappingEditorPanel::Render()
    {
        auto &shell=EditorShell::GetInstance();
        const auto &reference=shell.GetActiveInputMappingAssetReference();
        if (reference.empty()) { ImGui::TextDisabled("No input mapping selected."); return; }
        if (reference != m_loadedReference) Load();
        ImGui::TextWrapped("Input Mapping: %s", reference.c_str());
        ImGui::SameLine();
        if (ImGui::Button(m_dirty ? "Save *" : "Save")) Save();
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Live Controller Diagnostics", ImGuiTreeNodeFlags_DefaultOpen))
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
                for (std::size_t button = 0; button < gamepad.buttons.size(); ++button)
                    if (gamepad.buttons[button]) pressed += " " + std::to_string(button);
                ImGui::TextUnformatted(pressed.c_str());
            }
            if (!anyConnected) ImGui::TextDisabled("No controller detected by GLFW.");
        }
        ImGui::Separator();
        int removeAction=-1;
        for (int a=0;a<static_cast<int>(m_actions.size());++a)
        {
            auto &action=m_actions[a]; ImGui::PushID(a);
            if (ImGui::CollapsingHeader(action.name.empty()?"Unnamed Action":action.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                char name[128]{}; strncpy_s(name, action.name.c_str(), _TRUNCATE);
                if (ImGui::InputText("Action", name, sizeof(name))) { action.name=name; m_dirty=true; }
                ImGui::SameLine(); if (ImGui::Button("Remove Action")) removeAction=a;
                int removeBinding=-1;
                for (int b=0;b<static_cast<int>(action.bindings.size());++b)
                {
                    auto &binding=action.bindings[b]; ImGui::PushID(b); ImGui::SeparatorText("Binding");
                    if (ImGui::Combo("Device", &binding.kind, Kinds, 3)) m_dirty=true;
                    if (binding.kind==0) { if (ImGui::Combo("Key", &binding.key, Keys, static_cast<int>(std::size(Keys)))) m_dirty=true; }
                    else { if (ImGui::SliderInt("Gamepad", &binding.gamepad, 0, 15)) m_dirty=true;
                        if (binding.kind==1) { if (ImGui::Combo("Button", &binding.button, Buttons, 15)) m_dirty=true; }
                        else if (ImGui::Combo("Axis", &binding.axis, Axes, 6)) m_dirty=true; }
                    if (ImGui::DragFloat("Scale", &binding.scale, .05f, -10, 10)) m_dirty=true;
                    if (ImGui::SliderFloat("Dead Zone", &binding.deadZone, 0, .99f)) m_dirty=true;
                    if (ImGui::Button("Remove Binding")) removeBinding=b; ImGui::PopID();
                }
                if (removeBinding>=0) { action.bindings.erase(action.bindings.begin()+removeBinding); m_dirty=true; }
                if (ImGui::Button("Add Binding")) { action.bindings.emplace_back(); m_dirty=true; }
            }
            ImGui::PopID();
        }
        if (removeAction>=0) { m_actions.erase(m_actions.begin()+removeAction); m_dirty=true; }
        if (ImGui::Button("Add Action")) { m_actions.push_back({"New Action", {}}); m_dirty=true; }
    }
}
