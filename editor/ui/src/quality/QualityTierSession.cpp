#include "PlutoGE/ui/quality/QualityTierSession.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/render/RhiRenderService.h"
#include "PlutoGE/ui/EditorShell.h"

namespace PlutoGE::ui
{
    namespace
    {
        class ProjectQualityTierStorage final : public QualityTierStorage
        {
        public:
            ProjectQualityTierStorage(assets::Project &project, QualityPreviewHandler preview)
                : m_project(project), m_preview(std::move(preview)) {}

            render::GraphicsQualityPresets Read() const override { return m_project.GetManifest().graphicsQualityPresets; }

            bool Write(const render::GraphicsQualityPresets &presets) override
            {
                if (!core::Engine::GetInstance().GetRhiRenderService().SetGraphicsQualityPresets(presets))
                    return false;
                m_project.GetManifest().graphicsQualityPresets = presets;
                EditorShell::GetInstance().MarkProjectDirty();
                return true;
            }

            void Preview(const render::GraphicsQuality *quality) override
            {
                if (m_preview) m_preview(quality ? *quality : render::GraphicsQuality{});
            }

        private:
            assets::Project &m_project;
            QualityPreviewHandler m_preview;
        };
    }

    std::unique_ptr<QualityTierStorage> MakeProjectQualityTierStorage(assets::Project &project, QualityPreviewHandler preview)
    {
        return std::make_unique<ProjectQualityTierStorage>(project, std::move(preview));
    }

    QualityTierSession::QualityTierSession(std::unique_ptr<QualityTierStorage> storage)
        : m_storage(std::move(storage)), m_presets(m_storage->Read())
    {
    }

    QualityTierSession::~QualityTierSession()
    {
        if (m_shown) m_storage->Preview(nullptr);
    }

    render::GraphicsQuality QualityTierSession::EngineDefault(std::size_t tier)
    {
        return render::GraphicsQuality::FromPreset(static_cast<render::GraphicsPreset>(tier));
    }

    bool QualityTierSession::Commit(const render::GraphicsQualityPresets &next)
    {
        if (next == m_presets || !next.IsValid()) return false;
        const auto before = m_presets;
        if (!m_storage->Write(next)) return false;
        m_history.Record(before);
        m_presets = next;
        Changed();
        return true;
    }

    bool QualityTierSession::CommitStep(const render::GraphicsQualityPresets &next)
    {
        m_history.CloseGroup();
        const bool changed = Commit(next);
        m_history.CloseGroup();
        return changed;
    }

    bool QualityTierSession::ModifyStep(std::size_t tier, const std::function<void(render::GraphicsQuality &)> &edit)
    {
        if (tier >= QualityTierCount) return false;
        auto next = m_presets;
        edit(next.tiers[tier]);
        return CommitStep(next);
    }

    void QualityTierSession::Changed()
    {
        ++m_revision;
        RefreshPreview();
    }

    bool QualityTierSession::Modify(std::size_t tier, const std::function<void(render::GraphicsQuality &)> &edit)
    {
        if (tier >= QualityTierCount) return false;
        auto next = m_presets;
        edit(next.tiers[tier]);
        return Commit(next);
    }

    bool QualityTierSession::SetEnabled(std::size_t tier, bool enabled)
    {
        return Modify(tier, [&](auto &quality) { quality.enabled = enabled; });
    }

    bool QualityTierSession::SetField(std::size_t tier, const QualityTierField &field, double value)
    {
        return Modify(tier, [&](auto &quality) { field.Set(quality, value); });
    }

    bool QualityTierSession::ResetField(std::size_t tier, const QualityTierField &field)
    {
        return ModifyStep(tier, [&](auto &quality) { field.Set(quality, field.Get(EngineDefault(tier))); });
    }

    bool QualityTierSession::ResetTier(std::size_t tier)
    {
        return ModifyStep(tier, [&](auto &quality) { quality = EngineDefault(tier); });
    }

    bool QualityTierSession::ResetAll()
    {
        return CommitStep(render::GraphicsQualityPresets{});
    }

    bool QualityTierSession::CopyTier(std::size_t from, std::size_t to)
    {
        if (from >= QualityTierCount || from == to) return false;
        return ModifyStep(to, [&](auto &quality) { quality = m_presets.tiers[from]; });
    }

    bool QualityTierSession::CopyField(const QualityTierField &field, std::size_t from, QualityTierMask targets)
    {
        if (from >= QualityTierCount) return false;
        auto next = m_presets;
        const double value = field.Get(m_presets.tiers[from]);
        for (std::size_t tier = 0; tier < QualityTierCount; ++tier)
            if (tier != from && (targets & QualityTierBit(tier))) field.Set(next.tiers[tier], value);
        return CommitStep(next);
    }

    bool QualityTierSession::IsModified(std::size_t tier, const QualityTierField &field) const
    {
        return !field.Equal(GetTier(tier), EngineDefault(tier));
    }

    bool QualityTierSession::IsModified() const
    {
        return m_presets != render::GraphicsQualityPresets{};
    }

    std::vector<QualityTierIssue> QualityTierSession::FindOrderIssues() const
    {
        std::vector<QualityTierIssue> issues;
        for (const auto &field : GetQualityTierFields())
        {
            std::optional<std::size_t> lower;
            for (std::size_t tier = 0; tier < QualityTierCount; ++tier)
            {
                if (!m_presets.tiers[tier].enabled) continue;
                if (lower && field.Cost(m_presets.tiers[*lower]) > field.Cost(m_presets.tiers[tier]))
                    issues.push_back({&field, *lower, tier});
                lower = tier;
            }
        }
        return issues;
    }

    bool QualityTierSession::Undo()
    {
        auto restored = m_presets;
        if (!m_history.Undo(restored)) return false;
        if (!m_storage->Write(restored))
        {
            m_history.Redo(restored); // put the history back as it was
            return false;
        }
        m_presets = restored;
        Changed();
        return true;
    }

    bool QualityTierSession::Redo()
    {
        auto restored = m_presets;
        if (!m_history.Redo(restored)) return false;
        if (!m_storage->Write(restored))
        {
            m_history.Undo(restored);
            return false;
        }
        m_presets = restored;
        Changed();
        return true;
    }

    void QualityTierSession::SetPreviewTier(std::optional<std::size_t> tier)
    {
        if (tier && *tier >= QualityTierCount) return;
        if (tier == m_previewTier) return;
        m_previewTier = tier;
        if (m_previewTier) RefreshPreview();
        else if (m_shown)
        {
            m_shown.reset();
            m_storage->Preview(nullptr);
        }
    }

    void QualityTierSession::RefreshPreview()
    {
        // Only a change to what the viewport shows is pushed: each push resets the viewport's temporal history.
        if (!m_previewTier || m_shown == m_presets.tiers[*m_previewTier]) return;
        m_shown = m_presets.tiers[*m_previewTier];
        m_storage->Preview(&*m_shown);
    }
}
