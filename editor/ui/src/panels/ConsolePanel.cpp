#include "PlutoGE/ui/panels/ConsolePanel.h"

#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorSettings.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorTheme.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>

#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr std::array<ConsoleSource, kConsoleSourceCount> kSources{
            ConsoleSource::Editor, ConsoleSource::Script, ConsoleSource::ScriptBuild, ConsoleSource::Import, ConsoleSource::Build};

        std::string Lower(std::string_view text)
        {
            std::string lower(text);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lower;
        }

        const char *SeverityIcon(ConsoleSeverity severity)
        {
            switch (severity)
            {
            case ConsoleSeverity::Warning: return icons::Warning;
            case ConsoleSeverity::Error: return icons::Error;
            case ConsoleSeverity::Info:
            default: return icons::Info;
            }
        }

        ImVec4 SeverityColor(ConsoleSeverity severity)
        {
            switch (severity)
            {
            case ConsoleSeverity::Warning: return GetEditorColor(EditorColor::Warning);
            case ConsoleSeverity::Error: return GetEditorColor(EditorColor::Error);
            case ConsoleSeverity::Info:
            default: return GetEditorColor(EditorColor::TextMuted);
            }
        }

        std::string FormatTime(std::chrono::system_clock::time_point time)
        {
            const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
            std::tm local{};
#ifdef _WIN32
            localtime_s(&local, &seconds);
#else
            localtime_r(&seconds, &local);
#endif
            char buffer[16];
            std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
            return buffer;
        }

        std::string_view FirstLine(std::string_view text)
        {
            const auto end = text.find_first_of("\r\n");
            return end == std::string_view::npos ? text : text.substr(0, end);
        }

        std::string CopyText(const ConsoleEntry &entry, bool withDetails)
        {
            std::string text = "[" + std::string(ConsoleSeverityLabel(entry.severity)) + "] " + entry.text;
            if (withDetails && !entry.details.empty())
                text += "\n" + entry.details;
            return text;
        }

        // A toolbar toggle drawn like the editor's other icon buttons, tinted while on.
        bool ToolbarToggle(const char *label, bool *value, const char *tooltip, const ImVec4 *onTextColor = nullptr)
        {
            const bool on = *value;
            if (on)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, GetEditorColor(EditorColor::Selection));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GetEditorColor(EditorColor::SelectionHovered));
            }
            ImGui::PushStyleColor(ImGuiCol_Text, on ? (onTextColor ? *onTextColor : GetEditorColor(EditorColor::Text))
                                                    : GetEditorColor(EditorColor::TextMuted));
            const bool pressed = ImGui::Button(label);
            ImGui::PopStyleColor(on ? 3 : 1);
            if (pressed)
                *value = !*value;
            if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", tooltip);
            return pressed;
        }

        std::string SourceSettingKey(ConsoleSource source)
        {
            return "console.show" + std::string(ConsoleSourceLabel(source));
        }
    }

    ConsolePanel::ConsolePanel(const PanelConfig &config) : Panel(config)
    {
        const auto &settings = EditorSettings::Get();
        m_showSeverity[0] = settings.GetBool("console.showInfo", true);
        m_showSeverity[1] = settings.GetBool("console.showWarnings", true);
        m_showSeverity[2] = settings.GetBool("console.showErrors", true);
        for (std::size_t i = 0; i < kSources.size(); ++i)
            m_showSource[i] = settings.GetBool(SourceSettingKey(kSources[i]), true);
        m_collapse = settings.GetBool("console.collapse", false);
        m_autoScroll = settings.GetBool("console.autoScroll", true);
    }

    void ConsolePanel::SaveSettings() const
    {
        auto &settings = EditorSettings::Get();
        settings.SetBool("console.showInfo", m_showSeverity[0]);
        settings.SetBool("console.showWarnings", m_showSeverity[1]);
        settings.SetBool("console.showErrors", m_showSeverity[2]);
        for (std::size_t i = 0; i < kSources.size(); ++i)
            settings.SetBool(SourceSettingKey(kSources[i]), m_showSource[i]);
        settings.SetBool("console.collapse", m_collapse);
        settings.SetBool("console.autoScroll", m_autoScroll);
    }

    bool ConsolePanel::PassesFilter(const ConsoleEntry &entry) const
    {
        if (!m_showSeverity[static_cast<std::size_t>(entry.severity)] || !m_showSource[static_cast<std::size_t>(entry.source)])
            return false;
        if (m_filterBuffer[0] == '\0')
            return true;
        const auto needle = Lower(m_filterBuffer.data());
        return Lower(entry.text).find(needle) != std::string::npos ||
               Lower(ConsoleSourceLabel(entry.source)).find(needle) != std::string::npos;
    }

    void ConsolePanel::RebuildVisibleEntries()
    {
        if (m_collapse)
            m_collapsed = CollapseConsoleEntries(m_entries);
        else
            m_collapsed.clear();
        const auto &source = m_collapse ? m_collapsed : m_entries;
        m_visible.clear();
        for (std::size_t i = 0; i < source.size(); ++i)
            if (PassesFilter(source[i]))
                m_visible.push_back(i);
        m_visibleDirty = false;
    }

    const ConsoleEntry *ConsolePanel::FindSelected() const
    {
        if (m_selectedSequence == 0)
            return nullptr;
        const auto &source = m_collapse ? m_collapsed : m_entries;
        const auto it = std::find_if(source.begin(), source.end(), [&](const ConsoleEntry &entry) { return entry.sequence == m_selectedSequence; });
        return it == source.end() ? nullptr : &*it;
    }

    void ConsolePanel::RenderToolbar(const std::array<uint64_t, 3> &counts)
    {
        auto &shell = EditorShell::GetInstance();
        bool changed = false;

        if (ImGui::Button("Clear"))
        {
            shell.ClearConsoleMessages();
            m_selectedSequence = 0;
        }
        ImGui::SameLine();
        changed |= ToolbarToggle((std::string(icons::Compress) + "##collapse").c_str(), &m_collapse,
                                 "Collapse: show each distinct message once, with how many times it occurred");
        ImGui::SameLine();
        bool pauseOnError = EditorSettings::Get().GetBool(kPauseOnScriptErrorKey, false);
        const ImVec4 errorColor = GetEditorColor(EditorColor::Error);
        if (ToolbarToggle((std::string(icons::Pause) + "  Error Pause").c_str(), &pauseOnError,
                          "Pause play mode when a script throws an exception", &errorColor))
            EditorSettings::Get().SetBool(kPauseOnScriptErrorKey, pauseOnError);
        ImGui::SameLine();
        changed |= ToolbarToggle("Auto Scroll", &m_autoScroll, "Keep the newest message in view");

        ImGui::SameLine();
        if (ImGui::Button("Sources"))
            ImGui::OpenPopup("##ConsoleSources");
        if (ImGui::BeginPopup("##ConsoleSources"))
        {
            for (std::size_t i = 0; i < kSources.size(); ++i)
                if (ImGui::Checkbox(ConsoleSourceLabel(kSources[i]), &m_showSource[i]))
                    changed = true;
            ImGui::EndPopup();
        }

        // Right-aligned: search, then the severity toggles with their counts.
        const auto countLabel = [&](const char *icon, std::size_t index)
        {
            const auto count = counts[index];
            return std::string(icon) + " " + (count > 999 ? std::string("999+") : std::to_string(count)) + "##severity" + std::to_string(index);
        };
        const std::array<std::string, 3> labels{countLabel(icons::Info, 0), countLabel(icons::Warning, 1), countLabel(icons::Error, 2)};
        const auto &style = ImGui::GetStyle();
        float togglesWidth = 0.0f;
        for (const auto &label : labels)
            togglesWidth += ImGui::CalcTextSize(label.c_str(), nullptr, true).x + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
        const float searchWidth = std::clamp(ImGui::GetContentRegionAvail().x - togglesWidth - 8.0f, 80.0f, 260.0f);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - togglesWidth - searchWidth));
        ImGui::SetNextItemWidth(searchWidth);
        if (ImGui::InputTextWithHint("##ConsoleFilter", (std::string(icons::Search) + "  Search").c_str(), m_filterBuffer.data(), m_filterBuffer.size()))
            m_visibleDirty = true;
        const std::array<const char *, 3> tooltips{"Show messages", "Show warnings", "Show errors"};
        const std::array<ImVec4, 3> colors{GetEditorColor(EditorColor::Text), GetEditorColor(EditorColor::Warning), errorColor};
        for (std::size_t i = 0; i < labels.size(); ++i)
        {
            ImGui::SameLine();
            changed |= ToolbarToggle(labels[i].c_str(), &m_showSeverity[i], tooltips[i], &colors[i]);
        }

        if (changed)
        {
            m_visibleDirty = true;
            SaveSettings();
        }
    }

    void ConsolePanel::RenderEntries(float height)
    {
        auto &shell = EditorShell::GetInstance();
        ImGui::BeginChild("ConsoleMessages", ImVec2(0.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
        const auto &source = m_collapse ? m_collapsed : m_entries;

        constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX;
        if (ImGui::BeginTable("##ConsoleTable", 4, tableFlags))
        {
            ImGui::TableSetupColumn("##severity", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 1.2f);
            ImGui::TableSetupColumn("##time", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("00:00:00").x);
            ImGui::TableSetupColumn("##message", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##count", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("9999").x);

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(m_visible.size()));
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const auto &entry = source[m_visible[static_cast<std::size_t>(row)]];
                    ImGui::PushID(static_cast<int>(entry.sequence));
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    const bool selected = entry.sequence == m_selectedSequence;
                    if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        m_selectedSequence = entry.sequence;
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            shell.NavigateToConsoleEntry(entry);
                    }
                    if (ImGui::BeginPopupContextItem("##entryMenu"))
                    {
                        m_selectedSequence = entry.sequence;
                        if (ImGui::MenuItem("Copy"))
                            ImGui::SetClipboardText(CopyText(entry, false).c_str());
                        if (ImGui::MenuItem("Copy With Details", nullptr, false, !entry.details.empty()))
                            ImGui::SetClipboardText(CopyText(entry, true).c_str());
                        ImGui::Separator();
                        if (ImGui::MenuItem("Open Source", nullptr, false, entry.location.IsSet()))
                            shell.NavigateToConsoleEntry(ConsoleEntry{.location = entry.location});
                        if (ImGui::MenuItem("Select Entity", nullptr, false, entry.entityId != 0))
                            shell.NavigateToConsoleEntry(ConsoleEntry{.entityId = entry.entityId});
                        if (ImGui::MenuItem("Show Asset", nullptr, false, !entry.asset.empty()))
                            shell.NavigateToConsoleEntry(ConsoleEntry{.asset = entry.asset});
                        ImGui::EndPopup();
                    }
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::TextColored(SeverityColor(entry.severity), "%s", SeverityIcon(entry.severity));

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled("%s", FormatTime(entry.time).c_str());

                    ImGui::TableSetColumnIndex(2);
                    if (entry.source != ConsoleSource::Editor)
                    {
                        ImGui::TextDisabled("%s", ConsoleSourceLabel(entry.source));
                        ImGui::SameLine();
                    }
                    const auto line = FirstLine(entry.text);
                    if (entry.severity == ConsoleSeverity::Info)
                        ImGui::TextUnformatted(line.data(), line.data() + line.size());
                    else
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, SeverityColor(entry.severity));
                        ImGui::TextUnformatted(line.data(), line.data() + line.size());
                        ImGui::PopStyleColor();
                    }

                    ImGui::TableSetColumnIndex(3);
                    if (entry.repeatCount > 1)
                        ImGui::TextDisabled("%llu", static_cast<unsigned long long>(entry.repeatCount));
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }

        if (m_scrollToBottom || (m_autoScroll && wasAtBottom))
            ImGui::SetScrollHereY(1.0f);
        m_scrollToBottom = false;
        ImGui::EndChild();
    }

    void ConsolePanel::RenderDetails()
    {
        const auto *entry = FindSelected();
        if (entry == nullptr)
        {
            ImGui::TextDisabled("Select a message to see its details. Double-click opens its source, entity or asset.");
            return;
        }

        auto &shell = EditorShell::GetInstance();
        bool needsSeparator = false;
        const auto action = [&](const char *label, ConsoleEntry target)
        {
            if (needsSeparator)
                ImGui::SameLine();
            if (ImGui::SmallButton(label))
                shell.NavigateToConsoleEntry(target);
            needsSeparator = true;
        };
        if (entry->location.IsSet())
        {
            const auto label = std::string(icons::OpenExternal) + "  " + entry->location.file.filename().string() + ":" +
                               std::to_string(entry->location.line);
            action(label.c_str(), ConsoleEntry{.location = entry->location});
        }
        if (entry->entityId != 0)
            action((std::string(icons::Crosshairs) + "  Select Entity").c_str(), ConsoleEntry{.entityId = entry->entityId});
        if (!entry->asset.empty())
            action((std::string(icons::FolderOpen) + "  Show Asset").c_str(), ConsoleEntry{.asset = entry->asset});
        if (needsSeparator)
            ImGui::SameLine();
        if (ImGui::SmallButton((std::string(icons::Copy) + "  Copy").c_str()))
            ImGui::SetClipboardText(CopyText(*entry, true).c_str());

        // Read-only text box so the message and stack trace can be selected and copied.
        std::string body = entry->text;
        if (!entry->details.empty() && entry->details != entry->text)
            body += "\n\n" + entry->details;
        ImGui::InputTextMultiline("##ConsoleDetails", body.data(), body.size() + 1, ImVec2(-FLT_MIN, -FLT_MIN),
                                  ImGuiInputTextFlags_ReadOnly);
    }

    void ConsolePanel::Render()
    {
        auto &shell = EditorShell::GetInstance();
        const auto &log = shell.GetConsoleLog();
        if (log.CopyIfChanged(m_revision, m_entries))
            m_visibleDirty = true;
        if (m_visibleDirty)
            RebuildVisibleEntries();

        RenderToolbar(log.CountBySeverity());
        if (m_visibleDirty)
            RebuildVisibleEntries();
        ImGui::Separator();

        // Message list above, details below, with a draggable divider.
        const float available = ImGui::GetContentRegionAvail().y;
        const float splitter = 6.0f;
        const float minimum = ImGui::GetFrameHeight() * 2.0f;
        m_detailsHeight = std::clamp(m_detailsHeight, minimum, std::max(minimum, available - minimum - splitter));
        RenderEntries(std::max(minimum, available - m_detailsHeight - splitter));

        ImGui::InvisibleButton("##ConsoleSplitter", ImVec2(-FLT_MIN, splitter));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive())
            m_detailsHeight -= ImGui::GetIO().MouseDelta.y;
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const float y = (min.y + max.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), GetEditorColorU32(EditorColor::Border));

        ImGui::BeginChild("ConsoleDetails", ImVec2(0.0f, 0.0f));
        RenderDetails();
        ImGui::EndChild();

        // Ctrl+C copies the selected message while the Console has focus and no text field is active.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
            ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
        {
            if (const auto *entry = FindSelected())
                ImGui::SetClipboardText(CopyText(*entry, true).c_str());
        }
    }
}
