#pragma once

#include "PlutoGE/render/GraphicsQualityPresets.h"
#include "PlutoGE/ui/DocumentHistory.h"
#include "PlutoGE/ui/quality/QualityTierFields.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::ui
{
    // Where tier definitions live and where a preview shows. The project implementation writes the manifest, installs
    // the definitions in the runtime and marks the project dirty; tests implement their own.
    class QualityTierStorage
    {
    public:
        virtual ~QualityTierStorage() = default;
        [[nodiscard]] virtual render::GraphicsQualityPresets Read() const = 0;
        virtual bool Write(const render::GraphicsQualityPresets &presets) = 0;
        // Shows a tier in the preview viewport; null returns it to scene settings.
        virtual void Preview(const render::GraphicsQuality *quality) {}
    };

    using QualityPreviewHandler = std::function<void(const render::GraphicsQuality &)>;
    [[nodiscard]] std::unique_ptr<QualityTierStorage> MakeProjectQualityTierStorage(assets::Project &project,
                                                                                    QualityPreviewHandler preview);

    // A lower tier that costs more than the next enabled tier above it, for one field.
    struct QualityTierIssue
    {
        const QualityTierField *field = nullptr;
        std::size_t lower = 0;
        std::size_t higher = 0;
    };

    // Bit per tier, for operations that apply to several.
    using QualityTierMask = std::uint32_t;
    inline constexpr QualityTierMask AllQualityTiers = (1u << QualityTierCount) - 1u;
    [[nodiscard]] constexpr QualityTierMask QualityTierBit(std::size_t tier) { return 1u << tier; }
    [[nodiscard]] constexpr QualityTierMask QualityTiersAbove(std::size_t tier) { return AllQualityTiers & ~((2u << tier) - 1u); }
    [[nodiscard]] constexpr QualityTierMask QualityTiersBelow(std::size_t tier) { return (1u << tier) - 1u; }

    // The project's four tier definitions as an editable document: every change is written through to storage at
    // once (saved with the project), can be undone, and refreshes the live preview.
    class QualityTierSession
    {
    public:
        explicit QualityTierSession(std::unique_ptr<QualityTierStorage> storage);
        // Ends any preview, so the viewport returns to scene settings.
        ~QualityTierSession();
        QualityTierSession(const QualityTierSession &) = delete;
        QualityTierSession &operator=(const QualityTierSession &) = delete;

        [[nodiscard]] const render::GraphicsQualityPresets &GetPresets() const { return m_presets; }
        [[nodiscard]] const render::GraphicsQuality &GetTier(std::size_t tier) const { return m_presets.tiers.at(tier); }
        [[nodiscard]] static render::GraphicsQuality EngineDefault(std::size_t tier);

        // Edits are rejected (false) when they leave a tier invalid; a no-op edit records nothing. Modify, SetEnabled
        // and SetField group by held control (EndFrame); resets and copies are always one step each.
        bool Modify(std::size_t tier, const std::function<void(render::GraphicsQuality &)> &edit);
        bool SetEnabled(std::size_t tier, bool enabled);
        bool SetField(std::size_t tier, const QualityTierField &field, double value);
        bool ResetField(std::size_t tier, const QualityTierField &field);
        bool ResetTier(std::size_t tier);
        bool ResetAll();
        // Copies a whole tier (including whether it applies) over another.
        bool CopyTier(std::size_t from, std::size_t to);
        // Copies one field of `from` to every tier in `targets`, as one undo step.
        bool CopyField(const QualityTierField &field, std::size_t from, QualityTierMask targets);

        [[nodiscard]] bool IsModified(std::size_t tier, const QualityTierField &field) const;
        [[nodiscard]] bool IsModified(std::size_t tier) const { return GetTier(tier) != EngineDefault(tier); }
        [[nodiscard]] bool IsModified() const;

        // Enabled tiers whose field costs more than the next enabled tier's (Low allowing what Medium skips).
        [[nodiscard]] std::vector<QualityTierIssue> FindOrderIssues() const;

        // Changes made while a control is held become one undo step until EndFrame sees it released.
        void EndFrame(bool controlHeld) { m_history.EndFrame(controlHeld); }
        [[nodiscard]] bool CanUndo() const { return m_history.CanUndo(); }
        [[nodiscard]] bool CanRedo() const { return m_history.CanRedo(); }
        bool Undo();
        bool Redo();

        // While a tier is previewed, edits to it show at once. Nullopt returns the viewport to scene settings.
        void SetPreviewTier(std::optional<std::size_t> tier);
        [[nodiscard]] std::optional<std::size_t> GetPreviewTier() const { return m_previewTier; }

        // Increments on every change, for views that cache derived data.
        [[nodiscard]] std::uint64_t GetRevision() const { return m_revision; }

    private:
        bool Commit(const render::GraphicsQualityPresets &next);
        // A discrete action (reset, copy): always its own undo step, even within a held control's group.
        bool CommitStep(const render::GraphicsQualityPresets &next);
        bool ModifyStep(std::size_t tier, const std::function<void(render::GraphicsQuality &)> &edit);
        void Changed();
        void RefreshPreview();

        std::unique_ptr<QualityTierStorage> m_storage;
        render::GraphicsQualityPresets m_presets;
        DocumentHistory<render::GraphicsQualityPresets> m_history;
        std::optional<std::size_t> m_previewTier;
        std::optional<render::GraphicsQuality> m_shown; // last quality sent to the preview
        std::uint64_t m_revision = 1;
    };
}
