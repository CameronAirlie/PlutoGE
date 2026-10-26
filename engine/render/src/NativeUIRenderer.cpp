#include "PlutoGE/render/NativeUIRenderer.h"

#include "PlutoGE/core/CpuTrace.h"
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
    NativeUIRenderer::NativeUIRenderer() = default;
    NativeUIRenderer::~NativeUIRenderer() = default;

    void NativeUIRenderer::Shutdown()
    {
        m_renderer.reset();
        m_device = nullptr;
    }

    bool NativeUIRenderer::HasContent(const scene::Scene &scene)
    {
        return scene.HasNativeRuntimeUI();
    }

    void NativeUIRenderer::Render(const scene::Scene &scene, rhi::IRenderDevice &device, rhi::TextureHandle target,
                                  int width, int height, const glm::mat4 &view, const glm::mat4 &projection,
                                  bool manageSubmission, rhi::ICommandContext *commands)
    {
        core::CpuScope scope("Native UI render", core::CpuCategory::UI);
        if (!target || width <= 0 || height <= 0)
            return;
        if (!m_renderer || m_device != &device)
        {
            const ShaderArtifactLibrary shaders;
            m_renderer = std::make_unique<UIRhiRenderer>(device, shaders.Load("NativeUI", "vertex"),
                                                         shaders.Load("NativeUI", "fragment"));
            m_renderer->SetTextureResolver([](const std::string &reference) { return scene::LoadUITexture(reference); });
            m_device = &device;
        }
        if (!*m_renderer)
            return;

        // Rendering refreshes the UI system's per-frame layout and paint cache;
        // the scene itself is not modified.
        auto &uiSystem = const_cast<scene::Scene &>(scene).GetUISystem();
        const scene::UICameraInfo camera{view, projection};
        const auto &canvases = uiSystem.BuildDrawLists(const_cast<scene::Scene &>(scene),
                                                 glm::vec2(static_cast<float>(width), static_cast<float>(height)), &camera);
        std::vector<UIRhiRenderer::DrawItem> items;
        items.reserve(canvases.size());
        for (const auto &canvas : canvases)
            if (!canvas.drawList.Empty())
                items.push_back({&canvas.drawList, canvas.entity ? canvas.entity->GetID() : 0u});
        if (items.empty())
            return;

        const bool immediate = commands == nullptr;
        auto &context = immediate ? device.GetImmediateContext() : *commands;
        if (immediate && manageSubmission)
            context.BeginFrame("Native UI");
        m_renderer->Render(target, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), context, items,
                           ui::FontLibrary::Get().GetAtlas());
        m_renderer->CollectUnused();
        if (immediate && manageSubmission)
            context.Submit();
    }
}
