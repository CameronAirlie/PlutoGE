#include "PlutoGE/scene/TerrainLayerStack.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <fstream>

namespace PlutoGE::scene
{
    namespace
    {
        float SrgbToLinear(float value)
        {
            return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        float LinearToSrgb(float value)
        {
            value = std::clamp(value, 0.0f, 1.0f);
            return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        }

        // Rows of `width` RGBA8 texels, reversed (file orientation <-> UV orientation).
        std::vector<std::uint8_t> FlipRows(std::span<const std::uint8_t> pixels, int width)
        {
            const std::size_t row = static_cast<std::size_t>(width) * 4;
            const std::size_t rows = pixels.size() / row;
            std::vector<std::uint8_t> flipped(pixels.size());
            for (std::size_t y = 0; y < rows; ++y)
                std::copy_n(pixels.data() + (rows - 1 - y) * row, row, flipped.data() + y * row);
            return flipped;
        }

        std::size_t PlanesFor(std::size_t layers)
        {
            return std::max<std::size_t>(1, (layers + 3) / 4);
        }
    }

    TerrainLayerStack::TerrainLayerStack(Services services) : m_services(std::move(services)) {}

    bool TerrainLayerStack::AddLayer(TerrainLayer layer)
    {
        if (m_layers.size() >= kMaxLayers)
            return false;
        if (m_layers.empty())
            m_weights.Fill(0);
        m_layers.push_back(std::move(layer));
        MarkLayersChanged();
        return true;
    }

    void TerrainLayerStack::SetLayer(std::size_t index, TerrainLayer layer)
    {
        if (index >= m_layers.size() || m_layers[index] == layer)
            return;
        m_layers[index] = std::move(layer);
        MarkLayersChanged();
    }

    void TerrainLayerStack::RemoveLayer(std::size_t index)
    {
        if (index >= m_layers.size())
            return;
        m_layers.erase(m_layers.begin() + static_cast<std::ptrdiff_t>(index));
        m_weights.RemoveLayer(index);
        MarkLayersChanged();
        MarkWeightsChanged();
    }

    void TerrainLayerStack::MoveLayer(std::size_t from, std::size_t to)
    {
        if (from >= m_layers.size() || to >= m_layers.size() || from == to)
            return;
        auto layer = std::move(m_layers[from]);
        m_layers.erase(m_layers.begin() + static_cast<std::ptrdiff_t>(from));
        m_layers.insert(m_layers.begin() + static_cast<std::ptrdiff_t>(to), std::move(layer));
        m_weights.MoveLayer(from, to);
        MarkLayersChanged();
        MarkWeightsChanged();
    }

    void TerrainLayerStack::SetLayers(std::vector<TerrainLayer> layers)
    {
        if (layers.size() > kMaxLayers)
            layers.resize(kMaxLayers);
        if (layers == m_layers)
            return;
        m_layers = std::move(layers);
        MarkLayersChanged();
    }

    void TerrainLayerStack::SetHeightBlend(float heightBlend)
    {
        heightBlend = std::clamp(heightBlend, 0.0f, 1.0f);
        if (heightBlend == m_heightBlend)
            return;
        m_heightBlend = heightBlend;
        MarkLayersChanged();
    }

    void TerrainLayerStack::SetDetailDistance(float distance)
    {
        distance = std::max(distance, 0.0f);
        if (distance == m_detailDistance)
            return;
        m_detailDistance = distance;
        MarkLayersChanged();
    }

    TerrainWeightRect TerrainLayerStack::Paint(glm::vec2 uv, glm::vec2 radiusUv, std::size_t layer, const TerrainLayerBrush &brush)
    {
        if (layer >= m_layers.size())
            return {};
        const auto changed = m_weights.Paint(uv, radiusUv, layer, brush);
        MarkWeightsChanged(changed);
        return changed;
    }

    void TerrainLayerStack::Fill(std::size_t layer)
    {
        if (layer >= m_layers.size())
            return;
        m_weights.Fill(layer);
        MarkWeightsChanged();
    }

    void TerrainLayerStack::SetWeightResolution(int resolution)
    {
        if (resolution == m_weights.GetResolution())
            return;
        m_weights.Resize(resolution);
        MarkWeightsChanged();
        MarkLayersChanged(); // the weight textures are recreated at the new size
    }

    void TerrainLayerStack::SetWeights(TerrainWeightMap weights)
    {
        const bool resized = weights.GetResolution() != m_weights.GetResolution();
        m_weights = std::move(weights);
        MarkWeightsChanged();
        if (resized)
            MarkLayersChanged();
    }

    void TerrainLayerStack::RestoreWeights(const TerrainWeightMap::Region &region)
    {
        m_weights.Restore(region);
        MarkWeightsChanged(region.rect);
    }

    void TerrainLayerStack::Restore(const Snapshot &snapshot)
    {
        m_layers = snapshot.layers;
        SetWeights(snapshot.weights);
        MarkLayersChanged();
    }

    std::vector<std::uint8_t> TerrainLayerStack::EncodeWeightPlane(const TerrainWeightMap &weights, std::size_t plane)
    {
        const int size = weights.GetResolution();
        const auto pixels = FlipRows(weights.GetPlane(plane), size);
        std::vector<std::uint8_t> png;
        stbi_write_png_to_func([](void *context, void *data, int length) {
            auto *output = static_cast<std::vector<std::uint8_t> *>(context);
            output->insert(output->end(), static_cast<std::uint8_t *>(data), static_cast<std::uint8_t *>(data) + length);
        }, &png, size, size, 4, pixels.data(), size * 4);
        return png;
    }

    bool TerrainLayerStack::DecodeWeightPlanes(std::span<const std::string> files, TerrainWeightMap &weights)
    {
        std::vector<std::vector<std::uint8_t>> planes;
        int resolution = 0;
        for (const auto &file : files)
        {
            if (file.empty() || file.size() > INT_MAX)
                break;
            int width = 0, height = 0, channels = 0;
            auto *decoded = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(file.data()), static_cast<int>(file.size()),
                                                  &width, &height, &channels, 4);
            if (!decoded)
                return false;
            const bool usable = width == height && width > 0 && (resolution == 0 || width == resolution);
            if (usable)
            {
                resolution = width;
                planes.push_back(FlipRows({decoded, static_cast<std::size_t>(width) * height * 4}, width));
            }
            stbi_image_free(decoded);
            if (!usable)
                return false;
        }
        return !planes.empty() && weights.SetPlanes(resolution, planes);
    }

    bool TerrainLayerStack::LoadWeightMaps()
    {
        if (!m_services.readFile || m_weightMapPaths[0].empty())
            return false;
        std::vector<std::string> files;
        for (const auto &reference : m_weightMapPaths)
        {
            if (reference.empty())
                break;
            const auto path = m_services.resolvePath ? m_services.resolvePath(reference) : reference;
            std::string bytes;
            if (path.empty() || !m_services.readFile(path, bytes))
                break;
            files.push_back(std::move(bytes));
        }
        TerrainWeightMap weights;
        if (files.empty() || !DecodeWeightPlanes(files, weights))
            return false;
        SetWeights(std::move(weights));
        return true;
    }

    bool TerrainLayerStack::SaveWeightMaps() const
    {
        const std::size_t planes = PlanesFor(m_layers.size());
        for (std::size_t plane = 0; plane < planes; ++plane)
        {
            const auto path = m_services.resolvePath ? m_services.resolvePath(m_weightMapPaths[plane]) : m_weightMapPaths[plane];
            if (m_weightMapPaths[plane].empty() || path.empty())
                return false;
            const auto png = EncodeWeightPlane(m_weights, plane);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
            if (!output)
                return false;
        }
        return true;
    }

    std::shared_ptr<const render::TerrainLayerSet> TerrainLayerStack::GetRenderSet()
    {
        if (m_layers.empty())
            return nullptr;
        UpdateWeightTextures();
        if (m_renderSet)
            return m_renderSet;
        auto set = std::make_shared<render::TerrainLayerSet>();
        set->heightBlend = m_heightBlend;
        set->detailDistance = m_detailDistance;
        const auto load = [&](const std::string &reference, render::TextureColorSpace space) -> const render::Texture * {
            return reference.empty() || !m_services.loadTexture ? nullptr : m_services.loadTexture(reference, space);
        };
        for (const auto &layer : m_layers)
        {
            render::TerrainLayerSurface surface;
            surface.albedoMap = load(layer.albedoMap, render::TextureColorSpace::SRGB);
            surface.normalMap = load(layer.normalMap, render::TextureColorSpace::Linear);
            surface.roughnessMap = load(layer.roughnessMap, render::TextureColorSpace::Linear);
            surface.heightMap = load(layer.heightMap, render::TextureColorSpace::Linear);
            surface.tint = layer.tint;
            surface.tileSize = std::max(layer.tileSize, 0.001f);
            surface.normalStrength = layer.normalStrength;
            surface.roughness = layer.roughness;
            surface.metallic = layer.metallic;
            surface.antiTiling = layer.antiTiling;
            set->layers.push_back(surface);
        }
        for (std::size_t plane = 0; plane < m_weightTextures.size(); ++plane)
            set->weightMaps[plane] = m_weightTextures[plane].get();
        m_renderSet = std::move(set);
        return m_renderSet;
    }

    const render::Texture *TerrainLayerStack::GetMacroAlbedo()
    {
        if (m_layers.empty())
            return nullptr;
        if (m_macroStale || !m_macroDirty.Empty() || !m_macroAlbedo)
            UpdateMacroAlbedo();
        return m_macroAlbedo.get();
    }

    void TerrainLayerStack::MarkLayersChanged()
    {
        m_renderSet.reset();
        m_meanColours.clear();
        m_macroStale = true;
    }

    void TerrainLayerStack::MarkWeightsChanged()
    {
        m_weightTexturesStale = true;
        m_macroStale = true;
    }

    void TerrainLayerStack::MarkWeightsChanged(const TerrainWeightRect &rect)
    {
        m_weightTexturesDirty.Include(rect);
        m_macroDirty.Include(rect);
    }

    void TerrainLayerStack::UpdateWeightTextures()
    {
        const int size = m_weights.GetResolution();
        const auto &dirty = m_weightTexturesDirty;
        for (std::size_t plane = 0; plane < m_weightTextures.size(); ++plane)
        {
            auto &texture = m_weightTextures[plane];
            if (texture && texture->GetWidth() != size)
            {
                texture.reset();
                m_renderSet.reset();
            }
            if (!texture)
            {
                texture = render::Texture::CreateRgba8(size, size, m_weights.GetPlane(plane), "Terrain weights " + std::to_string(plane));
                m_renderSet.reset();
            }
            else if (m_weightTexturesStale)
                texture->SetRgba8Pixels(m_weights.GetPlane(plane));
            else if (!dirty.Empty())
                // Painting re-uploads only the texels it changed.
                texture->UpdateRgba8Region({dirty.x0, dirty.y0, dirty.x1 - dirty.x0, dirty.y1 - dirty.y0}, m_weights.GetPlane(plane, dirty));
        }
        m_weightTexturesStale = false;
        m_weightTexturesDirty = {};
    }

    glm::vec3 TerrainLayerStack::MeanLinearColour(std::size_t layer)
    {
        if (m_meanColours.size() != m_layers.size())
        {
            m_meanColours.assign(m_layers.size(), glm::vec3(0.5f));
            for (std::size_t index = 0; index < m_layers.size(); ++index)
            {
                const auto &source = m_layers[index];
                const auto *texture = source.albedoMap.empty() || !m_services.loadTexture
                    ? nullptr : m_services.loadTexture(source.albedoMap, render::TextureColorSpace::SRGB);
                const auto pixels = texture ? texture->GetRgba8Pixels() : std::span<const unsigned char>{};
                glm::dvec3 sum(0.0);
                const std::size_t texels = pixels.size() / 4;
                // A strided sample is plenty for a mean colour.
                const std::size_t step = std::max<std::size_t>(1, texels / 65536);
                std::size_t counted = 0;
                for (std::size_t texel = 0; texel < texels; texel += step, ++counted)
                    for (int channel = 0; channel < 3; ++channel)
                        sum[channel] += SrgbToLinear(pixels[texel * 4 + channel] / 255.0f);
                const glm::vec3 mean = counted ? glm::vec3(sum / double(counted)) : glm::vec3(1.0f);
                m_meanColours[index] = mean * source.tint;
            }
        }
        return m_meanColours[layer];
    }

    std::vector<unsigned char> TerrainLayerStack::MacroPixels(const render::TextureRect &rect)
    {
        const int size = std::min(kMacroResolution, m_weights.GetResolution());
        std::vector<glm::vec3> colours(m_layers.size());
        for (std::size_t layer = 0; layer < m_layers.size(); ++layer)
            colours[layer] = MeanLinearColour(layer);
        std::vector<unsigned char> pixels(static_cast<std::size_t>(rect.width) * rect.height * 4);
        auto *pixel = pixels.data();
        for (int y = rect.y; y < rect.y + rect.height; ++y)
            for (int x = rect.x; x < rect.x + rect.width; ++x, pixel += 4)
            {
                const glm::vec2 uv = (glm::vec2(x, y) + 0.5f) / float(size);
                glm::vec3 colour(0.0f);
                for (std::size_t layer = 0; layer < m_layers.size(); ++layer)
                    colour += colours[layer] * m_weights.SampleWeight(uv, layer);
                for (int channel = 0; channel < 3; ++channel)
                    pixel[channel] = static_cast<unsigned char>(std::lround(LinearToSrgb(colour[channel]) * 255.0f));
                pixel[3] = 255;
            }
        return pixels;
    }

    void TerrainLayerStack::UpdateMacroAlbedo()
    {
        const int size = std::min(kMacroResolution, m_weights.GetResolution());
        if (m_macroStale || !m_macroAlbedo || m_macroAlbedo->GetWidth() != size)
        {
            const render::TextureRect whole{0, 0, size, size};
            if (!m_macroAlbedo || m_macroAlbedo->GetWidth() != size || !m_macroAlbedo->SetRgba8Pixels(MacroPixels(whole)))
                m_macroAlbedo = render::Texture::CreateRgba8(size, size, MacroPixels(whole), "Terrain macro albedo");
        }
        else if (!m_macroDirty.Empty())
        {
            // The macro texels over the painted weight texels (one texel of margin for the bilinear weight lookups).
            const float scale = float(size) / float(m_weights.GetResolution());
            const int x0 = std::max(0, static_cast<int>(std::floor(m_macroDirty.x0 * scale)) - 1);
            const int y0 = std::max(0, static_cast<int>(std::floor(m_macroDirty.y0 * scale)) - 1);
            const int x1 = std::min(size, static_cast<int>(std::ceil(m_macroDirty.x1 * scale)) + 1);
            const int y1 = std::min(size, static_cast<int>(std::ceil(m_macroDirty.y1 * scale)) + 1);
            const render::TextureRect rect{x0, y0, x1 - x0, y1 - y0};
            if (!rect.Empty())
                m_macroAlbedo->UpdateRgba8Region(rect, MacroPixels(rect));
        }
        m_macroStale = false;
        m_macroDirty = {};
    }
}
