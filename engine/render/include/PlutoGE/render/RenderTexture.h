#pragma once
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace PlutoGE::render
{
    namespace rhi
    {
        class IRenderDevice;
    }

    // Persisted settings of a render texture asset.
    struct RenderTextureDescriptor
    {
        static constexpr int kMinSize = 1;
        static constexpr int kMaxSize = 4096;
        int width = 512;
        int height = 512;

        bool operator==(const RenderTextureDescriptor &) const = default;
    };

    // A texture whose contents are rendered each frame by a camera, similar to
    // Unity's RenderTexture. Materials reference it like any other texture;
    // cameras whose TargetTexture names the same asset render into it.
    //
    // The GPU image belongs to the renderer that draws it. It is published here
    // per frame, tagged with its device, and read back by scene renderers when
    // a material samples this texture. The image holds the camera's displayed
    // colour in an sRGB format, so it samples exactly like an imported image.
    class RenderTexture final : public Texture
    {
    public:
        static constexpr std::string_view kAssetExtension = ".plutorendertexture";

        RenderTexture(std::string assetPath, const RenderTextureDescriptor &descriptor);

        [[nodiscard]] const RenderTextureDescriptor &GetDescriptor() const noexcept { return m_descriptor; }
        // Changing the size invalidates the published image and every material
        // that samples this texture.
        void SetDescriptor(const RenderTextureDescriptor &descriptor);

        // Returns the image rendered for `device`, or null before the first
        // render or when it was rendered on another device.
        [[nodiscard]] rhi::TextureHandle GetGpuTexture(const rhi::IRenderDevice &device) const noexcept;
        // Renderer-facing: publish the current image, or clear it with a null handle.
        void PublishGpuTexture(const rhi::IRenderDevice *device, rhi::TextureHandle texture);

        [[nodiscard]] static bool IsAssetPath(std::string_view path);
        [[nodiscard]] static RenderTextureDescriptor Sanitize(RenderTextureDescriptor descriptor);
        [[nodiscard]] static std::optional<RenderTextureDescriptor> LoadDescriptor(const std::filesystem::path &path);
        static bool SaveDescriptor(const std::filesystem::path &path, const RenderTextureDescriptor &descriptor,
                                   std::string *errorMessage = nullptr);

    private:
        void Invalidate() noexcept { ++m_contentRevision; }

        RenderTextureDescriptor m_descriptor;
        const rhi::IRenderDevice *m_gpuDevice = nullptr;
        rhi::TextureHandle m_gpuTexture;
    };
}
