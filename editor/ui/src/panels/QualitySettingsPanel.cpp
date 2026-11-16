#include "PlutoGE/ui/panels/QualitySettingsPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <string>
#include <string_view>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr int TableColumns = 1 + static_cast<int>(QualityTierCount);

        bool ContainsIgnoringCase(std::string_view text, std::string_view needle)
        {
            if (needle.empty()) return true;
            return std::ranges::search(text, needle, [](char a, char b) {
                       return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                   }).begin() != text.end();
        }

        std::string TierLabel(std::size_t tier) { return std::string(QualityTierName(tier)); }

        std::string IssueText(const QualityTierIssue &issue, const QualityTierSession &session)
        {
            const auto &field = *issue.field;
            return TierLabel(issue.lower) + " costs more than " + TierLabel(issue.higher) + " in " + std::string(field.label) +
                " (" + field.Format(field.Get(session.GetTier(issue.lower))) + " vs " +
                field.Format(field.Get(session.GetTier(issue.higher))) + ")";
        }

        // Draws the editing control for one cell; true when the value changed this frame.
        bool FieldControl(const QualityTierField &field, double &value)
        {
            ImGui::SetNextItemWidth(-FLT_MIN);
            switch (field.kind)
            {
            case QualityFieldKind::Toggle:
            {
                bool on = value >= 0.5;
                // Centre the checkbox in its cell.
                const float offset = (ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight()) * 0.5f;
                if (offset > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
                if (!ImGui::Checkbox("##value", &on)) return false;
                value = on ? 1.0 : 0.0;
                return true;
            }
            case QualityFieldKind::Resolution:
            {
                bool changed = false;
                if (ImGui::BeginCombo("##value", field.Format(value).c_str()))
                {
                    for (double option = field.min; option <= field.max; option *= 2.0)
                        if (ImGui::Selectable(field.Format(option).c_str(), option == value))
                        {
                            value = option;
                            changed = true;
                        }
                    ImGui::EndCombo();
                }
                return changed;
            }
            case QualityFieldKind::Count:
            {
                int count = static_cast<int>(value);
                if (!ImGui::SliderInt("##value", &count, static_cast<int>(field.min), static_cast<int>(field.max), "%d",
                                      ImGuiSliderFlags_AlwaysClamp))
                    return false;
                value = count;
                return true;
            }
            case QualityFieldKind::Distance:
            {
                float distance = static_cast<float>(value);
                if (!ImGui::SliderFloat("##value", &distance, static_cast<float>(field.min), static_cast<float>(field.max),
                                        distance < 10.0f ? "%.1f m" : "%.0f m",
                                        ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
                    return false;
                value = distance;
                return true;
            }
            case QualityFieldKind::Scale:
            {
                float percent = static_cast<float>(value * 100.0);
                if (!ImGui::SliderFloat("##value", &percent, static_cast<float>(field.min * 100.0),
                                        static_cast<float>(field.max * 100.0), "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
                    return false;
                value = percent / 100.0;
                return true;
            }
            }
            return false;
        }
    }

    void QualitySettingsPanel::OnProjectChanged()
    {
        m_session.reset();
        m_sessionProject = nullptr;
        m_issuesRevision = 0;
    }

    QualityTierSession *QualitySettingsPanel::EnsureSession(assets::Project &project)
    {
        if (!m_session || m_sessionProject != &project)
        {
            m_session.reset(); // ends the old session's preview first
            m_session = std::make_unique<QualityTierSession>(MakeProjectQualityTierStorage(project, m_preview));
            m_sessionProject = &project;
            m_issuesRevision = 0;
        }
        return m_session.get();
    }

    void QualitySettingsPanel::Render()
    {
        auto *project = EditorShell::GetInstance().GetProject();
        if (!project)
        {
            OnProjectChanged();
            ImGui::TextDisabled("Open a project to edit its quality tiers.");
            return;
        }
        auto &session = *EnsureSession(*project);
        if (m_issuesRevision != session.GetRevision())
        {
            m_issues = session.FindOrderIssues();
            m_issuesRevision = session.GetRevision();
        }
        const bool editable = !core::Engine::GetInstance().IsRuntimeRunning();
        if (editable) HandleShortcuts();

        RenderToolbar(editable);
        RenderPreviewBar();
        RenderTierTable(editable);
        RenderIssues();
        RenderScopeNotes();
        session.EndFrame(ImGui::IsAnyItemActive());
    }

    void QualitySettingsPanel::HandleShortcuts()
    {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput)
            return;
        const auto &io = ImGui::GetIO();
        if (!io.KeyCtrl && !io.KeySuper)
            return;
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
            io.KeyShift ? m_session->Redo() : m_session->Undo();
        else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
            m_session->Redo();
    }

    void QualitySettingsPanel::RenderToolbar(bool editable)
    {
        auto &session = *m_session;
        widgets::PageTitle("Quality Tiers",
                           "Ceilings each player tier applies over scene-authored settings. A tier never enables what a "
                           "scene disables. Saved with the project (File > Save Project, Ctrl+S).");
        if (!editable)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Warning));
            ImGui::TextWrapped("%s Stop Play to edit tier definitions. Previewing still works.", icons::Warning);
            ImGui::PopStyleColor();
        }

        ImGui::BeginDisabled(!editable || !session.CanUndo());
        if (widgets::IconButton("##Undo", icons::ArrowLeft, "Undo (Ctrl+Z)")) session.Undo();
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::BeginDisabled(!editable || !session.CanRedo());
        if (widgets::IconButton("##Redo", icons::ArrowRight, "Redo (Ctrl+Y)")) session.Redo();
        ImGui::EndDisabled();

        ImGui::SameLine();
        const float menuWidth = ImGui::GetFrameHeight();
        const float filterWidth = std::max(120.0f, ImGui::GetContentRegionAvail().x - menuWidth -
                                                       ImGui::CalcTextSize("Modified only").x - ImGui::GetFrameHeight() * 2.5f);
        ImGui::SetNextItemWidth(filterWidth);
        ImGui::InputTextWithHint("##Filter", (std::string(icons::Search) + "  Filter settings").c_str(), m_filter.data(),
                                 m_filter.size());
        ImGui::SameLine();
        ImGui::Checkbox("Modified only", &m_modifiedOnly);
        ImGui::SetItemTooltip("Show only settings that differ from the engine defaults in some tier.");

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - menuWidth));
        if (widgets::IconButton("##More", icons::Ellipsis, "More actions")) ImGui::OpenPopup("QualityTierActions");
        if (ImGui::BeginPopup("QualityTierActions"))
        {
            ImGui::BeginDisabled(!editable || !session.IsModified());
            if (ImGui::MenuItem("Reset all tiers to engine defaults")) session.ResetAll();
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
    }

    void QualitySettingsPanel::RenderPreviewBar()
    {
        auto &session = *m_session;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Scene viewport preview");
        ImGui::SetItemTooltip("Applies a tier to the Scene viewport only, updating as you edit it. Use the Game viewport's "
                              "Quality popup to preview there.");
        ImGui::SameLine();
        constexpr std::array<const char *, QualityTierCount + 1> labels{"Off", "Low", "Medium", "High", "Ultra"};
        int selected = session.GetPreviewTier() ? static_cast<int>(*session.GetPreviewTier()) + 1 : 0;
        if (widgets::SegmentedControl("##Preview", &selected, labels))
            session.SetPreviewTier(selected == 0 ? std::nullopt : std::optional<std::size_t>(selected - 1));
        if (const auto tier = session.GetPreviewTier(); tier && !session.GetTier(*tier).enabled)
            ImGui::TextDisabled("%s does not apply overrides, so the viewport shows scene settings.", TierLabel(*tier).c_str());
        ImGui::Spacing();
    }

    bool QualitySettingsPanel::IsVisible(const QualityTierField &field) const
    {
        const std::string_view filter(m_filter.data());
        if (!ContainsIgnoringCase(field.label, filter) && !ContainsIgnoringCase(QualityFieldGroupName(field.group), filter))
            return false;
        if (!m_modifiedOnly) return true;
        for (std::size_t tier = 0; tier < QualityTierCount; ++tier)
            if (m_session->IsModified(tier, field)) return true;
        return false;
    }

    const QualityTierIssue *QualitySettingsPanel::FindIssue(std::size_t tier, const QualityTierField &field) const
    {
        const auto found = std::ranges::find_if(m_issues, [&](const QualityTierIssue &issue) {
            return issue.field == &field && (issue.lower == tier || issue.higher == tier);
        });
        return found == m_issues.end() ? nullptr : &*found;
    }

    void QualitySettingsPanel::RenderTierTable(bool editable)
    {
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuter |
                                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp |
                                          ImGuiTableFlags_PadOuterX;
        const float height = std::max(ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.5f,
                                      ImGui::GetFrameHeightWithSpacing() * 8.0f);
        if (!ImGui::BeginTable("QualityTiers", TableColumns, flags, ImVec2(0.0f, height)))
            return;
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        for (std::size_t tier = 0; tier < QualityTierCount; ++tier)
            ImGui::TableSetupColumn(TierLabel(tier).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);

        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableSetColumnIndex(0);
        ImGui::TableHeader("Setting");
        for (std::size_t tier = 0; tier < QualityTierCount; ++tier)
        {
            ImGui::TableSetColumnIndex(static_cast<int>(tier) + 1);
            RenderTierHeader(tier, editable);
        }

        // Whether each tier applies at all.
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Apply tier");
        ImGui::SetItemTooltip("Off: the tier leaves every scene-authored setting as it is.");
        ImGui::BeginDisabled(!editable);
        for (std::size_t tier = 0; tier < QualityTierCount; ++tier)
        {
            ImGui::TableSetColumnIndex(static_cast<int>(tier) + 1);
            ImGui::PushID(static_cast<int>(tier));
            bool enabled = m_session->GetTier(tier).enabled;
            const float offset = (ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 1.3f) * 0.5f;
            if (offset > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
            if (widgets::ToggleSwitch("##Enabled", &enabled)) m_session->SetEnabled(tier, enabled);
            ImGui::PopID();
        }
        ImGui::EndDisabled();

        const bool filtering = m_filter[0] != '\0' || m_modifiedOnly;
        bool anyVisible = false;
        for (std::size_t group = 0; group < QualityFieldGroupCount; ++group)
        {
            const auto groupId = static_cast<QualityFieldGroup>(group);
            const auto fields = GetQualityTierFields();
            if (std::ranges::none_of(fields, [&](const auto &field) { return field.group == groupId && IsVisible(field); }))
                continue;
            anyVisible = true;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Accent));
            if (filtering) ImGui::SetNextItemOpen(true);
            const bool open = ImGui::TreeNodeEx(std::string(QualityFieldGroupName(groupId)).c_str(),
                                                ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen |
                                                    ImGuiTreeNodeFlags_NoTreePushOnOpen);
            ImGui::PopStyleColor();
            if (!open) continue;
            for (const auto &field : fields)
                if (field.group == groupId && IsVisible(field)) RenderFieldRow(field, editable);
        }
        if (!anyVisible)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("No settings match.");
        }
        ImGui::EndTable();
    }

    void QualitySettingsPanel::RenderTierHeader(std::size_t tier, bool editable)
    {
        auto &session = *m_session;
        std::string label = TierLabel(tier);
        if (session.GetPreviewTier() == tier) label += std::string("  ") + icons::View;
        if (session.IsModified(tier)) label += "  *";
        ImGui::PushID(static_cast<int>(tier));
        ImGui::TableHeader((label + "##Header").c_str());
        ImGui::SetItemTooltip("%s%s\nRight-click for tier actions.", TierLabel(tier).c_str(),
                              session.IsModified(tier) ? " (differs from engine defaults)" : "");
        if (ImGui::BeginPopupContextItem("TierActions"))
        {
            const bool previewing = session.GetPreviewTier() == tier;
            if (ImGui::MenuItem(previewing ? "Stop preview" : "Preview in Scene viewport"))
                session.SetPreviewTier(previewing ? std::nullopt : std::optional(tier));
            ImGui::Separator();
            ImGui::BeginDisabled(!editable);
            if (ImGui::BeginMenu("Copy settings from"))
            {
                for (std::size_t from = 0; from < QualityTierCount; ++from)
                    if (from != tier && ImGui::MenuItem(TierLabel(from).c_str())) session.CopyTier(from, tier);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Reset to engine defaults", nullptr, false, session.IsModified(tier))) session.ResetTier(tier);
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    void QualitySettingsPanel::RenderFieldRow(const QualityTierField &field, bool editable)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(field.id.data(), field.id.data() + field.id.size());
        ImGui::Indent(ImGui::GetStyle().IndentSpacing * 0.5f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(field.label.data(), field.label.data() + field.label.size());
        ImGui::SetItemTooltip("%.*s", static_cast<int>(field.description.size()), field.description.data());
        ImGui::Unindent(ImGui::GetStyle().IndentSpacing * 0.5f);
        for (std::size_t tier = 0; tier < QualityTierCount; ++tier)
        {
            ImGui::TableSetColumnIndex(static_cast<int>(tier) + 1);
            RenderFieldCell(tier, field, editable);
        }
        ImGui::PopID();
    }

    void QualitySettingsPanel::RenderFieldCell(std::size_t tier, const QualityTierField &field, bool editable)
    {
        auto &session = *m_session;
        const auto &quality = session.GetTier(tier);
        const auto *issue = FindIssue(tier, field);
        const bool modified = session.IsModified(tier, field);
        if (issue)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, GetEditorColorU32(EditorColor::Warning, 0.22f));
        else if (modified)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, GetEditorColorU32(EditorColor::Accent, 0.14f));

        ImGui::PushID(static_cast<int>(tier));
        ImGui::BeginDisabled(!editable || !quality.enabled);
        double value = field.Get(quality);
        if (FieldControl(field, value)) session.SetField(tier, field, value);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        {
            ImGui::BeginTooltip();
            ImGui::Text("%s - %.*s",TierLabel(tier).c_str(), static_cast<int>(field.label.size()), field.label.data());
            ImGui::TextDisabled("Engine default: %s", field.Format(field.Get(QualityTierSession::EngineDefault(tier))).c_str());
            if (!quality.enabled) ImGui::TextDisabled("This tier does not apply overrides.");
            if (issue)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Warning));
                ImGui::TextUnformatted(IssueText(*issue, session).c_str());
                ImGui::PopStyleColor();
            }
            ImGui::TextDisabled("Right-click for more.");
            ImGui::EndTooltip();
        }
        if (editable) RenderFieldMenu(tier, field);
        ImGui::PopID();
    }

    void QualitySettingsPanel::RenderFieldMenu(std::size_t tier, const QualityTierField &field)
    {
        if (!ImGui::BeginPopupContextItem("FieldActions", ImGuiPopupFlags_MouseButtonRight))
            return;
        auto &session = *m_session;
        const auto fallback = field.Format(field.Get(QualityTierSession::EngineDefault(tier)));
        if (ImGui::MenuItem(("Reset to default (" + fallback + ")").c_str(), nullptr, false, session.IsModified(tier, field)))
            session.ResetField(tier, field);
        ImGui::Separator();
        const auto value = field.Format(field.Get(session.GetTier(tier)));
        ImGui::TextDisabled("Apply %s to", value.c_str());
        if (ImGui::MenuItem("Higher tiers", nullptr, false, QualityTiersAbove(tier) != 0))
            session.CopyField(field, tier, QualityTiersAbove(tier));
        if (ImGui::MenuItem("Lower tiers", nullptr, false, QualityTiersBelow(tier) != 0))
            session.CopyField(field, tier, QualityTiersBelow(tier));
        if (ImGui::MenuItem("All tiers")) session.CopyField(field, tier, AllQualityTiers);
        ImGui::EndPopup();
    }

    void QualitySettingsPanel::RenderIssues()
    {
        if (m_issues.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Success));
            ImGui::TextUnformatted(icons::Check);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("Tiers are in order: each costs no more than the one above it.");
            return;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Warning));
        const std::string title = std::string(icons::Warning) + "  " + std::to_string(m_issues.size()) +
                                  (m_issues.size() == 1 ? " tier out of order" : " tiers out of order") + "###Issues";
        const bool open = ImGui::CollapsingHeader(title.c_str());
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("A lower tier should never cost more than a higher one. Highlighted cells show where.");
        if (!open) return;
        for (const auto &issue : m_issues)
            ImGui::BulletText("%s", IssueText(issue, *m_session).c_str());
    }

    void QualitySettingsPanel::RenderScopeNotes()
    {
        if (!ImGui::CollapsingHeader("What tiers do not control"))
            return;
        ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
        ImGui::TextWrapped("Display resolution, VSync, upscaling, texture residency, particles, non-foliage mesh LOD, "
                           "local-light shadow maps and virtual-shadow budgets are independent. Allowed effects keep "
                           "their scene-authored strength, resolution and sample counts. A selected tier is a snapshot: "
                           "reapply it in game after editing its definition.");
        ImGui::PopStyleColor();
    }
}
