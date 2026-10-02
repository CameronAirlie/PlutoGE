#pragma once

#include "PlutoGE/render/rhi/Resource.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::core { class Engine; }
namespace PlutoGE::render { class RmlLoadingDocument; }
namespace PlutoGE::ui
{
    class RmlDocumentEditSession;

    // GPU/context lifetime is independent of the panel and source edit history.
    // Render must run before the host begins recording its editor frame.
    class RmlDocumentPreview
    {
    public:
        bool Rebuild(core::Engine &engine, const RmlDocumentEditSession &session);
        bool Render(core::Engine &engine, int width, int height, const std::array<float, 4> &background);
        void Reset();
        render::rhi::TextureHandle GetTexture() const { return m_target.Get(); }
        const std::vector<std::string> &GetDiagnostics() const { return m_diagnostics; }
        bool IsStale() const { return m_stale; }
    private:
        std::shared_ptr<render::RmlLoadingDocument> m_document;
        render::rhi::Texture m_target;
        int m_width = 0, m_height = 0;
        bool m_stale = false;
        std::vector<std::string> m_diagnostics;
    };
}
