#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    struct DdsImage
    {
        int width = 0, height = 0;
        std::vector<unsigned char> pixels;
    };

    // Base mip decoder for 2D BC1/BC2/BC3/BC5. BC5 normal maps store XY;
    // reconstruct positive Z for the engine's RGB normal-map shader.
    inline bool DecodeDds(std::span<const unsigned char> bytes, DdsImage &image)
    {
        image = {};
        if (bytes.size() < 128)
            return false;
        const auto u32 = [&](size_t o) {
            return uint32_t(bytes[o]) | uint32_t(bytes[o + 1]) << 8 | uint32_t(bytes[o + 2]) << 16 |
                   uint32_t(bytes[o + 3]) << 24;
        };
        if (u32(0) != 0x20534444 || u32(4) != 124 || u32(76) != 32 || !(u32(80) & 4))
            return false;
        const auto width = u32(16), height = u32(12);
        if (!width || !height || width > 16384 || height > 16384 || (u32(112) & 0x20fe00))
            return false;
        auto format = u32(84);
        size_t offset = 128;
        if (format == 0x30315844)
        {
            if (bytes.size() < 148 || u32(132) != 3 || u32(140) != 1 || (u32(136) & 4))
                return false;
            const auto dxgi = u32(128);
            format = dxgi == 71 || dxgi == 72   ? 0x31545844
                     : dxgi == 74 || dxgi == 75 ? 0x33545844
                     : dxgi == 77 || dxgi == 78 ? 0x35545844
                     : dxgi == 83               ? 0x32495441
                                                : 0;
            offset = 148;
        }
        const bool bc1 = format == 0x31545844, bc2 = format == 0x33545844, bc3 = format == 0x35545844;
        const bool bc5 = format == 0x32495441 || format == 0x55354342;
        if (!bc1 && !bc2 && !bc3 && !bc5)
            return false;
        const size_t blockSize = bc1 ? 8 : 16, blocksX = (width + 3) / 4, blocksY = (height + 3) / 4;
        if (blocksX * blocksY * blockSize > bytes.size() - offset)
            return false;
        image.width = int(width);
        image.height = int(height);
        image.pixels.resize(size_t(width) * height * 4);
        const auto channel = [](const unsigned char *p, unsigned char *out) {
            unsigned char table[8]{p[0], p[1]};
            if (p[0] > p[1])
                for (int i = 1; i <= 6; ++i)
                    table[i + 1] = ((7 - i) * p[0] + i * p[1]) / 7;
            else
            {
                for (int i = 1; i <= 4; ++i)
                    table[i + 1] = ((5 - i) * p[0] + i * p[1]) / 5;
                table[6] = 0;
                table[7] = 255;
            }
            uint64_t bits = 0;
            for (int i = 0; i < 6; ++i)
                bits |= uint64_t(p[i + 2]) << (8 * i);
            for (int i = 0; i < 16; ++i)
                out[i] = table[(bits >> (3 * i)) & 7];
        };
        for (size_t by = 0; by < blocksY; ++by)
            for (size_t bx = 0; bx < blocksX; ++bx, offset += blockSize)
            {
                const auto *block = bytes.data() + offset;
                unsigned char red[16]{}, green[16]{}, alpha[16];
                std::fill_n(alpha, 16, 255);
                std::array<std::array<unsigned char, 4>, 4> colors{};
                uint32_t indices = 0;
                if (bc5)
                {
                    channel(block, red);
                    channel(block + 8, green);
                }
                else
                {
                    if (bc3)
                        channel(block, alpha);
                    if (bc2)
                        for (int i = 0; i < 16; ++i)
                            alpha[i] = ((block[i / 2] >> ((i % 2) * 4)) & 15) * 17;
                    const auto *c = block + (bc1 ? 0 : 8);
                    const uint16_t a = uint16_t(c[0]) | uint16_t(c[1]) << 8, b = uint16_t(c[2]) | uint16_t(c[3]) << 8;
                    for (int i = 0; i < 2; ++i)
                    {
                        const auto value = i ? b : a;
                        const auto r = (value >> 11) & 31, g = (value >> 5) & 63, blue = value & 31;
                        colors[i] = {static_cast<unsigned char>((r << 3) | (r >> 2)),
                                     static_cast<unsigned char>((g << 2) | (g >> 4)),
                                     static_cast<unsigned char>((blue << 3) | (blue >> 2)), 255};
                    }
                    for (int k = 0; k < 3; ++k)
                    {
                        colors[2][k] =
                            a > b || !bc1 ? (2 * colors[0][k] + colors[1][k]) / 3 : (colors[0][k] + colors[1][k]) / 2;
                        colors[3][k] = a > b || !bc1 ? (colors[0][k] + 2 * colors[1][k]) / 3 : 0;
                    }
                    colors[2][3] = 255;
                    colors[3][3] = a > b || !bc1 ? 255 : 0;
                    for (int i = 0; i < 4; ++i)
                        indices |= uint32_t(c[i + 4]) << (8 * i);
                }
                for (size_t y = 0; y < 4 && by * 4 + y < height; ++y)
                    for (size_t x = 0; x < 4 && bx * 4 + x < width; ++x)
                    {
                        const auto i = y * 4 + x;
                        auto *out = image.pixels.data() + ((by * 4 + y) * width + bx * 4 + x) * 4;
                        if (bc5)
                        {
                            const float nx = red[i] / 127.5f - 1, ny = green[i] / 127.5f - 1;
                            out[0] = red[i];
                            out[1] = green[i];
                            out[2] = static_cast<unsigned char>(
                                127.5f * (1 + std::sqrt(std::max(0.0f, 1 - nx * nx - ny * ny))) + 0.5f);
                            out[3] = 255;
                        }
                        else
                        {
                            const auto &color = colors[(indices >> (2 * i)) & 3];
                            std::copy(color.begin(), color.end(), out);
                            if (!bc1)
                                out[3] = alpha[i];
                        }
                    }
            }
        return true;
    }
} // namespace PlutoGE::render
