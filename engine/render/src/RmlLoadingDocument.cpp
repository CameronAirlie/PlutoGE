#include "PlutoGE/render/RmlLoadingDocument.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/render/RmlUiRhiRenderer.h"
#include <RmlUi/Core.h>

namespace PlutoGE::render
{
    RmlLoadingDocument::RmlLoadingDocument() = default;
    RmlLoadingDocument::~RmlLoadingDocument() { Shutdown(); }

    void RmlLoadingDocument::Shutdown()
    {
        if (m_context)
        {
            if (m_document) RmlUiRuntime::Get().m_elementLookup.Invalidate(m_document);
            Rml::RemoveContext(m_context->GetName());
        }
        // Context removal alone retains the renderer in RmlUi's manager cache.
        // Release it while the concrete interface is still alive.
        if (m_renderer && RmlUiRuntime::Get().IsInitialized()) Rml::ReleaseRenderManagers();
        m_document = nullptr;
        m_context = nullptr;
        m_renderer.reset();
    }

    bool RmlLoadingDocument::Render(rhi::TextureHandle target, int width, int height)
    {
        if (!m_context || !m_document || width <= 0 || height <= 0) return false;
        m_context->SetDimensions({width, height});
        m_renderer->SetViewport(width, height);
        m_context->Update();
        m_renderer->BeginFrame(target);
        try
        {
            m_context->Render();
            m_renderer->EndFrame();
        }
        catch (...) { m_renderer->CancelFrame(); throw; }
        return true;
    }

}
