#pragma once

#include "PlutoGE/render/rhi/Resource.h"

#include <cstdint>
#include <vector>

namespace PlutoGE::render
{
    // Baked lattice for volumetric cloud value noise.
    //
    // VolumetricCloud.slang's value noise interpolates hashed lattice corners
    // with smoothstep weights. Storing the hashes in a tiling volume lets one
    // trilinear fetch at floor(p) + smoothstep(frac(p)) replace eight integer
    // hashes and seven interpolations per octave. The lattice repeats every
    // Size cells (kilometres at the cloud noise scales), so the cloud layout is
    // a different random instance of the same noise. Keep Size in sync with
    // PLUTO_CLOUD_NOISE_VOLUME_SIZE in VolumetricCloud.slang.
    class CloudNoiseVolume
    {
    public:
        static constexpr std::uint32_t Size = 128;

        // RGBA8 texels; red holds the top eight bits of the shader's hash31.
        [[nodiscard]] static std::vector<std::uint8_t> BakeLattice(std::uint32_t size = Size);
        // The shader's cloudHash/hash31 lattice value for an integer cell.
        [[nodiscard]] static float LatticeValue(std::int32_t x, std::int32_t y, std::int32_t z);

        bool Initialize(rhi::IRenderDevice &device);
        [[nodiscard]] rhi::TextureHandle Texture() const noexcept { return m_texture.Get(); }
        [[nodiscard]] rhi::SamplerHandle Sampler() const noexcept { return m_sampler.Get(); }

    private:
        rhi::Texture m_texture;
        rhi::Sampler m_sampler;
    };
}
