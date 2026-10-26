#include "PlutoGE/render/NativeUIPreview.h"

#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/render/UIRhiRenderer.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/UISystem.h"
#include "PlutoGE/scene/ui/UIEngineServices.h"
#include "PlutoGE/ui/text/FontLibrary.h"

#include <vector>

namespace PlutoGE::render
{
    NativeUIPreview::NativeUIPreview() : m_ui(std::make_unique<scene::UISystem>())
    {
        m_ui->SetRectDriversEnabled(false);
    }

    NativeUIPreview::~NativeUIPreview() = default;

    void NativeUIPreview::Shutdown()
    {
        m_renderer.reset();
        m_target = {};
        m_device = nullptr;
        m_width = m_height = 0;
    }

    bool NativeUIPreview::Render(scene::Scene &scene, rhi::IRenderDevice &device, int width, int height,
                                 const glm::vec4 &background)
    {
        if (width <= 0 || height <= 0)
            return false;
        if (!m_renderer || m_device != &device)
        {
            Shutdown();
            const ShaderArtifactLibrary shaders;
            m_renderer = std::make_unique<UIRhiRenderer>(device, shaders.Load("NativeUI", "vertex"), shaders.Load("NativeUI", "fragment"));
            m_renderer->SetTextureResolver([](const std::string &reference) { return scene::LoadUITexture(reference); });
            m_device = &device;
        }
        if (!*m_renderer)
            return false;
        if (!m_target || width != m_width || height != m_height)
        {
            m_target = rhi::Texture(device, device.CreateTexture({.width = static_cast<std::uint32_t>(width),
                                                                  .height = static_cast<std::uint32_t>(height),
                                                                  .format = rhi::Format::R8G8B8A8Unorm,
                                                                  .usage = rhi::TextureUsage::ColorAttachment,
                                                                  .debugName = "Native UI preview",
                                                                  .sampled = true,
                                                                  .mipLevels = 1}));
            if (!m_target)
                return false;
            m_width = width;
            m_height = height;
        }

        const auto &canvases = m_ui->BuildDrawLists(scene, glm::vec2(static_cast<float>(width), static_cast<float>(height)));
        std::vector<UIRhiRenderer::DrawItem> items;
        for (const auto &canvas : canvases)
            if (!canvas.drawList.Empty())
                items.push_back({&canvas.drawList, canvas.entity ? canvas.entity->GetID() : 0u});

        auto &commands = device.GetImmediateContext();
        try
        {
            commands.BeginFrame("Native UI preview");
            rhi::RenderingInfo clear;
            clear.colorAttachments = {m_target.Get()};
            clear.width = static_cast<std::uint32_t>(width);
            clear.height = static_cast<std::uint32_t>(height);
            clear.clearDepth = false;
            for (int channel = 0; channel < 4; ++channel)
                clear.clearColorValue[channel] = background[channel];
            commands.BeginRendering(clear);
            commands.EndRendering();
            m_renderer->Render(m_target.Get(), static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), commands,
                               items, ui::FontLibrary::Get().GetAtlas());
            commands.Submit();
        }
        catch (...)
        {
            commands.RecoverInterruptedFrame();
            throw;
        }
        m_renderer->CollectUnused();
        return true;
    }
}
