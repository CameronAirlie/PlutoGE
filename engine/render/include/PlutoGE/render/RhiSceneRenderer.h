#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Camera.h"

#include <functional>
#include <memory>
#include <span>
#include <unordered_map>

namespace PlutoGE::render
{
    struct RhiSceneTimingStats
    {
        float commandTranslationMs = 0.0f;
        float sceneSetupMs = 0.0f;
        float renderRecordingMs = 0.0f;
        float totalMs = 0.0f;
        std::size_t visibleDrawCount = 0;
        std::size_t visibleInstanceCount = 0;
        std::size_t shadowCandidateCount = 0;
        std::size_t recordedGeometryDrawCount = 0;
        std::size_t recordedGeometryInstanceCount = 0;
        std::size_t recordedShadowDrawCount = 0;
        std::size_t recordedShadowInstanceCount = 0;
        std::size_t shadowObjectUploadCount = 0;
        std::size_t shadowCascadeUpdateCount = 0;
        std::size_t shadowCascadeCacheHitCount = 0;
        std::array<std::size_t, 4> recordedShadowDrawsByCascade{};
    };

    class Mesh;
    class Texture;
    class IPostProcessEffect;
    struct RenderCommand;

    // Backend-neutral scene translation and GPU asset cache shared by editor
    // and runtime hosts. Pixel acquisition is injected so this layer never
    // depends on OpenGL readback or a particular asset decoder.
    class RhiSceneRenderer
    {
    public:
        using TexturePixelReader = std::function<std::vector<std::byte>(const Texture &)>;

        bool Initialize(rhi::IRenderDevice &device, const BasicRendererShaderPackage &shaders);
        void Shutdown();
        bool Render(std::uint32_t width, std::uint32_t height,
                    const CameraData &cameraData, const BasicLighting &lighting,
                    std::span<const RenderCommand> commands,
                    std::span<const RenderCommand> shadowCommands,
                    std::span<IPostProcessEffect *const> postProcessEffects = {},
                    std::span<const BasicPostProcessEffect> atmosphereEffects = {},
                    const TexturePixelReader &texturePixelReader = {},
                    PostProcessDebugView debugView = PostProcessDebugView::None);

        [[nodiscard]] rhi::TextureHandle GetColorTexture() const noexcept;
        [[nodiscard]] rhi::TextureHandle GetDepthTexture() const noexcept;
        [[nodiscard]] rhi::TextureHandle GetNormalTexture() const noexcept;
        [[nodiscard]] rhi::TextureHandle GetMaterialTexture() const noexcept;
        [[nodiscard]] rhi::TextureHandle GetMotionTexture() const noexcept;
        [[nodiscard]] std::size_t GetSceneCommandCount() const noexcept { return m_sceneCommandCount; }
        [[nodiscard]] std::size_t GetDrawCount() const noexcept { return m_drawCount; }
        [[nodiscard]] const RhiSceneTimingStats &GetTimingStats() const noexcept { return m_timingStats; }

    private:
        rhi::IRenderDevice *m_device = nullptr;
        std::unique_ptr<BasicRenderer> m_renderer;
        std::unordered_map<const Mesh *, BasicMesh> m_meshes;
        std::unordered_map<const Texture *, rhi::Texture> m_srgbTextures;
        std::unordered_map<const Texture *, rhi::Texture> m_linearTextures;
        std::size_t m_sceneCommandCount = 0;
        std::size_t m_drawCount = 0;
        RhiSceneTimingStats m_timingStats;
        std::uint64_t m_temporalFrameIndex = 0;
        glm::vec2 m_previousTemporalJitterNdc{0.0f};
    };
}
