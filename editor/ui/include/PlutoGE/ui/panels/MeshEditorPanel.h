#pragma once

#include "PlutoGE/ui/meshedit/MeshEditorViews.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <memory>
#include <optional>
#include <string>

namespace PlutoGE::ui
{
    class MeshAssetSession;

    // Editor for .plutomesh assets: an overview, material slots, the humanoid rig map (with a diagram of the
    // mesh's joints to assign them from) and level-of-detail and import settings. Follows the mesh EditorShell
    // opens; unsaved changes are offered for saving first.
    class MeshEditorPanel : public Panel
    {
    public:
        explicit MeshEditorPanel(const PanelConfig &config);
        ~MeshEditorPanel() override;

        void Render() override;
        void OnProjectChanged() override;

        // Shows an already-open session, e.g. one with custom storage. Replaces the current one without asking.
        void Open(std::unique_ptr<MeshAssetSession> session);
        [[nodiscard]] bool HasUnsavedChanges() const;
        [[nodiscard]] const MeshAssetSession *GetSession() const { return m_session.get(); }
        void SetPage(meshedit::MeshEditorPage page) { m_page = page; }

    private:
        void FollowShell();
        void OpenReference(const std::string &reference);
        void DrawEmptyState();
        void DrawToolbar();
        void DrawNavigation(float height);
        void DrawPage();
        void DrawSavePrompt();
        void HandleShortcuts();
        void Save();
        void Revert();
        void ResetViews();

        std::unique_ptr<MeshAssetSession> m_session;
        std::string m_reference;         // asset shown, empty for a session opened directly
        std::string m_followedReference; // EditorShell's active mesh when last seen
        std::optional<std::string> m_pendingReference;
        bool m_openSavePrompt = false;
        meshedit::MeshEditorPage m_page = meshedit::MeshEditorPage::Overview;
        meshedit::HumanoidRigAnalysisCache m_rigAnalysis;
        meshedit::HumanoidRigViewState m_rig;
        std::string m_error;
    };
}
