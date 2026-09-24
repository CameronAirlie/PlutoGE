#pragma once
#include "PlutoGE/ui/panels/Panel.h"
#include "PlutoGE/ui/EditorCompositor.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::core { class LoadingScreenSession; }
namespace PlutoGE::ui
{
    class LoadingScreenEditorPanel : public Panel
    {
    public:
        explicit LoadingScreenEditorPanel(const PanelConfig &config);
        ~LoadingScreenEditorPanel() override;
        void Render() override;
        // Called before the editor starts recording its frame.
        void PreparePreview();
        void Shutdown() override;
        void OnProjectChanged() override;
    private:
        void Load();
        void LoadSources();
        void Save();
        void Create();
        void ClearPreview();
        std::string m_reference, m_error;
        std::array<char, 4096> m_document{};
        std::array<char, 1025> m_controller{};
        std::array<char, 128> m_name{};
        std::vector<char> m_rml, m_rcss;
        std::unique_ptr<core::LoadingScreenSession> m_preview;
        EditorTextureHandle m_texture;
        bool m_dirty = false, m_previewEnabled = false, m_reloadPreview = false;
        bool m_sourcesValid = false;
        int m_stage = 1;
    };
}
