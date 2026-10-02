#include "PlutoGE/ui/RmlDocumentPreview.h"
#include "PlutoGE/ui/RmlDocumentEditSession.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/RmlLoadingDocument.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include <algorithm>
#include <stdexcept>

namespace PlutoGE::ui
{
    bool RmlDocumentPreview::Rebuild(core::Engine &engine, const RmlDocumentEditSession &session)
    {
        m_diagnostics.clear();
        m_stale = true;
        try
        {
            if (session.GetBuffers().empty() || !engine.GetRenderDevice()) return false;
            auto next = render::RmlUiRuntime::Get().CreatePreviewDocument(
                session.GetBuffers().front().path.generic_string(), session.GetSourceOverlay(),
                engine.GetWindow(), *engine.GetRenderDevice(), m_diagnostics);
            if (!next) throw std::runtime_error("Cannot load document; previous preview retained.");
            m_document = std::move(next);
            m_stale = false;
            return true;
        }
        catch (const std::exception &error) { m_diagnostics.push_back(error.what()); return false; }
    }

    bool RmlDocumentPreview::Render(core::Engine &engine, int width, int height, const std::array<float, 4> &background)
    {
        auto *device = engine.GetRenderDevice();
        if (!m_document || !device || width <= 0 || height <= 0) return false;
        auto &commands = device->GetImmediateContext();
        try
        {
            render::rhi::Texture replacement;
            if (width != m_width || height != m_height || !m_target)
            {
                replacement = render::rhi::Texture(*device, device->CreateTexture({
                    .width = static_cast<unsigned>(width), .height = static_cast<unsigned>(height),
                    .format = render::rhi::Format::R8G8B8A8Unorm,
                    .usage = render::rhi::TextureUsage::ColorAttachment,
                    .debugName = "RML document preview", .sampled = true, .mipLevels = 1}));
                if (!replacement) throw std::runtime_error("Cannot allocate preview texture");
            }
            const auto target = replacement ? replacement.Get() : m_target.Get();
            commands.BeginFrame("RML preview background");
            render::rhi::RenderingInfo info;
            info.colorAttachments = {target};
            info.width = static_cast<unsigned>(width); info.height = static_cast<unsigned>(height);
            info.clearDepth = false;
            std::copy(background.begin(), background.end(), info.clearColorValue);
            commands.BeginRendering(info);
            commands.EndRendering();
            commands.Submit();
            if (!m_document->Render(target, width, height))
                throw std::runtime_error("Preview context is no longer available");
            // Publish a resized texture only after rendering succeeds. Otherwise
            // the panel's existing texture registration must remain valid.
            if (replacement) m_target = std::move(replacement);
            m_width = width;
            m_height = height;
            return true;
        }
        catch (const std::exception &error)
        {
            commands.RecoverInterruptedFrame();
            m_stale = true;
            if (m_diagnostics.empty() || m_diagnostics.back() != error.what()) m_diagnostics.push_back(error.what());
            return false;
        }
    }

    void RmlDocumentPreview::Reset()
    {
        m_document.reset(); m_target.Reset(); m_width = m_height = 0;
        m_diagnostics.clear(); m_stale = false;
    }
}
