#include "PlutoGE/ui/panels/ConsolePanel.h"

#include "PlutoGE/ui/EditorShell.h"

#include <algorithm>
#include <cctype>
#include <string>

#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        bool ContainsInsensitive(std::string_view text, std::string_view filter)
        {
            if (filter.empty())
            {
                return true;
            }

            auto lower = [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            };

            std::string haystack(text);
            std::string needle(filter);
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), lower);
            std::transform(needle.begin(), needle.end(), needle.begin(), lower);
            return haystack.find(needle) != std::string::npos;
        }

        ImVec4 SeverityColor(EditorShell::ConsoleSeverity severity)
        {
            switch (severity)
            {
            case EditorShell::ConsoleSeverity::Warning:
                return ImVec4(1.0f, 0.78f, 0.25f, 1.0f);
            case EditorShell::ConsoleSeverity::Error:
                return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
            case EditorShell::ConsoleSeverity::Info:
            default:
                return ImGui::GetStyleColorVec4(ImGuiCol_Text);
            }
        }

        const char *SeverityLabel(EditorShell::ConsoleSeverity severity)
        {
            switch (severity)
            {
            case EditorShell::ConsoleSeverity::Warning:
                return "Warning";
            case EditorShell::ConsoleSeverity::Error:
                return "Error";
            case EditorShell::ConsoleSeverity::Info:
            default:
                return "Info";
            }
        }
    }

    void ConsolePanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();

        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputText("Filter", m_filterBuffer.data(), m_filterBuffer.size());
        ImGui::SameLine();
        ImGui::Checkbox("Info", &m_showInfo);
        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &m_showWarnings);
        ImGui::SameLine();
        ImGui::Checkbox("Errors", &m_showErrors);
        ImGui::SameLine();
        ImGui::Checkbox("Auto Scroll", &m_autoScroll);
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            editorShell.ClearConsoleMessages();
        }

        ImGui::Separator();

        ImGui::BeginChild("ConsoleMessages", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
        const auto messages = editorShell.GetConsoleMessages();
        std::string selectableText;
        for (const auto &message : messages)
        {
            if ((message.severity == EditorShell::ConsoleSeverity::Info && !m_showInfo) ||
                (message.severity == EditorShell::ConsoleSeverity::Warning && !m_showWarnings) ||
                (message.severity == EditorShell::ConsoleSeverity::Error && !m_showErrors) ||
                !ContainsInsensitive(message.text, m_filterBuffer.data()))
            {
                continue;
            }

            selectableText += '[';
            selectableText += SeverityLabel(message.severity);
            selectableText += "] ";
            selectableText += message.text;
            selectableText += '\n';
        }

        if (!selectableText.empty())
            ImGui::InputTextMultiline("##SelectableConsoleText", selectableText.data(), selectableText.size() + 1,
                                      ImVec2(-1.0f, -1.0f),
                                      ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoHorizontalScroll);

        if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
}
