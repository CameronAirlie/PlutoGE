#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace PlutoGE::render::rhi
{
    inline void ValidateNormalMipChain(std::span<const std::byte> data,
                                      std::uint32_t width, std::uint32_t height,
                                      std::uint32_t levels)
    {
        std::size_t expected = 0;
        for (std::uint32_t level = 0; level < levels; ++level)
        {
            expected += std::size_t(width) * height * 4;
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }
        if (!levels || data.size() != expected)
            throw std::invalid_argument("Invalid packed normal mip chain");
    }
    // RGBA8 linear tangent normals, with repeat addressing as used by materials.
    // Keep the averaged vector length: renormalizing each mip would amplify
    // unresolved bumps. Keep float intermediates to avoid cumulative byte bias.
    inline std::vector<std::byte> BuildNormalMipmaps(
        std::span<const std::byte> source, std::uint32_t width,
        std::uint32_t height, std::uint32_t levels)
    {
        if (!width || !height || !levels || source.size() != std::size_t(width) * height * 4)
            throw std::invalid_argument("Invalid normal mipmap input");
        std::vector<std::byte> result(source.begin(), source.end());
        if (levels == 1)
            return result;
        std::size_t totalBytes = source.size();
        for (auto w = width, h = height, level = 1u; level < levels; ++level)
        {
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
            totalBytes += std::size_t(w) * h * 4;
        }
        result.reserve(totalBytes);
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
            // Calculate repeat addressing and tent weights once per axis,
            // rather than doing divisions and modulo for every pixel sample.
            struct Tap { std::size_t offset; float weight; };
            // The widest case is 3 -> 1: seven taps including the seam.
            struct Kernel { std::array<Tap, 7> taps{}; unsigned count = 0; };
            const auto kernels = [&](unsigned extent, unsigned nextExtent, float scale, std::size_t stride) {
                std::vector<Kernel> values(nextExtent);
                for (unsigned i = 0; i < nextExtent; ++i)
                {
                    const float center = (i + 0.5f) * scale;
                    auto &kernel = values[i];
                    for (int s = int(std::floor(center - scale)); s < int(std::ceil(center + scale)); ++s)
                        kernel.taps[kernel.count++] = {
                            std::size_t(wrap(s, int(extent))) * stride,
                            std::max(0.0f, 1.0f - std::abs(s + 0.5f - center) / scale)};
                }
                return values;
            };
            const auto xKernels = kernels(width, nextWidth, scaleX, 4);
            const auto yKernels = kernels(height, nextHeight, scaleY, std::size_t(width) * 4);
            std::vector<float> next(std::size_t(nextWidth) * nextHeight * 4);
            const auto outputOffset = result.size();
            result.resize(outputOffset + next.size());
            for (std::uint32_t y = 0; y < nextHeight; ++y)
                for (std::uint32_t x = 0; x < nextWidth; ++x)
                {
                    const auto &kx = xKernels[x];
                    const auto &ky = yKernels[y];
                    float sum[4]{};
                    float weightSum = 0;
                    // A separable tent low-pass, [1,3,3,1] per axis for a
                    // 2:1 reduction, suppresses phase-dependent groove bands.
                    for (unsigned sy = 0; sy < ky.count; ++sy)
                        for (unsigned sx = 0; sx < kx.count; ++sx)
                        {
                            const float weight = kx.taps[sx].weight * ky.taps[sy].weight;
                            const auto index = ky.taps[sy].offset + kx.taps[sx].offset;
                            for (int c = 0; c < 4; ++c)
                                sum[c] += current[index + c] * weight;
                            weightSum += weight;
                        }
                    for (int c = 0; c < 4; ++c)
                    {
                        const float value = sum[c] / weightSum;
                        next[(std::size_t(y) * nextWidth + x) * 4 + c] = value;
                        result[outputOffset + (std::size_t(y) * nextWidth + x) * 4 + c] =
                            static_cast<std::byte>(std::clamp(std::lround(value), 0l, 255l));
                    }
                }
            current = std::move(next);
            width = nextWidth;
            height = nextHeight;
        }
        return result;
    }
}
