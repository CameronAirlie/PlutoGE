#include "PlutoGE/ui/ProjectLauncher.h"

#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr float kSidebarWidth = 240.0f;
        constexpr float kMaxContentWidth = 920.0f;
        constexpr float kRowHeight = 64.0f;
        constexpr float kTemplateCardHeight = 104.0f;
        constexpr float kTemplateCardMinWidth = 250.0f;

        ImVec4 Mix(const ImVec4 &a, const ImVec4 &b, float t)
        {
            return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
        }

        ImU32 U32(const ImVec4 &color) { return ImGui::ColorConvertFloat4ToU32(color); }

        // Matches widgets::BeginCard so launcher cards read as the same surface.
        ImVec4 CardColor() { return Mix(GetEditorColor(EditorColor::Panel), GetEditorColor(EditorColor::Surface), 0.55f); }

        float BaseFontSize()
        {
            const float base = ImGui::GetStyle().FontSizeBase;
            return base > 0.0f ? base : ImGui::GetFontSize();
        }

        bool ContainsInsensitive(const std::string &text, const std::string &needle)
        {
            return std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b)
                               { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }) !=
                   text.end();
        }

        void MutedText(const char *text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        }

        void DrawTextClipped(ImDrawList *drawList, float fontSize, ImVec2 position, float maxWidth, ImU32 color, const std::string &text)
        {
            ImFont *font = ImGui::GetFont();
            if (font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str()).x <= maxWidth)
            {
                drawList->AddText(font, fontSize, position, color, text.c_str());
                return;
            }
            const float ellipsis = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, "...").x;
            std::size_t length = text.size();
            while (length > 0 && font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str(), text.c_str() + length).x + ellipsis > maxWidth)
                --length;
            while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80)
                --length;
            drawList->AddText(font, fontSize, position, color, (text.substr(0, length) + "...").c_str());
        }

        // A stable, muted hue per project so tiles are recognisable at a glance.
        ImVec4 ProjectTint(const std::string &name)
        {
            static constexpr ImVec4 kPalette[] = {
                {0.34f, 0.55f, 0.85f, 1.0f}, {0.56f, 0.45f, 0.86f, 1.0f}, {0.27f, 0.68f, 0.60f, 1.0f},
                {0.86f, 0.55f, 0.30f, 1.0f}, {0.80f, 0.40f, 0.50f, 1.0f}, {0.45f, 0.65f, 0.35f, 1.0f},
            };
            const std::size_t hash = std::hash<std::string>{}(name);
            return kPalette[hash % std::size(kPalette)];
        }

        std::string Initials(const std::string &name)
        {
            std::string initials;
            bool startOfWord = true;
            for (const char character : name)
            {
                const bool alphanumeric = std::isalnum(static_cast<unsigned char>(character)) != 0;
                const bool upper = std::isupper(static_cast<unsigned char>(character)) != 0;
                if (alphanumeric && (startOfWord || (upper && !initials.empty() && initials.size() < 2)))
                    initials += static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
                startOfWord = !alphanumeric;
                if (initials.size() == 2)
                    break;
            }
            return initials.empty() ? std::string("?") : initials;
        }

        std::string RelativeTime(const std::filesystem::path &path)
        {
            std::error_code error;
            const auto written = std::filesystem::last_write_time(path, error);
            if (error)
                return {};
            const auto time = std::chrono::clock_cast<std::chrono::system_clock>(written);
            const auto age = std::chrono::system_clock::now() - time;
            const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(age).count();
            const auto plural = [](long long count, const char *unit)
            { return std::to_string(count) + " " + unit + (count == 1 ? "" : "s") + " ago"; };
            if (minutes < 1)
                return "Modified just now";
            if (minutes < 60)
                return "Modified " + plural(minutes, "minute");
            if (minutes < 60 * 24)
                return "Modified " + plural(minutes / 60, "hour");
            if (minutes < 60 * 24 * 2)
                return "Modified yesterday";
            if (minutes < 60 * 24 * 30)
                return "Modified " + plural(minutes / (60 * 24), "day");
            const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
            std::tm local{};
#ifdef _WIN32
            localtime_s(&local, &seconds);
#else
            localtime_r(&seconds, &local);
#endif
            char buffer[32]{};
            std::strftime(buffer, sizeof(buffer), "%d %b %Y", &local);
            return std::string("Modified ") + buffer;
        }

        const char *TemplateIcon(const std::string &id)
        {
            if (id.find("third-person") != std::string::npos)
                return icons::AnimationAsset;
            if (id.find("fps") != std::string::npos)
                return icons::Crosshairs;
            if (id.find("viewer") != std::string::npos)
                return icons::View;
            if (id.find("vehicle") != std::string::npos)
                return icons::Car;
            if (id.find("empty") != std::string::npos)
                return icons::File;
            return icons::PrefabAsset;
        }

        // Rounded tile with a centred glyph, used for project initials and template icons.
        void DrawTile(ImDrawList *drawList, ImVec2 min, float size, const ImVec4 &tint, bool solid, const char *text, float fontSize)
        {
            const ImVec2 max(min.x + size, min.y + size);
            drawList->AddRectFilled(min, max, U32(solid ? tint : ImVec4(tint.x, tint.y, tint.z, 0.16f)), size * 0.24f);
            const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
            drawList->AddText(ImGui::GetFont(), fontSize, ImVec2(min.x + (size - textSize.x) * 0.5f, min.y + (size - textSize.y) * 0.5f),
                              solid ? IM_COL32_WHITE : U32(tint), text);
        }

        // A page title with actions aligned to its right edge.
        void PageHeader(const char *title, const char *subtitle, const std::function<void()> &actions, float actionsWidth)
        {
            const ImVec2 start = ImGui::GetCursorPos();
            const float width = ImGui::GetContentRegionAvail().x;
            ImGui::PushTextWrapPos(start.x + std::max(120.0f, width - actionsWidth - 24.0f));
            widgets::PageTitle(title, subtitle);
            ImGui::PopTextWrapPos();
            const ImVec2 end = ImGui::GetCursorPos();
            if (actions)
            {
                ImGui::SetCursorPos(ImVec2(start.x + width - actionsWidth, start.y + 4.0f));
                actions();
            }
            ImGui::SetCursorPos(end);
        }

        float ButtonWidth(const char *label)
        {
            return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        }
    }

    ProjectLauncherActions ProjectLauncher::Draw(std::span<const std::filesystem::path> recentProjects,
                                                 std::span<const ProjectTemplateOption> templates,
                                                 const std::string &errorMessage)
    {
        ProjectLauncherActions actions;
        const auto *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, GetEditorColor(EditorColor::Panel));
        ImGui::Begin("Project Launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        const float height = ImGui::GetContentRegionAvail().y;
        DrawSidebar(actions, height);

        ImGui::SameLine(0.0f, 0.0f);
        const ImVec2 dividerTop = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(dividerTop, ImVec2(dividerTop.x, dividerTop.y + height), GetEditorColorU32(EditorColor::Border));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(40.0f, 32.0f));
        ImGui::BeginChild("##LauncherPage", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();

        // Centre a readable column on wide screens.
        const float available = ImGui::GetContentRegionAvail().x;
        const float columnWidth = std::min(available, kMaxContentWidth);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - columnWidth) * 0.5f);
        ImGui::BeginChild("##LauncherColumn", ImVec2(columnWidth, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

        if (!errorMessage.empty())
        {
            const ImVec4 warning = GetEditorColor(EditorColor::Error);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(warning.x, warning.y, warning.z, 0.12f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(warning.x, warning.y, warning.z, 0.45f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
            ImGui::BeginChild("##LauncherError", ImVec2(0.0f, 0.0f),
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::TextColored(warning, "%s", icons::Warning);
            ImGui::SameLine();
            ImGui::TextWrapped("%s", errorMessage.c_str());
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
            ImGui::Dummy(ImVec2(0.0f, 12.0f));
        }

        if (m_page == Page::Projects)
            DrawProjectsPage(recentProjects, actions);
        else
            DrawNewProjectPage(templates, actions);

        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::End();
        return actions;
    }

    void ProjectLauncher::DrawSidebar(ProjectLauncherActions &actions, float height)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, GetEditorColor(EditorColor::Background));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 22.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f));
        ImGui::BeginChild("##LauncherNav", ImVec2(kSidebarWidth, height), ImGuiChildFlags_AlwaysUseWindowPadding);

        // Brand: an accent mark beside the product name.
        auto *drawList = ImGui::GetWindowDrawList();
        const float markSize = 36.0f;
        const ImVec2 markMin = ImVec2(ImGui::GetCursorScreenPos().x + 8.0f, ImGui::GetCursorScreenPos().y);
        drawList->AddRectFilled(markMin, ImVec2(markMin.x + markSize, markMin.y + markSize), GetEditorColorU32(EditorColor::Accent), 9.0f);
        drawList->AddCircle(ImVec2(markMin.x + markSize * 0.5f, markMin.y + markSize * 0.5f), markSize * 0.24f,
                            GetEditorColorU32(EditorColor::AccentText), 0, 2.5f);
        drawList->AddCircleFilled(ImVec2(markMin.x + markSize * 0.74f, markMin.y + markSize * 0.28f), markSize * 0.08f,
                                  GetEditorColorU32(EditorColor::AccentText));
        const float textX = markMin.x + markSize + 12.0f;
        ImGui::PushFont(nullptr, BaseFontSize() * 1.35f);
        drawList->AddText(ImVec2(textX, markMin.y - 2.0f), GetEditorColorU32(EditorColor::Text), "PlutoGE");
        ImGui::PopFont();
        drawList->AddText(ImVec2(textX, markMin.y + BaseFontSize() * 1.35f), GetEditorColorU32(EditorColor::TextMuted), "Game Engine");
        ImGui::Dummy(ImVec2(0.0f, markSize + 26.0f));

        ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
        ImGui::TextUnformatted("START");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (widgets::NavItem(icons::History, "Projects", m_page == Page::Projects))
            m_page = Page::Projects;
        if (widgets::NavItem(icons::Plus, "New Project", m_page == Page::NewProject))
            m_page = Page::NewProject;
        if (widgets::NavItem(icons::FolderOpen, "Open Project...", false))
            actions.browse = true;

        // Quit sits at the bottom, away from the everyday actions.
        const float quitHeight = std::round(ImGui::GetFrameHeight() * 1.35f);
        const float bottom = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - quitHeight;
        if (ImGui::GetCursorPosY() < bottom)
            ImGui::SetCursorPosY(bottom);
        if (widgets::NavItem(icons::Quit, "Quit", false))
            actions.quit = true;

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    void ProjectLauncher::DrawProjectsPage(std::span<const std::filesystem::path> recentProjects, ProjectLauncherActions &actions)
    {
        const float actionsWidth = ButtonWidth("Open...") + ButtonWidth("New Project") + ImGui::GetStyle().ItemSpacing.x;
        PageHeader("Projects", "Pick up where you left off, or open a project from disk.",
                   [&]
                   {
                       if (ImGui::Button("Open..."))
                           actions.browse = true;
                       ImGui::SameLine();
                       if (widgets::PrimaryButton("New Project"))
                           m_page = Page::NewProject;
                   },
                   actionsWidth);

        if (recentProjects.empty())
        {
            widgets::BeginCard("##NoProjects");
            ImGui::Dummy(ImVec2(0.0f, 18.0f));
            const float width = ImGui::GetContentRegionAvail().x;
            const auto centred = [width](float itemWidth) { ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - itemWidth) * 0.5f); };
            ImGui::PushFont(nullptr, BaseFontSize() * 2.4f);
            centred(ImGui::CalcTextSize(icons::FolderOpen).x);
            ImGui::TextColored(GetEditorColor(EditorColor::TextMuted), "%s", icons::FolderOpen);
            ImGui::PopFont();
            ImGui::PushFont(nullptr, BaseFontSize() * 1.12f);
            centred(ImGui::CalcTextSize("No recent projects").x);
            ImGui::TextUnformatted("No recent projects");
            ImGui::PopFont();
            const char *hint = "Create a project from a template, or open one you already have.";
            centred(ImGui::CalcTextSize(hint).x);
            ImGui::TextColored(GetEditorColor(EditorColor::TextMuted), "%s", hint);
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            centred(ButtonWidth("Open...") + ButtonWidth("New Project") + ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::Button("Open..."))
                actions.browse = true;
            ImGui::SameLine();
            if (widgets::PrimaryButton("New Project"))
                m_page = Page::NewProject;
            ImGui::Dummy(ImVec2(0.0f, 18.0f));
            widgets::EndCard();
            return;
        }

        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##SearchProjects", "Search recent projects", &m_search);
        widgets::SectionHeading("Recent");

        auto *drawList = ImGui::GetWindowDrawList();
        const float fontSize = ImGui::GetFontSize();
        int shown = 0;
        for (const auto &path : recentProjects)
        {
            const std::string name = path.stem().string();
            const std::string location = path.parent_path().string();
            if (!m_search.empty() && !ContainsInsensitive(name, m_search) && !ContainsInsensitive(location, m_search))
                continue;
            ++shown;

            std::error_code error;
            const bool exists = std::filesystem::is_regular_file(path, error);
            ImGui::PushID(path.string().c_str());

            const ImVec2 min = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const ImVec2 max(min.x + width, min.y + kRowHeight);
            ImGui::SetNextItemAllowOverlap();
            const bool clicked = ImGui::InvisibleButton("##Row", ImVec2(width, kRowHeight));
            const bool hovered = ImGui::IsItemHovered();
            if (clicked && exists)
                actions.open = path;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("##ProjectMenu");
            if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s%s", path.string().c_str(), exists ? "" : "\nThis project file no longer exists.");

            const float rounding = 8.0f;
            if (hovered)
            {
                drawList->AddRectFilled(min, max, U32(CardColor()), rounding);
                drawList->AddRect(min, max, GetEditorColorU32(EditorColor::Border), rounding);
            }
            const float alpha = exists ? 1.0f : 0.5f;
            const float tileSize = 40.0f;
            const ImVec2 tile(min.x + 12.0f, min.y + (kRowHeight - tileSize) * 0.5f);
            ImVec4 tint = ProjectTint(name);
            tint.w = alpha;
            DrawTile(drawList, tile, tileSize, tint, true, Initials(name).c_str(), fontSize * 1.05f);

            // Right column: relative time, or a Missing pill, then the menu button.
            const float menuSize = ImGui::GetFrameHeight();
            const float rightEdge = max.x - 12.0f - menuSize - 8.0f;
            float rightColumn = 0.0f;
            if (exists)
            {
                const std::string modified = RelativeTime(path);
                rightColumn = ImGui::CalcTextSize(modified.c_str()).x;
                drawList->AddText(ImVec2(rightEdge - rightColumn, min.y + (kRowHeight - fontSize) * 0.5f),
                                  GetEditorColorU32(EditorColor::TextMuted), modified.c_str());
            }
            else
            {
                const char *label = "Missing";
                const ImVec2 textSize = ImGui::CalcTextSize(label);
                const ImVec2 pillMin(rightEdge - textSize.x - 16.0f, min.y + (kRowHeight - textSize.y) * 0.5f - 3.0f);
                const ImVec2 pillMax(rightEdge, pillMin.y + textSize.y + 6.0f);
                const ImVec4 warning = GetEditorColor(EditorColor::Warning);
                drawList->AddRectFilled(pillMin, pillMax, U32(ImVec4(warning.x, warning.y, warning.z, 0.18f)), (pillMax.y - pillMin.y) * 0.5f);
                drawList->AddText(ImVec2(pillMin.x + 8.0f, pillMin.y + 3.0f), U32(warning), label);
                rightColumn = pillMax.x - pillMin.x;
            }

            const float textX = tile.x + tileSize + 14.0f;
            const float textWidth = rightEdge - rightColumn - 24.0f - textX;
            const float nameSize = fontSize * 1.08f;
            const float blockTop = min.y + (kRowHeight - nameSize - fontSize - 4.0f) * 0.5f;
            DrawTextClipped(drawList, nameSize, ImVec2(textX, blockTop), textWidth, GetEditorColorU32(EditorColor::Text, alpha), name);
            DrawTextClipped(drawList, fontSize, ImVec2(textX, blockTop + nameSize + 4.0f), textWidth,
                            GetEditorColorU32(EditorColor::TextMuted, alpha), location);

            ImGui::SetCursorScreenPos(ImVec2(max.x - 12.0f - menuSize, min.y + (kRowHeight - menuSize) * 0.5f));
            if (widgets::IconButton("Menu", icons::Ellipsis, "More actions"))
                ImGui::OpenPopup("##ProjectMenu");
            if (ImGui::BeginPopup("##ProjectMenu"))
            {
                if (ImGui::MenuItem("Open", nullptr, false, exists))
                    actions.open = path;
                if (ImGui::MenuItem("Show in File Browser", nullptr, false, exists))
                    actions.reveal = path;
                ImGui::Separator();
                if (ImGui::MenuItem("Remove from List"))
                    actions.forget = path;
                ImGui::EndPopup();
            }
            ImGui::SetCursorScreenPos(ImVec2(min.x, max.y + 4.0f));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
            ImGui::PopID();
        }
        if (shown == 0)
            MutedText(("No recent projects match \"" + m_search + "\".").c_str());
    }

    void ProjectLauncher::DrawNewProjectPage(std::span<const ProjectTemplateOption> templates, ProjectLauncherActions &actions)
    {
        const auto selectable = [](const ProjectTemplateOption &option) { return option.available; };
        const bool selectedValid = std::any_of(templates.begin(), templates.end(), [this, &selectable](const ProjectTemplateOption &option)
                                               { return option.id == m_selectedTemplate && selectable(option); });
        if (!selectedValid)
        {
            const auto first = std::find_if(templates.begin(), templates.end(), selectable);
            m_selectedTemplate = first == templates.end() ? std::string{} : first->id;
        }

        widgets::PageTitle("New Project", "Choose a starting point. You will pick where to save the project next.");
        widgets::SectionHeading("Templates");

        auto *drawList = ImGui::GetWindowDrawList();
        const ImGuiStyle &style = ImGui::GetStyle();
        const float gap = 12.0f;
        const float width = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, static_cast<int>((width + gap) / (kTemplateCardMinWidth + gap)));
        const float cardWidth = (width - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
        const float fontSize = ImGui::GetFontSize();
        bool createRequested = false;

        for (std::size_t index = 0; index < templates.size(); ++index)
        {
            const auto &option = templates[index];
            ImGui::PushID(option.id.c_str());
            if (index % static_cast<std::size_t>(columns) != 0)
                ImGui::SameLine(0.0f, gap);
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + cardWidth, min.y + kTemplateCardHeight);
            ImGui::BeginDisabled(!option.available);
            const bool clicked = ImGui::InvisibleButton("##Template", ImVec2(cardWidth, kTemplateCardHeight));
            ImGui::EndDisabled();
            const bool hovered = ImGui::IsItemHovered();
            const bool selected = option.id == m_selectedTemplate;
            if (clicked)
                m_selectedTemplate = option.id;
            if (hovered && option.available && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                createRequested = true;

            const float rounding = 8.0f;
            const float alpha = option.available ? 1.0f : 0.45f;
            ImVec4 fill = CardColor();
            if (selected)
                fill = Mix(fill, GetEditorColor(EditorColor::Selection), 0.6f);
            drawList->AddRectFilled(min, max, U32(fill), rounding);
            drawList->AddRect(min, max,
                              selected ? GetEditorColorU32(EditorColor::Accent)
                                       : GetEditorColorU32(hovered ? EditorColor::TextMuted : EditorColor::Border, hovered ? 0.6f : 1.0f),
                              rounding, 0, selected ? 2.0f : 1.0f);

            const float tileSize = 38.0f;
            const ImVec2 tile(min.x + 16.0f, min.y + 16.0f);
            ImVec4 accent = GetEditorColor(EditorColor::Accent);
            accent.w = alpha;
            DrawTile(drawList, tile, tileSize, accent, false, TemplateIcon(option.id), fontSize * 1.15f);

            const float textX = tile.x + tileSize + 14.0f;
            const float textWidth = max.x - 16.0f - textX;
            DrawTextClipped(drawList, fontSize * 1.05f, ImVec2(textX, tile.y + 1.0f), textWidth,
                            GetEditorColorU32(EditorColor::Text, alpha), option.label);
            const ImVec2 descriptionPos(textX, tile.y + fontSize * 1.05f + 6.0f);
            drawList->PushClipRect(descriptionPos, ImVec2(max.x - 16.0f, max.y - 12.0f), true);
            drawList->AddText(ImGui::GetFont(), fontSize, descriptionPos, GetEditorColorU32(EditorColor::TextMuted, alpha),
                              option.description.c_str(), nullptr, textWidth);
            drawList->PopClipRect();
            if (selected)
            {
                const ImVec2 checkSize = ImGui::CalcTextSize(icons::Check);
                drawList->AddText(ImVec2(max.x - 14.0f - checkSize.x, min.y + 12.0f), GetEditorColorU32(EditorColor::Accent), icons::Check);
            }
            ImGui::PopID();
        }

        ImGui::Dummy(ImVec2(0.0f, 20.0f));
        const float buttonsWidth = ButtonWidth("Cancel") + ButtonWidth("Create Project...") + style.ItemSpacing.x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonsWidth);
        if (ImGui::Button("Cancel"))
            m_page = Page::Projects;
        ImGui::SameLine();
        ImGui::BeginDisabled(m_selectedTemplate.empty());
        if (widgets::PrimaryButton("Create Project...") || (createRequested && !m_selectedTemplate.empty()))
            actions.createTemplateId = m_selectedTemplate;
        ImGui::EndDisabled();
    }
}
