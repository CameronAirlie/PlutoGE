#pragma once
#include "PlutoGE/render/rhi/Types.h"
#include <memory>
#include <string>

namespace Rml { class Context; class ElementDocument; }
namespace PlutoGE::render
{
    class RmlUiRuntime;
    class RmlUiRhiRenderer;
    // A noninteractive document with its own context and renderer. Global RmlUi
    // lifetime remains owned by RmlUiRuntime; scene resets leave this untouched.
    class RmlLoadingDocument
    {
    public:
        ~RmlLoadingDocument();
        RmlLoadingDocument(const RmlLoadingDocument &) = delete;
        RmlLoadingDocument &operator=(const RmlLoadingDocument &) = delete;
        bool Render(rhi::TextureHandle target, int width, int height);
        Rml::ElementDocument *GetDocument() const { return m_document; }
    private:
        friend class RmlUiRuntime;
        RmlLoadingDocument();
        void Shutdown();
        std::unique_ptr<RmlUiRhiRenderer> m_renderer;
        Rml::Context *m_context = nullptr;
        Rml::ElementDocument *m_document = nullptr;
    };
}
