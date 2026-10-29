#pragma once

#include "PlutoGE/ui/panels/Panel.h"
#include "PlutoGE/ui/postprocess/PostProcessViews.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    class PostProcessPresetSession;

    // Editor for .plutopostprocess presets: the effect stack grouped by pipeline stage, each effect's settings, and
    // the cameras using the preset. Edits preview live on those cameras; saving keeps them. Follows the preset
    // EditorShell opens; unsaved changes are offered for saving first.
    class PostProcessEditorPanel : public Panel
    {
    public:
        explicit PostProcessEditorPanel(const PanelConfig &config);
        ~PostProcessEditorPanel() override;

        void Render() override;
        void OnProjectChanged() override;

        // Shows an already-open session, e.g. one with custom storage. Replaces the current one without asking.
        void Open(std::unique_ptr<PostProcessPresetSession> session);
        [[nodiscard]] bool HasUnsavedChanges() const;
        [[nodiscard]] const PostProcessPresetSession *GetSession() const { return m_session.get(); }

    private:
        enum class Tab
        {
            Effects,
            Usage,
        };

        void FollowShell();
        void OpenReference(const std::string &reference);
        void DrawEmptyState();
        void DrawToolbar();
        void DrawProblems();
        void DrawUsage();
        void DrawSavePrompt();
        void HandleShortcuts();
        void Save();
        void ResetViews();

        std::unique_ptr<PostProcessPresetSession> m_session;
        std::string m_reference;         // asset shown, empty for a session opened directly
        std::string m_followedReference; // EditorShell's active preset when last seen
        std::optional<std::string> m_pendingReference;
        bool m_openSavePrompt = false;
        std::optional<Tab> m_requestedTab;
        postprocess::PostProcessStackAnalysis m_analysis;
        postprocess::EffectsViewState m_effects;
        std::string m_pickerReference;
        std::string m_error;
    };
}
