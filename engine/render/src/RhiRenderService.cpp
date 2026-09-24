#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/RhiRenderService.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/scene/Scene.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace PlutoGE::render
{
    bool RhiRenderService::Initialize(rhi::IRenderDevice &device, rhi::ISwapchain &swapchain)
    {
        Shutdown();
        const ShaderArtifactLibrary shaderArtifacts;
        BasicRendererShaderPackage shaders = shaderArtifacts.LoadBasicRendererPackage();

        auto renderer = std::make_unique<BasicRenderer>();
        renderer->SetTemporalUpscalerOptions(m_upscalerOptions);
        if (!renderer->Initialize(device, shaders) || !renderer->Resize(swapchain.GetWidth(), swapchain.GetHeight()))
            return false;
        m_graphicsApi = device.GetApi();
        m_device = &device;
        m_swapchain = &swapchain;
        m_renderer = std::move(renderer);
        m_hostFrameReady = false;
        return true;
    }

    void RhiRenderService::Shutdown()
    {
        m_loadingLabel.reset();
        m_loadingQuad.reset();
        RmlUiRuntime::Get().Shutdown();
        if (m_sceneRenderer)
            m_sceneRenderer->Shutdown();
        m_sceneRenderer.reset();
        if (m_renderer)
            m_renderer->Shutdown();
        m_renderer.reset();
        m_device = nullptr;
        m_swapchain = nullptr;
        m_frameSequence = 0;
        m_hostFrameReady = false;
    }

    bool RhiRenderService::RenderSceneAndPresent(const CameraData &cameraData,
                                                 const BasicLighting &lighting,
                                                 std::span<const RenderCommand> commands,
                                                 const RhiSceneRenderer::TexturePixelReader &texturePixelReader,
                                                 const scene::Scene *scene,
                                                 std::span<IPostProcessEffect *const> postProcessEffects)
    {
        if (!m_swapchain || !m_renderer)
            return false;
        if (!m_sceneRenderer)
        {
            const ShaderArtifactLibrary shaderArtifacts;
            auto sceneRenderer = std::make_unique<RhiSceneRenderer>();
            if (!sceneRenderer->Initialize(*m_device, shaderArtifacts.LoadBasicRendererPackage()))
                return false;
            sceneRenderer->SetTemporalUpscalerOptions(m_upscalerOptions);
            m_sceneRenderer = std::move(sceneRenderer);
        }
        // Let the first UI frame initialize its pipelines and upload resources
        // outside an active scene command buffer. Subsequent frames can safely
        // append UI rendering to the scene submission.
        const bool combineRuntimeUiSubmission = scene && scene->HasRmlRuntimeUI() &&
                                                RmlUiRuntime::Get().IsInitialized() &&
                                                m_device->GetApi() == rhi::GraphicsApi::Vulkan;
        if (scene) RmlUiRuntime::Get().PrepareScenePortraits(*scene, *m_device);
        const auto atmosphere = BuildSceneAtmosphere(scene, lighting);
        if (!m_sceneRenderer->Render(m_swapchain->GetWidth(), m_swapchain->GetHeight(), cameraData, lighting, commands,
                                     commands, postProcessEffects, atmosphere, texturePixelReader, PostProcessDebugView::None,
                                     !combineRuntimeUiSubmission, scene))
            return false;
        if (scene && scene->HasRmlRuntimeUI())
            RmlUiRuntime::Get().RenderRhi(*scene, *m_device, m_sceneRenderer->GetColorTexture(),
                                         static_cast<int>(m_swapchain->GetWidth()),
                                         static_cast<int>(m_swapchain->GetHeight()), ++m_frameSequence,
                                         cameraData.view, cameraData.projection,
                                         !combineRuntimeUiSubmission);
        if (combineRuntimeUiSubmission)
            m_device->GetImmediateContext().Submit();
        // Scene output uses the same bottom-up texture convention as editor viewports.
        return m_swapchain->Present(m_sceneRenderer->GetColorTexture(),
                                    m_graphicsApi == rhi::GraphicsApi::Vulkan);
    }

    bool RhiRenderService::Resize(std::uint32_t width, std::uint32_t height)
    {
        if (!m_renderer || !m_swapchain || !m_swapchain->Resize(width, height) ||
            !m_renderer->Resize(m_swapchain->GetWidth(), m_swapchain->GetHeight()))
            return false;
        m_hostFrameReady = false;
        return true;
    }

    bool RhiRenderService::RenderAndPresent(const glm::mat4 &viewProjection,
                                            const BasicLighting &lighting,
                                            std::span<const BasicDraw> draws)
    {
        if (!m_renderer || !m_swapchain)
            return false;
        if (m_renderer->GetWidth() != m_swapchain->GetWidth() || m_renderer->GetHeight() != m_swapchain->GetHeight())
            if (!m_renderer->Resize(m_swapchain->GetWidth(), m_swapchain->GetHeight()))
                return false;
        m_renderer->Render(viewProjection, lighting, draws);
        m_hostFrameReady = true;
        return m_swapchain->Present(m_renderer->GetColorTexture());
    }

    bool RhiRenderService::Present()
    {
        if (!m_renderer || !m_swapchain)
            return false;
        if (m_renderer->GetWidth() != m_swapchain->GetWidth() ||
            m_renderer->GetHeight() != m_swapchain->GetHeight())
        {
            if (!m_renderer->Resize(m_swapchain->GetWidth(), m_swapchain->GetHeight()))
                return false;
            m_hostFrameReady = false;
        }
        if (!m_hostFrameReady)
        {
            m_renderer->Render(glm::mat4(1.0f), BasicLighting{}, {});
            m_hostFrameReady = true;
        }
        return m_swapchain->Present(m_renderer->GetColorTexture());
    }

    bool RhiRenderService::PresentLoading(float elapsedSeconds, unsigned stage)
    {
        if (!m_renderer || !m_swapchain) return false;
        if (!m_loadingQuad)
        {
            const std::array<BasicVertex, 4> vertices{{
                {.position={0,0,0}, .normal={0,0,1}}, {.position={1,0,0}, .normal={0,0,1}},
                {.position={1,1,0}, .normal={0,0,1}}, {.position={0,1,0}, .normal={0,0,1}}}};
            const std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
            m_loadingQuad = std::make_unique<BasicMesh>(CreateMesh({vertices, indices}));
            // Embedded pixel glyphs avoid loading a font/texture just to load a scene.
            constexpr unsigned glyphs[7][7] = {
                {16,16,16,16,16,16,31}, {14,17,17,17,17,17,14},
                {14,17,17,31,17,17,17}, {30,17,17,17,17,17,30},
                {31,4,4,4,4,4,31}, {17,25,25,21,19,19,17}, {14,17,16,23,17,17,14}};
            std::vector<BasicVertex> labelVertices;
            std::vector<std::uint32_t> labelIndices;
            for (int letter=0; letter<7; ++letter)
                for (int row=0; row<7; ++row)
                    for (int col=0; col<5; ++col)
                        if (glyphs[letter][row] & (16 >> col))
                        {
                            const auto base = static_cast<std::uint32_t>(labelVertices.size());
                            for (auto vertex : vertices)
                            {
                                vertex.position[0] += static_cast<float>(letter*6+col);
                                vertex.position[1] += static_cast<float>(6-row);
                                labelVertices.push_back(vertex);
                            }
                            for (const auto index : indices) labelIndices.push_back(base+index);
                        }
            m_loadingLabel = std::make_unique<BasicMesh>(CreateMesh({labelVertices, labelIndices}));
        }
        const float aspect = static_cast<float>(m_swapchain->GetWidth()) /
                             static_cast<float>(std::max(m_swapchain->GetHeight(), 1u));
        std::vector<BasicDraw> draws;
        const auto rectangle = [&](const BasicMesh *mesh, float x, float y, float w, float h, glm::vec3 color)
        {
            BasicDraw draw;
            draw.mesh = mesh;
            draw.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(x/aspect,y,0.5f)), glm::vec3(w/aspect,h,1));
            draw.baseColor = glm::vec4(0,0,0,1);
            draw.emission = color;
            draw.twoSided = true;
            draw.castsShadow = false;
            draw.contributesToGi = false;
            draws.push_back(std::move(draw));
        };
        rectangle(m_loadingLabel.get(), -.41f, .08f, .02f, .02f, {.75f,.82f,.95f});
        rectangle(m_loadingQuad.get(), -.5f, -.12f, 1.f, .025f, {.04f,.05f,.07f});
        const float phase = .5f + .5f*std::sin(elapsedSeconds*2.5f);
        rectangle(m_loadingQuad.get(), -.5f+.8f*phase, -.12f, .2f, .025f, {.3f,.6f,.95f});
        for (unsigned i=0; i<3; ++i)
            rectangle(m_loadingQuad.get(), -.06f+.05f*i, -.22f, .02f,.02f,
                      i < stage ? glm::vec3(.3f,.6f,.95f) : glm::vec3(.05f));
        BasicLighting lighting;
        lighting.ambientIntensity = 0;
        lighting.directionalIntensity = 0;
        glm::mat4 projection(1);
        if (m_graphicsApi == rhi::GraphicsApi::Vulkan) projection[1][1] = -1;
        return RenderAndPresent(projection, lighting, draws);
    }

    BasicMesh RhiRenderService::CreateMesh(const BasicMeshData &data)
    {
        if (!m_renderer)
            throw std::logic_error("RHI render service is not initialized");
        return m_renderer->CreateMesh(data);
    }

    void RhiRenderService::SetTemporalUpscalerOptions(rhi::TemporalUpscalerOptions options) noexcept
    {
        if (m_upscalerOptions == options)
            return;
        m_upscalerOptions = options;
        if (m_renderer)
            m_renderer->SetTemporalUpscalerOptions(options);
        if (m_sceneRenderer)
        {
            m_sceneRenderer->SetTemporalUpscalerOptions(options);
            m_sceneRenderer->ResetTemporalHistory();
        }
    }

    rhi::TemporalUpscalerSupport RhiRenderService::GetTemporalUpscalerSupport() const
    {
        if (!m_device)
            return {false, "The RHI render service is not initialized"};
        return m_device->GetTemporalUpscalerSupport(m_upscalerOptions.technology);
    }
}
