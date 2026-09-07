#include "rhi/NormalMipmaps.h"
#include <iostream>

int main()
{
    using PlutoGE::render::rhi::BuildNormalMipmaps;
    auto require = [](bool condition, const char *message) {
        if (!condition) throw std::runtime_error(message);
    };
    try
    {
        // Constant tangent normals must not drift, including odd and thin maps.
        for (const auto size : {std::pair{32u, 32u}, {7u, 5u}, {1u, 16u}})
        {
            std::vector<std::byte> pixels(std::size_t(size.first) * size.second * 4);
            for (std::size_t i = 0; i < pixels.size(); ++i)
                pixels[i] = static_cast<std::byte>(i % 4 < 2 ? 128 : 255);
            const auto levels = 1u + unsigned(std::floor(std::log2(std::max(size.first, size.second))));
            const auto mips = BuildNormalMipmaps(pixels, size.first, size.second, levels);
            for (std::size_t i = 0; i < mips.size(); ++i)
                require(mips[i] == pixels[i % 4], "Flat normals drift through mip levels");
        }
        // Two-texel opposing bevels alias at half resolution under a box
        // reduction. The wider filter must reduce their residual slope equally
        // for horizontal and vertical grooves, including the repeating seam.
        constexpr unsigned size = 16;
        std::vector<std::byte> horizontal(size * size * 4), vertical(horizontal.size());
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x)
                for (unsigned c = 0; c < 4; ++c)
                {
                    horizontal[(y * size + x) * 4 + c] = static_cast<std::byte>(
                        c == 0 ? (x % 4 < 2 ? 64 : 192) : c == 1 ? 128 : 255);
                    vertical[(y * size + x) * 4 + c] = static_cast<std::byte>(
                        c == 1 ? (y % 4 < 2 ? 64 : 192) : c == 0 ? 128 : 255);
                }
        const auto a = BuildNormalMipmaps(horizontal, size, size, 5);
        const auto b = BuildNormalMipmaps(vertical, size, size, 5);
        require(std::equal(horizontal.begin(), horizontal.end(), a.begin()), "Source detail changed");
        std::size_t offset = horizontal.size();
        for (unsigned extent = size / 2; extent; extent /= 2)
        {
            for (unsigned y = 0; y < extent; ++y)
                for (unsigned x = 0; x < extent; ++x)
                {
                    const int slope = std::to_integer<int>(a[offset + (y * extent + x) * 4]);
                    require(std::abs(slope - 128) <= 32, "Unresolved bevel was not filtered");
                    require(a[offset + (y * extent + x) * 4] == b[offset + (x * extent + y) * 4 + 1],
                            "Filtering differs between groove directions");
                }
            offset += extent * extent * 4;
        }
        require(offset == a.size(), "Incorrect mip chain layout");
        std::cout << "Normal mipmap checks passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
