#include "PlutoGE/scene/TerrainWeightMap.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::scene
{
    void TerrainWeightRect::Include(const TerrainWeightRect &other)
    {
        if (other.Empty())
            return;
        if (Empty())
        {
            *this = other;
            return;
        }
        x0 = std::min(x0, other.x0);
        y0 = std::min(y0, other.y0);
        x1 = std::max(x1, other.x1);
        y1 = std::max(y1, other.y1);
    }

    TerrainWeightMap::TerrainWeightMap(int resolution)
        : m_resolution(std::max(resolution, 1)),
          m_weights(static_cast<std::size_t>(m_resolution) * m_resolution * kLayers, 0)
    {
        Fill(0);
    }

    std::uint8_t TerrainWeightMap::GetWeight(int x, int y, std::size_t layer) const
    {
        if (layer >= kLayers)
            return 0;
        x = std::clamp(x, 0, m_resolution - 1);
        y = std::clamp(y, 0, m_resolution - 1);
        return m_weights[Index(x, y) + layer];
    }

    float TerrainWeightMap::SampleWeight(glm::vec2 uv, std::size_t layer) const
    {
        const glm::vec2 texel = glm::clamp(uv, 0.0f, 1.0f) * float(m_resolution) - 0.5f;
        const glm::vec2 base = glm::floor(texel);
        const glm::vec2 t = texel - base;
        const int x = static_cast<int>(base.x), y = static_cast<int>(base.y);
        const auto weight = [&](int dx, int dy) { return GetWeight(x + dx, y + dy, layer) / 255.0f; };
        return glm::mix(glm::mix(weight(0, 0), weight(1, 0), t.x), glm::mix(weight(0, 1), weight(1, 1), t.x), t.y);
    }

    std::size_t TerrainWeightMap::DominantLayer(glm::vec2 uv) const
    {
        std::size_t best = 0;
        float bestWeight = -1.0f;
        for (std::size_t layer = 0; layer < kLayers; ++layer)
            if (const float weight = SampleWeight(uv, layer); weight > bestWeight)
            {
                best = layer;
                bestWeight = weight;
            }
        return best;
    }

    void TerrainWeightMap::Normalize(std::uint8_t *texel) const
    {
        int sum = 0;
        for (std::size_t layer = 0; layer < kLayers; ++layer)
            sum += texel[layer];
        if (sum == 255)
            return;
        if (sum == 0)
        {
            texel[0] = 255;
            return;
        }
        // Scale, then hand the rounding remainder to the heaviest layer so the sum is exact.
        int scaledSum = 0;
        std::size_t heaviest = 0;
        for (std::size_t layer = 0; layer < kLayers; ++layer)
        {
            texel[layer] = static_cast<std::uint8_t>(texel[layer] * 255 / sum);
            scaledSum += texel[layer];
            if (texel[layer] > texel[heaviest])
                heaviest = layer;
        }
        texel[heaviest] = static_cast<std::uint8_t>(texel[heaviest] + (255 - scaledSum));
    }

    TerrainWeightRect TerrainWeightMap::Paint(glm::vec2 centre, glm::vec2 radius, std::size_t layer,
                                              const TerrainLayerBrush &brush)
    {
        if (layer >= kLayers || radius.x <= 0.0f || radius.y <= 0.0f || brush.strength <= 0.0f)
            return {};
        const float size = float(m_resolution);
        TerrainWeightRect rect{
            std::max(0, static_cast<int>(std::floor((centre.x - radius.x) * size))),
            std::max(0, static_cast<int>(std::floor((centre.y - radius.y) * size))),
            std::min(m_resolution, static_cast<int>(std::ceil((centre.x + radius.x) * size)) + 1),
            std::min(m_resolution, static_cast<int>(std::ceil((centre.y + radius.y) * size)) + 1)};
        if (rect.Empty())
            return {};
        const float hardness = std::clamp(brush.hardness, 0.0f, 0.999f);
        const float strength = std::clamp(brush.strength, 0.0f, 1.0f);
        TerrainWeightRect changed;
        for (int y = rect.y0; y < rect.y1; ++y)
            for (int x = rect.x0; x < rect.x1; ++x)
            {
                const glm::vec2 offset = (glm::vec2(x + 0.5f, y + 0.5f) / size - centre) / radius;
                const float distance = glm::length(offset);
                if (distance >= 1.0f)
                    continue;
                // Full strength inside the hard core, smoothly to nothing at the rim.
                const float falloff = 1.0f - glm::smoothstep(hardness, 1.0f, distance);
                const float amount = strength * falloff;
                auto *texel = &m_weights[Index(x, y)];
                const float current = texel[layer] / 255.0f;
                if (current >= 1.0f || amount <= 0.0f)
                    continue;
                // Raise the painted layer towards 1 and scale the others down so the weights still sum to 1.
                // Truncating never raises another layer (scale < 1) and always makes progress towards a full
                // layer (rounding could stall just short of it); the painted layer takes the exact rest.
                std::array<std::uint8_t, kLayers> before;
                std::copy_n(texel, kLayers, before.begin());
                const float raised = current + (1.0f - current) * amount;
                const float scale = (1.0f - raised) / (1.0f - current);
                int others = 0;
                for (std::size_t other = 0; other < kLayers; ++other)
                    if (other != layer)
                    {
                        texel[other] = static_cast<std::uint8_t>(texel[other] * scale);
                        others += texel[other];
                    }
                texel[layer] = static_cast<std::uint8_t>(255 - others);
                if (!std::equal(before.begin(), before.end(), texel))
                    changed.Include({x, y, x + 1, y + 1});
            }
        return changed;
    }

    void TerrainWeightMap::Fill(std::size_t layer)
    {
        if (layer >= kLayers)
            return;
        std::fill(m_weights.begin(), m_weights.end(), std::uint8_t{0});
        for (std::size_t texel = 0; texel < m_weights.size(); texel += kLayers)
            m_weights[texel + layer] = 255;
    }

    void TerrainWeightMap::RemoveLayer(std::size_t layer)
    {
        if (layer >= kLayers)
            return;
        for (std::size_t texel = 0; texel < m_weights.size(); texel += kLayers)
        {
            auto *weights = &m_weights[texel];
            for (std::size_t index = layer; index + 1 < kLayers; ++index)
                weights[index] = weights[index + 1];
            weights[kLayers - 1] = 0;
            Normalize(weights);
        }
    }

    void TerrainWeightMap::MoveLayer(std::size_t from, std::size_t to)
    {
        if (from >= kLayers || to >= kLayers || from == to)
            return;
        for (std::size_t texel = 0; texel < m_weights.size(); texel += kLayers)
        {
            auto *weights = &m_weights[texel];
            if (from < to)
                std::rotate(weights + from, weights + from + 1, weights + to + 1);
            else
                std::rotate(weights + to, weights + from, weights + from + 1);
        }
    }

    void TerrainWeightMap::Resize(int resolution)
    {
        resolution = std::max(resolution, 1);
        if (resolution == m_resolution)
            return;
        TerrainWeightMap resized(resolution);
        for (int y = 0; y < resolution; ++y)
            for (int x = 0; x < resolution; ++x)
            {
                const glm::vec2 uv = (glm::vec2(x, y) + 0.5f) / float(resolution);
                auto *texel = &resized.m_weights[resized.Index(x, y)];
                for (std::size_t layer = 0; layer < kLayers; ++layer)
                    texel[layer] = static_cast<std::uint8_t>(std::lround(SampleWeight(uv, layer) * 255.0f));
                Normalize(texel);
            }
        *this = std::move(resized);
    }

    std::vector<std::uint8_t> TerrainWeightMap::GetPlane(std::size_t plane) const
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(m_resolution) * m_resolution * 4, 0);
        if (plane >= kPlanes)
            return pixels;
        for (std::size_t texel = 0, pixel = 0; texel < m_weights.size(); texel += kLayers, pixel += 4)
            std::copy_n(&m_weights[texel + plane * 4], 4, &pixels[pixel]);
        return pixels;
    }

    std::vector<std::uint8_t> TerrainWeightMap::GetPlane(std::size_t plane, const TerrainWeightRect &rect) const
    {
        const TerrainWeightRect clipped{std::max(rect.x0, 0), std::max(rect.y0, 0), std::min(rect.x1, m_resolution),
                                        std::min(rect.y1, m_resolution)};
        if (clipped.Empty() || plane >= kPlanes)
            return {};
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(clipped.x1 - clipped.x0) * (clipped.y1 - clipped.y0) * 4);
        auto *pixel = pixels.data();
        for (int y = clipped.y0; y < clipped.y1; ++y)
            for (int x = clipped.x0; x < clipped.x1; ++x, pixel += 4)
                std::copy_n(&m_weights[Index(x, y) + plane * 4], 4, pixel);
        return pixels;
    }

    bool TerrainWeightMap::SetPlanes(int resolution, std::span<const std::vector<std::uint8_t>> planes)
    {
        const std::size_t texels = static_cast<std::size_t>(std::max(resolution, 0)) * std::max(resolution, 0);
        if (resolution <= 0 || planes.empty() || planes.size() > kPlanes ||
            std::ranges::any_of(planes, [&](const auto &plane) { return plane.size() != texels * 4; }))
            return false;
        m_resolution = resolution;
        m_weights.assign(texels * kLayers, 0);
        for (std::size_t plane = 0; plane < planes.size(); ++plane)
            for (std::size_t texel = 0; texel < texels; ++texel)
                std::copy_n(&planes[plane][texel * 4], 4, &m_weights[texel * kLayers + plane * 4]);
        for (std::size_t texel = 0; texel < m_weights.size(); texel += kLayers)
            Normalize(&m_weights[texel]);
        return true;
    }

    TerrainWeightMap::Region TerrainWeightMap::Capture(const TerrainWeightRect &rect) const
    {
        Region region{{std::max(rect.x0, 0), std::max(rect.y0, 0), std::min(rect.x1, m_resolution), std::min(rect.y1, m_resolution)}, {}};
        if (region.rect.Empty())
            return region;
        const std::size_t rowBytes = static_cast<std::size_t>(region.rect.x1 - region.rect.x0) * kLayers;
        region.weights.resize(rowBytes * (region.rect.y1 - region.rect.y0));
        for (int y = region.rect.y0; y < region.rect.y1; ++y)
            std::copy_n(&m_weights[Index(region.rect.x0, y)], rowBytes, &region.weights[(y - region.rect.y0) * rowBytes]);
        return region;
    }

    void TerrainWeightMap::Restore(const Region &region)
    {
        const auto &rect = region.rect;
        if (rect.Empty() || rect.x0 < 0 || rect.y0 < 0 || rect.x1 > m_resolution || rect.y1 > m_resolution)
            return;
        const std::size_t rowBytes = static_cast<std::size_t>(rect.x1 - rect.x0) * kLayers;
        if (region.weights.size() != rowBytes * (rect.y1 - rect.y0))
            return;
        for (int y = rect.y0; y < rect.y1; ++y)
            std::copy_n(&region.weights[(y - rect.y0) * rowBytes], rowBytes, &m_weights[Index(rect.x0, y)]);
    }
}
