#pragma once

#include "PlutoGE/ui/FoliageTypeSession.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <memory>
#include <optional>
#include <string>

namespace PlutoGE::render
{
    class Mesh;
}

namespace PlutoGE::scene
{
    class Scene;
}

namespace PlutoGE::ui
{
    // Editor for .plutofoliage assets: what a plant draws, how it is placed, rendered, collided and moved by wind, with
    // the foliage types using it. Edits preview live on those types; saving keeps them. The settings themselves are the
    // sections the inspector shows for a type defined in the scene (FoliageTypeSections.h).
    class FoliageEditorPanel final : public Panel
    {
    public:
        explicit FoliageEditorPanel(const PanelConfig &config);
        ~FoliageEditorPanel() override;

        void Render() override;
        void Shutdown() override;
        void OnProjectChanged() override;

        // Shows the asset, offering to save unsaved changes to another one first.
        void OpenAsset(const std::string &reference);
        // Shows an already-open session, e.g. one with custom storage. Replaces the current one without asking.
        void Open(std::unique_ptr<FoliageTypeSession> session);
        [[nodiscard]] bool HasUnsavedChanges() const;

    private:
        void OpenReference(const std::string &reference);
        void CloseSession();
        void UpdatePreviewState();
        void UpdateMesh();
        void DrawEmptyState();
        void DrawToolbar();
        void DrawOverview();
        void DrawUsage();
        void DrawSettings();
        void DrawSavePrompt();
        void HandleShortcuts();
        bool Save();

        std::unique_ptr<FoliageTypeSession> m_session;
        std::string m_reference; // asset shown; empty for a session opened directly
        std::optional<std::string> m_pendingReference; // what the save prompt moves on to; empty closes
        bool m_openSavePrompt = false;
        bool m_closeRequested = false;
        std::string m_pickerReference;
        std::string m_error;
        std::string m_meshReference;
        const render::Mesh *m_mesh = nullptr;
        bool m_hasImpostor = false;
        const scene::Scene *m_previewedScene = nullptr;
    };
}
