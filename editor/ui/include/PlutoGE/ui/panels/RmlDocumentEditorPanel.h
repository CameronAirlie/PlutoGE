#pragma once

#include "PlutoGE/ui/panels/Panel.h"
#include "PlutoGE/ui/EditorCompositor.h"
#include "PlutoGE/ui/RmlDocumentEditSession.h"
#include "PlutoGE/ui/RmlDocumentPreview.h"
#include <array>
#include <chrono>

namespace PlutoGE::ui
{
    class RmlDocumentEditorPanel final : public Panel
    {
    public:
        explicit RmlDocumentEditorPanel(PanelConfig config) : Panel(std::move(config)) {}
        void RequestOpen(const std::string &reference);
        void PreparePreview();
        void Render() override;
        void Shutdown() override;
        void OnProjectChanged() override;
        bool HasUnsavedChanges() const { return m_session.IsDirty(); }
        bool OwnsKeyboardFocus() const;
    private:
        void OpenPending();
        void ClearPreview();
        void Save();
        void RenderPendingPrompts();
        void RenderSourceTabs();
        void RenderPreviewPane();
        RmlDocumentEditSession m_session;
        RmlDocumentPreview m_preview;
        EditorTextureHandle m_texture;
        std::string m_pendingReference, m_error;
        std::array<char, 4096> m_openReference{};
        std::array<float, 4> m_background{0.12f, 0.14f, 0.18f, 1.0f};
        std::uint64_t m_seenRevision = 0, m_previewRevision = 0;
        std::chrono::steady_clock::time_point m_lastEdit{}, m_lastPoll{};
        int m_width = 1280, m_height = 720;
        float m_zoom = 0.6f;
        bool m_confirmOpen = false, m_confirmReload = false, m_confirmClose = false;
    };
}
