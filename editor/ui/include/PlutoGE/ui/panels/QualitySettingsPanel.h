#pragma once
#include "PlutoGE/ui/panels/Panel.h"
#include "PlutoGE/ui/quality/QualityTierSession.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::ui
{
    // Edits the project's Low/Medium/High/Ultra quality tiers side by side. The rows come from
    // GetQualityTierFields(); editing, undo, order checks and preview live in QualityTierSession.
    class QualitySettingsPanel : public Panel
    {
    public:
        // `preview` shows a tier in the Scene viewport (a disabled quality returns it to scene settings).
        QualitySettingsPanel(const PanelConfig &config, QualityPreviewHandler preview)
            : Panel(config), m_preview(std::move(preview)) {}
        void Render() override;
        void OnProjectChanged() override;

    private:
        QualityTierSession *EnsureSession(assets::Project &project);
        void HandleShortcuts();
        void RenderToolbar(bool editable);
        void RenderPreviewBar();
        void RenderTierTable(bool editable);
        void RenderTierHeader(std::size_t tier, bool editable);
        void RenderFieldRow(const QualityTierField &field, bool editable);
        void RenderFieldCell(std::size_t tier, const QualityTierField &field, bool editable);
        void RenderFieldMenu(std::size_t tier, const QualityTierField &field);
        void RenderIssues();
        void RenderScopeNotes();
        [[nodiscard]] bool IsVisible(const QualityTierField &field) const;
        [[nodiscard]] const QualityTierIssue *FindIssue(std::size_t tier, const QualityTierField &field) const;

        QualityPreviewHandler m_preview;
        std::unique_ptr<QualityTierSession> m_session;
        const assets::Project *m_sessionProject = nullptr;
        std::array<char, 64> m_filter{};
        bool m_modifiedOnly = false;
        // Issues of the session revision they were found for.
        std::vector<QualityTierIssue> m_issues;
        std::uint64_t m_issuesRevision = 0;
    };
}
