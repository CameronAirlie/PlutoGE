#pragma once

#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"

#include <stb_image_write.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>

namespace PlutoGE
{
    // Saves a bottom-up colour texture as an upright PNG so benchmark runs can
    // be compared visually (for example a lighting path against its reference
    // mode). Readback is implemented by the Vulkan device only.
    inline bool WriteFrameCapture(render::rhi::IRenderDevice *device, render::rhi::TextureHandle texture,
                                  std::uint32_t width, std::uint32_t height, const std::filesystem::path &path,
                                  std::string &error)
    {
        auto *vulkan = dynamic_cast<render::rhi::vulkan::VulkanDevice *>(device);
        if (!vulkan || !texture)
        {
            error = "Frame capture requires the Vulkan renderer";
            return false;
        }
        auto pixels = vulkan->ReadTextureRgba8(texture);
        const std::size_t rowBytes = std::size_t(width) * 4;
        if (pixels.size() != rowBytes * height)
        {
            error = "Frame capture size does not match the rendered output";
            return false;
        }
        for (std::uint32_t row = 0; row < height / 2; ++row)
            std::swap_ranges(pixels.begin() + row * rowBytes, pixels.begin() + (row + 1) * rowBytes,
                             pixels.begin() + (height - 1 - row) * rowBytes);
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        if (!stbi_write_png(path.string().c_str(), static_cast<int>(width), static_cast<int>(height), 4, pixels.data(),
                            static_cast<int>(width * 4)))
        {
            error = "Could not write frame capture " + path.string();
            return false;
        }
        return true;
    }
}
