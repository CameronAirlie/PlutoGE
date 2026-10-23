#include "PlutoGE/render/TerrainLayers.h"

#include "PlutoGE/render/Texture.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        using Rgba = std::array<std::uint8_t, 4>;

        // Bilinear, wrapping (the maps tile), from an RGBA8 image to a size x size layer.
        void Resample(std::span<const std::byte> source, std::uint32_t width, std::uint32_t height, std::uint32_t size,
                      std::byte *destination)
        {
            const auto texel = [&](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
                return static_cast<float>(std::to_integer<std::uint8_t>(source[(std::size_t(y % height) * width + x % width) * 4 + channel]));
            };
            for (std::uint32_t y = 0; y < size; ++y)
                for (std::uint32_t x = 0; x < size; ++x)
                {
                    const float u = (x + 0.5f) * width / size - 0.5f;
                    const float v = (y + 0.5f) * height / size - 0.5f;
                    const float fu = std::floor(u), fv = std::floor(v);
                    const float tu = u - fu, tv = v - fv;
                    const auto x0 = static_cast<std::uint32_t>(static_cast<std::int64_t>(fu) + width) % width;
                    const auto y0 = static_cast<std::uint32_t>(static_cast<std::int64_t>(fv) + height) % height;
                    for (std::uint32_t c = 0; c < 4; ++c)
                    {
                        const float top = texel(x0, y0, c) * (1 - tu) + texel(x0 + 1, y0, c) * tu;
                        const float bottom = texel(x0, y0 + 1, c) * (1 - tu) + texel(x0 + 1, y0 + 1, c) * tu;
                        destination[(std::size_t(y) * size + x) * 4 + c] =
                            static_cast<std::byte>(std::clamp(top * (1 - tv) + bottom * tv + 0.5f, 0.0f, 255.0f));
                    }
                }
        }

        void Fill(std::byte *destination, std::uint32_t size, Rgba value)
        {
            for (std::size_t i = 0; i < std::size_t(size) * size; ++i)
                for (std::size_t c = 0; c < 4; ++c)
                    destination[i * 4 + c] = static_cast<std::byte>(value[c]);
        }

        // Writes `texture` into the layer at `destination`, or `fallback` when it has no pixels.
        void WriteLayer(const Texture *texture, const TerrainTexturePixels &pixels, std::uint32_t size, Rgba fallback,
                        std::byte *destination)
        {
            if (texture && texture->GetWidth() > 0 && texture->GetHeight() > 0)
            {
                const auto source = pixels(*texture);
                const auto width = static_cast<std::uint32_t>(texture->GetWidth());
                const auto height = static_cast<std::uint32_t>(texture->GetHeight());
                if (source.size() == std::size_t(width) * height * 4)
                {
                    if (width == size && height == size)
                        std::copy(source.begin(), source.end(), destination);
                    else
                        Resample(source, width, height, size, destination);
                    return;
                }
            }
            Fill(destination, size, fallback);
        }
    }

    std::uint32_t TerrainLayerArraySize(const TerrainLayerSet &set, std::uint32_t maxSize)
    {
        std::uint32_t largest = 4;
        for (const auto &layer : set.layers)
            for (const auto *texture : {layer.albedoMap, layer.normalMap, layer.roughnessMap, layer.heightMap})
                if (texture)
                    largest = std::max({largest, static_cast<std::uint32_t>(std::max(texture->GetWidth(), texture->GetHeight()))});
        return std::min(std::bit_ceil(largest), std::max(4u, std::bit_floor(maxSize)));
    }

    TerrainLayerArrayPixels BuildTerrainLayerArrays(const TerrainLayerSet &set, const TerrainTexturePixels &pixels,
                                                    std::uint32_t maxSize)
    {
        TerrainLayerArrayPixels result;
        result.layers = static_cast<std::uint32_t>(std::min(set.layers.size(), kMaxTerrainLayers));
        if (result.layers == 0)
            return result;
        result.size = TerrainLayerArraySize(set, maxSize);
        const std::size_t layerBytes = std::size_t(result.size) * result.size * 4;
        result.albedo.resize(layerBytes * result.layers);
        result.normal.resize(layerBytes * result.layers);
        std::vector<std::byte> scratch(layerBytes);
        // Copies the red channel of `scratch` into channel 3 (alpha) of `destination`.
        const auto packIntoAlpha = [&](std::byte *destination) {
            for (std::size_t texel = 0; texel < std::size_t(result.size) * result.size; ++texel)
                destination[texel * 4 + 3] = scratch[texel * 4];
        };
        for (std::uint32_t index = 0; index < result.layers; ++index)
        {
            const auto &layer = set.layers[index];
            const std::size_t offset = layerBytes * index;
            WriteLayer(layer.albedoMap, pixels, result.size, {255, 255, 255, 255}, result.albedo.data() + offset);
            WriteLayer(layer.heightMap, pixels, result.size, {128, 128, 128, 255}, scratch.data());
            packIntoAlpha(result.albedo.data() + offset);
            WriteLayer(layer.normalMap, pixels, result.size, {128, 128, 255, 255}, result.normal.data() + offset);
            WriteLayer(layer.roughnessMap, pixels, result.size, {255, 255, 255, 255}, scratch.data());
            packIntoAlpha(result.normal.data() + offset);
        }
        return result;
    }
}
