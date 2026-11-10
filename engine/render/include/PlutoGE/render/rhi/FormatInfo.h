#pragma once

#include "PlutoGE/render/rhi/Types.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Size and layout traits of RHI formats, shared by the backends and by code that prepares texture data for them.
namespace PlutoGE::render::rhi
{
    [[nodiscard]] constexpr bool IsBlockCompressed(Format format) noexcept
    {
        switch (format)
        {
        case Format::BC1RgbaUnorm:
        case Format::BC1RgbaSrgb:
        case Format::BC3RgbaUnorm:
        case Format::BC3RgbaSrgb:
        case Format::BC4RUnorm:
        case Format::BC5RgUnorm:
        case Format::BC7RgbaUnorm:
        case Format::BC7RgbaSrgb:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] constexpr bool IsSrgb(Format format) noexcept
    {
        return format == Format::R8G8B8A8Srgb || format == Format::BC1RgbaSrgb || format == Format::BC3RgbaSrgb ||
               format == Format::BC7RgbaSrgb;
    }

    // Bytes of one 4x4 block of a block-compressed format; zero for any other format.
    [[nodiscard]] constexpr std::size_t BlockByteSize(Format format) noexcept
    {
        switch (format)
        {
        case Format::BC1RgbaUnorm:
        case Format::BC1RgbaSrgb:
        case Format::BC4RUnorm:
            return 8;
        case Format::BC3RgbaUnorm:
        case Format::BC3RgbaSrgb:
        case Format::BC5RgUnorm:
        case Format::BC7RgbaUnorm:
        case Format::BC7RgbaSrgb:
            return 16;
        default:
            return 0;
        }
    }

    // Bytes of one texel of an uncompressed format; zero for block-compressed and unknown formats.
    [[nodiscard]] constexpr std::size_t TexelByteSize(Format format) noexcept
    {
        switch (format)
        {
        case Format::R8G8B8A8Unorm:
        case Format::R8G8B8A8Srgb:
        case Format::R32Float:
        case Format::R32Uint:
        case Format::D32Float:
            return 4;
        case Format::R16G16B16A16Float:
        case Format::R32G32Float:
            return 8;
        case Format::R32G32B32Float:
            return 12;
        case Format::R32G32B32A32Float:
            return 16;
        default:
            return 0;
        }
    }

    // Bytes of one 2D mip level of `width` x `height` texels. Block-compressed levels round up to whole blocks.
    [[nodiscard]] constexpr std::size_t TextureLevelByteSize(Format format, std::uint32_t width, std::uint32_t height) noexcept
    {
        if (const auto block = BlockByteSize(format))
            return std::size_t(((std::max)(width, 1u) + 3) / 4) * (((std::max)(height, 1u) + 3) / 4) * block;
        return std::size_t(width) * height * TexelByteSize(format);
    }

    // The number of levels of a complete 2D mip chain.
    [[nodiscard]] inline std::uint32_t FullMipLevelCount(std::uint32_t width, std::uint32_t height) noexcept
    {
        return 1u + static_cast<std::uint32_t>(std::floor(std::log2(static_cast<double>((std::max)({width, height, 1u})))));
    }

    // Bytes of `levels` packed 2D mip levels starting at `width` x `height`.
    [[nodiscard]] constexpr std::size_t TextureChainByteSize(Format format, std::uint32_t width, std::uint32_t height,
                                                             std::uint32_t levels) noexcept
    {
        std::size_t total = 0;
        for (std::uint32_t level = 0; level < levels; ++level)
        {
            total += TextureLevelByteSize(format, width, height);
            width = (std::max)(1u, width / 2);
            height = (std::max)(1u, height / 2);
        }
        return total;
    }
}
