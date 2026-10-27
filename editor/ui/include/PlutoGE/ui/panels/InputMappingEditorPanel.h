#pragma once

#include "PlutoGE/ui/input/InputMappingViews.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <memory>
#include <optional>
#include <string>

namespace PlutoGE::ui
{
    class InputMappingSession;

    // Editor for .plutoinput mappings: actions and their bindings, a report of
    // shared and free inputs, and live device readings. Follows the mapping
    // EditorShell opens; unsaved changes are offered for saving first.
    class InputMappingEditorPanel : public Panel
    {
    public:
        explicit InputMappingEditorPanel(const PanelConfig &config);
        ~InputMappingEditorPanel() override;

        void Render() override;
        void OnProjectChanged() override;

        // Shows an already-open session, e.g. one with custom storage.
        // Replaces the current one without asking.
        void Open(std::unique_ptr<InputMappingSession> session);
        [[nodiscard]] bool HasUnsavedChanges() const;
        [[nodiscard]] const InputMappingSession *GetSession() const { return m_session.get(); }

    private:
        enum class Tab
        {
            Actions,
            Overview,
            Devices,
        };

        void FollowShell();
        void OpenReference(const std::string &reference);
        void DrawEmptyState();
        void DrawToolbar();
        void DrawProblems();
        void DrawSavePrompt();
        void HandleShortcuts();
        void Save();

        std::unique_ptr<InputMappingSession> m_session;
        std::string m_reference;         // asset shown, empty for a session opened directly
        std::string m_followedReference; // EditorShell's active mapping when last seen
        std::optional<std::string> m_pendingReference;
        bool m_openSavePrompt = false;
        std::optional<Tab> m_requestedTab;
        input::InputMappingAnalysis m_analysis;
        input::ActionsViewState m_actions;
        std::string m_pickerReference;
        std::string m_error;
    };
}
