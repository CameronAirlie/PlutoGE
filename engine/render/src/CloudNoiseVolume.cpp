#include "CloudNoiseVolume.h"

#include <span>

namespace PlutoGE::render
{
    namespace
    {
        // Mirrors cloudHash in VolumetricCloud.slang.
        std::uint32_t CloudHash(std::uint32_t value)
        {
            value ^= value >> 16; value *= 0x7feb352du;
            value ^= value >> 15; value *= 0x846ca68bu;
            return value ^ (value >> 16);
        }

        std::uint32_t LatticeHash(std::int32_t x, std::int32_t y, std::int32_t z)
        {
            const std::uint32_t seed = static_cast<std::uint32_t>(x) * 0x8da6b343u ^
                                       static_cast<std::uint32_t>(y) * 0xd8163841u ^
                                       static_cast<std::uint32_t>(z) * 0xcb1ab31fu;
            return CloudHash(seed);
        }
    }

    float CloudNoiseVolume::LatticeValue(std::int32_t x, std::int32_t y, std::int32_t z)
    {
        return static_cast<float>(LatticeHash(x, y, z) >> 8) * (1.0f / 16777216.0f);
    }

    std::vector<std::uint8_t> CloudNoiseVolume::BakeLattice(std::uint32_t size)
    {
        std::vector<std::uint8_t> texels(std::size_t(size) * size * size * 4, 0);
        std::size_t texel = 0;
        for (std::uint32_t z = 0; z < size; ++z)
            for (std::uint32_t y = 0; y < size; ++y)
                for (std::uint32_t x = 0; x < size; ++x, texel += 4)
                {
                    texels[texel] = static_cast<std::uint8_t>(LatticeHash(std::int32_t(x), std::int32_t(y), std::int32_t(z)) >> 24);
                    texels[texel + 3] = 255;
                }
        return texels;
    }

    bool CloudNoiseVolume::Initialize(rhi::IRenderDevice &device)
    {
        const auto texels = BakeLattice();
        m_texture = rhi::Texture(device, device.CreateTexture(
            {.width = Size, .height = Size, .format = rhi::Format::R8G8B8A8Unorm, .usage = rhi::TextureUsage::Sampled,
             .debugName = "Cloud noise lattice", .sampled = true, .depth = Size, .mipLevels = 1},
            std::as_bytes(std::span(texels))));
        m_sampler = rhi::Sampler(device, device.CreateSampler({.linearFiltering = true, .repeat = true,
                                                               .debugName = "Cloud noise lattice"}));
        return m_texture && m_sampler;
    }
}
