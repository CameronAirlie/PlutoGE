#include "CloudNoiseVolume.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using PlutoGE::render::CloudNoiseVolume;

int main()
{
    try
    {
        constexpr std::uint32_t size = 8;
        const auto lattice = CloudNoiseVolume::BakeLattice(size);
        if (lattice.size() != size * size * size * 4) throw std::runtime_error("Lattice texel count changed");
        for (std::uint32_t z = 0; z < size; ++z)
            for (std::uint32_t y = 0; y < size; ++y)
                for (std::uint32_t x = 0; x < size; ++x)
                {
                    const auto texel = ((z * size + y) * size + x) * 4;
                    // Red keeps the top eight bits of the shader's 24-bit lattice value.
                    const float value = CloudNoiseVolume::LatticeValue(int(x), int(y), int(z));
                    if (lattice[texel] != static_cast<std::uint8_t>(std::floor(value * 256.0f)) || lattice[texel + 3] != 255)
                        throw std::runtime_error("Baked lattice differs from the shader hash");
                }
        // Reference values pin the hash to VolumetricCloud.slang's cloudHash/hash31.
        if (std::abs(CloudNoiseVolume::LatticeValue(0, 0, 0) - 0.0f) > 1e-7f)
            throw std::runtime_error("Zero cell must hash to zero");
        const float a = CloudNoiseVolume::LatticeValue(1, 2, 3), b = CloudNoiseVolume::LatticeValue(-1, 2, 3);
        if (!(a >= 0.0f && a < 1.0f && b >= 0.0f && b < 1.0f) || a == b)
            throw std::runtime_error("Lattice values must be unit-range and distinguish negative cells");
        std::cout << "PASS: cloud noise lattice matches the shader hash\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
