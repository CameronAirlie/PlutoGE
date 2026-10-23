#pragma once

#include "PlutoGE/render/rhi/Types.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace PlutoGE::render::rhi
{
    // Whether IRenderDevice::UpdateTexture may apply `region` to a texture described by `descriptor`: a sampled RGBA8
    // 2D texture (or array layer), a non-empty region inside its base level, and tightly packed data for it. Shared
    // by the backends so they accept exactly the same updates.
    inline bool IsValidTextureUpdate(const TextureDescriptor &descriptor, const TextureRegion &region, std::span<const std::byte> data)
    {
        const bool sampled = descriptor.usage == TextureUsage::Sampled || descriptor.sampled;
        const bool rgba8 = descriptor.format == Format::R8G8B8A8Unorm || descriptor.format == Format::R8G8B8A8Srgb;
        return sampled && rgba8 && descriptor.depth == 1 && !descriptor.storage && !descriptor.normalMipmapsProvided &&
               region.width > 0 && region.height > 0 && region.x + region.width <= descriptor.width &&
               region.y + region.height <= descriptor.height && region.layer < std::max(1u, descriptor.arrayLayers) &&
               data.size() == std::size_t(region.width) * region.height * 4;
    }

    // The mip levels a texture was created with (TextureDescriptor::mipLevels 0 = the full chain for sampled textures).
    inline std::uint32_t TextureMipLevels(const TextureDescriptor &descriptor)
    {
        if (descriptor.mipLevels != 0)
            return descriptor.mipLevels;
        if (descriptor.usage != TextureUsage::Sampled)
            return 1;
        return 1u + static_cast<std::uint32_t>(std::floor(std::log2(static_cast<double>(
            std::max({descriptor.width, descriptor.height, descriptor.depth})))));
    }
}
