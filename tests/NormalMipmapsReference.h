// Reference implementation retained to verify filter equivalence.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace PlutoGE::render::rhi
{
    // RGBA8 linear tangent normals, with repeat addressing as used by materials.
    // Keep the averaged vector length: renormalizing each mip would amplify
    // unresolved bumps. Keep float intermediates to avoid cumulative byte bias.
    inline std::vector<std::byte> BuildNormalMipmapsBefore(
        std::span<const std::byte> source, std::uint32_t width,
        std::uint32_t height, std::uint32_t levels)
    {
        if (!width || !height || !levels || source.size() != std::size_t(width) * height * 4)
            throw std::invalid_argument("Invalid normal mipmap input");
        std::vector<std::byte> result(source.begin(), source.end());
        std::vector<float> current(source.size());
        for (std::size_t i = 0; i < source.size(); ++i)
            current[i] = float(std::to_integer<unsigned char>(source[i]));
        const auto wrap = [](int coordinate, int extent) {
            return (coordinate % extent + extent) % extent;
        };
        for (std::uint32_t level = 1; level < levels; ++level)
        {
            const auto nextWidth = std::max(1u, width / 2);
            const auto nextHeight = std::max(1u, height / 2);
            const float scaleX = float(width) / nextWidth;
            const float scaleY = float(height) / nextHeight;
            std::vector<float> next(std::size_t(nextWidth) * nextHeight * 4);
            for (std::uint32_t y = 0; y < nextHeight; ++y)
                for (std::uint32_t x = 0; x < nextWidth; ++x)
                {
                    const float centerX = (x + 0.5f) * scaleX;
                    const float centerY = (y + 0.5f) * scaleY;
                    float sum[4]{};
                    float weightSum = 0;
                    // A separable tent low-pass, [1,3,3,1] per axis for a
                    // 2:1 reduction, suppresses phase-dependent groove bands.
                    for (int sy = int(std::floor(centerY - scaleY)); sy < int(std::ceil(centerY + scaleY)); ++sy)
                        for (int sx = int(std::floor(centerX - scaleX)); sx < int(std::ceil(centerX + scaleX)); ++sx)
                        {
                            const float weight = std::max(0.0f, 1.0f - std::abs(sx + 0.5f - centerX) / scaleX) *
                                                 std::max(0.0f, 1.0f - std::abs(sy + 0.5f - centerY) / scaleY);
                            const auto index = (std::size_t(wrap(sy, int(height))) * width + wrap(sx, int(width))) * 4;
                            for (int c = 0; c < 4; ++c)
                                sum[c] += current[index + c] * weight;
                            weightSum += weight;
                        }
                    for (int c = 0; c < 4; ++c)
                    {
                        const float value = sum[c] / weightSum;
                        next[(std::size_t(y) * nextWidth + x) * 4 + c] = value;
                        result.push_back(static_cast<std::byte>(std::clamp(std::lround(value), 0l, 255l)));
                    }
                }
            current = std::move(next);
            width = nextWidth;
            height = nextHeight;
        }
        return result;
    }
}


