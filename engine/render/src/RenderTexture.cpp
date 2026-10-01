#include "PlutoGE/render/RenderTexture.h"

#include "PlutoGE/platform/ContentPack.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace PlutoGE::render
{
    RenderTexture::RenderTexture(std::string assetPath, const RenderTextureDescriptor &descriptor)
        : Texture(TextureConfig{std::move(assetPath)}), m_descriptor(Sanitize(descriptor))
    {
        m_width = m_descriptor.width;
        m_height = m_descriptor.height;
        m_channels = 4;
    }

    void RenderTexture::SetDescriptor(const RenderTextureDescriptor &descriptor)
    {
        const auto sanitized = Sanitize(descriptor);
        if (sanitized == m_descriptor)
            return;
        m_descriptor = sanitized;
        m_width = sanitized.width;
        m_height = sanitized.height;
        Invalidate();
    }

    rhi::TextureHandle RenderTexture::GetGpuTexture(const rhi::IRenderDevice &device) const noexcept
    {
        return m_gpuDevice == &device ? m_gpuTexture : rhi::TextureHandle{};
    }

    void RenderTexture::PublishGpuTexture(const rhi::IRenderDevice *device, rhi::TextureHandle texture)
    {
        if (!texture)
            device = nullptr;
        if (m_gpuDevice == device && m_gpuTexture == texture)
            return;
        m_gpuDevice = device;
        m_gpuTexture = texture;
        // Scene renderers cache resolved material textures by revision.
        Invalidate();
    }

    bool RenderTexture::IsAssetPath(std::string_view path)
    {
        if (path.size() < kAssetExtension.size())
            return false;
        const auto suffix = path.substr(path.size() - kAssetExtension.size());
        return std::ranges::equal(suffix, kAssetExtension, [](char a, char b)
                                  { return std::tolower(static_cast<unsigned char>(a)) == b; });
    }

    RenderTextureDescriptor RenderTexture::Sanitize(RenderTextureDescriptor descriptor)
    {
        descriptor.width = std::clamp(descriptor.width, RenderTextureDescriptor::kMinSize, RenderTextureDescriptor::kMaxSize);
        descriptor.height = std::clamp(descriptor.height, RenderTextureDescriptor::kMinSize, RenderTextureDescriptor::kMaxSize);
        return descriptor;
    }

    std::optional<RenderTextureDescriptor> RenderTexture::LoadDescriptor(const std::filesystem::path &path)
    {
        // Built games may read the asset from a mounted content pack.
        content::InputFile input(path);
        if (!input.is_open())
            return std::nullopt;
        RenderTextureDescriptor descriptor;
        std::string line;
        while (std::getline(input, line))
        {
            const auto separator = line.find('=');
            if (separator == std::string::npos)
                continue;
            const auto key = line.substr(0, separator);
            std::istringstream value(line.substr(separator + 1));
            if (key == "Width")
                value >> descriptor.width;
            else if (key == "Height")
                value >> descriptor.height;
        }
        return Sanitize(descriptor);
    }

    bool RenderTexture::SaveDescriptor(const std::filesystem::path &path, const RenderTextureDescriptor &descriptor,
                                       std::string *errorMessage)
    {
        std::ofstream output(path, std::ios::trunc);
        if (!output)
        {
            if (errorMessage)
                *errorMessage = "Cannot write render texture " + path.string();
            return false;
        }
        const auto sanitized = Sanitize(descriptor);
        output << "PlutoRenderTexture=1\n"
               << "Width=" << sanitized.width << "\n"
               << "Height=" << sanitized.height << "\n";
        return static_cast<bool>(output);
    }
}
