#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Camera.h"

#include <functional>
#include <memory>
#include <span>
#include <unordered_map>

namespace PlutoGE::render
{
    class Mesh;
    class Texture;
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
                    const TexturePixelReader &texturePixelReader = {});

        [[nodiscard]] rhi::TextureHandle GetColorTexture() const noexcept;
        [[nodiscard]] std::size_t GetSceneCommandCount() const noexcept { return m_sceneCommandCount; }
        [[nodiscard]] std::size_t GetDrawCount() const noexcept { return m_drawCount; }

    private:
        rhi::IRenderDevice *m_device = nullptr;
        std::unique_ptr<BasicRenderer> m_renderer;
        std::unordered_map<const Mesh *, BasicMesh> m_meshes;
        std::unordered_map<const Texture *, rhi::Texture> m_srgbTextures;
        std::unordered_map<const Texture *, rhi::Texture> m_linearTextures;
        std::size_t m_sceneCommandCount = 0;
        std::size_t m_drawCount = 0;
    };
}
