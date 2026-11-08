#include "PlutoGE/ui/EditorCommands.h"

#include "PlutoGE/ui/EditorSettings.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>

namespace PlutoGE::ui
{
    namespace
    {
        std::string SettingKey(std::string_view id)
        {
            return "keys." + std::string(id);
        }
    }

    void EditorCommandRegistry::Register(EditorCommandDefinition definition)
    {
        if (definition.id.empty() || Find(definition.id))
            return;
        m_commands.push_back(std::move(definition));
    }

    const EditorCommandDefinition *EditorCommandRegistry::Find(std::string_view id) const
    {
        const auto it = std::find_if(m_commands.begin(), m_commands.end(), [&](const auto &command) { return command.id == id; });
        return it == m_commands.end() ? nullptr : &*it;
    }

    ImGuiKeyChord EditorCommandRegistry::GetShortcut(std::string_view id) const
    {
        const auto *command = Find(id);
        if (!command)
            return ImGuiKey_None;
        if (m_settings && m_settings->Has(SettingKey(id)))
            return static_cast<ImGuiKeyChord>(m_settings->GetInt(SettingKey(id), command->defaultShortcut));
        return command->defaultShortcut;
    }

    void EditorCommandRegistry::SetShortcut(std::string_view id, ImGuiKeyChord chord)
    {
        const auto *command = Find(id);
        if (!command || !m_settings)
            return;
        if (chord == command->defaultShortcut)
            m_settings->Remove(SettingKey(id));
        else
            m_settings->SetInt(SettingKey(id), static_cast<int>(chord));
    }

    void EditorCommandRegistry::ResetShortcut(std::string_view id)
    {
        if (m_settings)
            m_settings->Remove(SettingKey(id));
    }

    bool EditorCommandRegistry::IsCustomized(std::string_view id) const
    {
        return m_settings && m_settings->Has(SettingKey(id));
    }

    const EditorCommandDefinition *EditorCommandRegistry::FindByShortcut(ImGuiKeyChord chord, std::string_view except) const
    {
        if (chord == ImGuiKey_None)
            return nullptr;
        for (const auto &command : m_commands)
            if (command.id != except && GetShortcut(command.id) == chord)
                return &command;
        return nullptr;
    }

    void EditorCommandRegistry::BeginFrame()
    {
        m_bindings.clear();
    }

    void EditorCommandRegistry::Bind(std::string_view id, std::function<void()> handler, bool enabled)
    {
        if (!Find(id) || !handler)
            return;
        std::erase_if(m_bindings, [&](const Binding &binding) { return binding.id == id; });
        m_bindings.push_back(Binding{std::string(id), std::move(handler), enabled});
    }

    const EditorCommandRegistry::Binding *EditorCommandRegistry::FindBinding(std::string_view id) const
    {
        const auto it = std::find_if(m_bindings.begin(), m_bindings.end(), [&](const Binding &binding) { return binding.id == id; });
        return it == m_bindings.end() ? nullptr : &*it;
    }

    bool EditorCommandRegistry::IsAvailable(std::string_view id) const
    {
        const auto *binding = FindBinding(id);
        return binding && binding->enabled;
    }

    bool EditorCommandRegistry::Run(std::string_view id)
    {
        const auto *binding = FindBinding(id);
        if (!binding || !binding->enabled)
            return false;
        auto handler = binding->handler; // The handler may rebind commands.
        handler();
        return true;
    }

    bool EditorCommandRegistry::DispatchShortcuts(const std::function<bool(EditorCommandScope)> &scopeActive)
    {
        if (!ImGui::GetCurrentContext() || ImGui::GetFrameCount() - m_recordingFrame <= 1)
            return false;
        for (const auto &command : m_commands)
        {
            if (!IsAvailable(command.id) || (scopeActive && !scopeActive(command.scope)))
                continue;
            const ImGuiInputFlags flags = command.repeat ? ImGuiInputFlags_Repeat : ImGuiInputFlags_None;
            const auto pressed = [&](ImGuiKeyChord chord) { return chord != ImGuiKey_None && ImGui::IsKeyChordPressed(chord, flags); };
            if (pressed(GetShortcut(command.id)) || (!IsCustomized(command.id) && pressed(command.alternateShortcut)))
            {
                Run(command.id);
                return true;
            }
        }
        return false;
    }

    void EditorCommandRegistry::MarkRecordingShortcut()
    {
        if (ImGui::GetCurrentContext())
            m_recordingFrame = ImGui::GetFrameCount();
    }

    std::string EditorCommandRegistry::GetShortcutText(std::string_view id) const
    {
        return FormatShortcut(GetShortcut(id));
    }

    std::string FormatShortcut(ImGuiKeyChord chord)
    {
        const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
        if (key == ImGuiKey_None)
            return {};
        std::string text;
        if (chord & ImGuiMod_Ctrl)
            text += "Ctrl+";
        if (chord & ImGuiMod_Shift)
            text += "Shift+";
        if (chord & ImGuiMod_Alt)
            text += "Alt+";
        if (chord & ImGuiMod_Super)
            text += "Super+";
        switch (key)
        {
        case ImGuiKey_Comma: return text + ",";
        case ImGuiKey_Period: return text + ".";
        case ImGuiKey_Slash: return text + "/";
        case ImGuiKey_Minus: return text + "-";
        case ImGuiKey_Equal: return text + "=";
        case ImGuiKey_LeftBracket: return text + "[";
        case ImGuiKey_RightBracket: return text + "]";
        case ImGuiKey_Semicolon: return text + ";";
        case ImGuiKey_Apostrophe: return text + "'";
        case ImGuiKey_GraveAccent: return text + "`";
        case ImGuiKey_Backslash: return text + "\\";
        default: break;
        }
        if (key >= ImGuiKey_0 && key <= ImGuiKey_9)
            return text + static_cast<char>('0' + (key - ImGuiKey_0));
        return text + ImGui::GetKeyName(key);
    }

    std::optional<ImGuiKeyChord> CapturePressedShortcut()
    {
        if (!ImGui::GetCurrentContext())
            return std::nullopt;
        for (int key = ImGuiKey_Keyboard_BEGIN; key < ImGuiKey_Keyboard_END; ++key)
        {
            const auto namedKey = static_cast<ImGuiKey>(key);
            if (ImGui::IsLRModKey(namedKey) || namedKey == ImGuiKey_Escape)
                continue;
            if (ImGui::IsKeyPressed(namedKey, false))
                return static_cast<ImGuiKeyChord>(namedKey) | ImGui::GetIO().KeyMods;
        }
        return std::nullopt;
    }

    void EditorCommandRegistry::RunQueued()
    {
        if (m_queued.empty())
            return;
        const auto id = std::exchange(m_queued, {});
        Run(id);
    }

    int FuzzyMatchScore(std::string_view text, std::string_view query)
    {
        if (query.empty())
            return 0;
        const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
        const auto isWordStart = [&](std::size_t i)
        {
            return i == 0 || text[i - 1] == ' ' || text[i - 1] == ':' || text[i - 1] == '/' || text[i - 1] == '_' || text[i - 1] == '.' ||
                   (std::isupper(static_cast<unsigned char>(text[i])) && std::islower(static_cast<unsigned char>(text[i - 1])));
        };
        // Greedy match from each place the first letter appears; the best start wins, so "dup" scores "Edit: Duplicate"
        // from the D of Duplicate rather than the d of Edit.
        int best = -1;
        for (std::size_t start = 0; start < text.size(); ++start)
        {
            if (lower(text[start]) != lower(query[0]))
                continue;
            int score = 0;
            std::size_t queryIndex = 0;
            bool previousMatched = false;
            for (std::size_t i = start; i < text.size() && queryIndex < query.size(); ++i)
            {
                if (lower(text[i]) != lower(query[queryIndex]))
                {
                    previousMatched = false;
                    continue;
                }
                score += 1 + (previousMatched ? 3 : 0) + (isWordStart(i) ? 5 : 0);
                previousMatched = true;
                ++queryIndex;
            }
            if (queryIndex == query.size())
                best = (std::max)(best, score);
        }
        if (best < 0)
            return -1;
        // Shorter texts with the same matches rank first.
        return best * 100 - static_cast<int>(text.size());
    }
}
