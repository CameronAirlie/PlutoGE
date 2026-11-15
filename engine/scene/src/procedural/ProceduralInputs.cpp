#include "PlutoGE/scene/procedural/ProceduralInputs.h"

#include <stb_image.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>

namespace PlutoGE::scene::procedural
{
    namespace
    {
        float SegmentDistance(glm::vec2 point, glm::vec2 a, glm::vec2 b)
        {
            const glm::vec2 edge = b - a;
            const float lengthSquared = glm::dot(edge, edge);
            const float t = lengthSquared > 0.0f ? std::clamp(glm::dot(point - a, edge) / lengthSquared, 0.0f, 1.0f) : 0.0f;
            return glm::length(point - (a + edge * t));
        }

        constexpr float RadToDeg = 57.29577951308232f;
    }

    float ProceduralMask::Sample(glm::vec2 world) const
    {
        if (!values || !values->Valid())
            return 0.0f;
        const glm::vec2 uv = (world - worldMin) / glm::max(worldSize, glm::vec2(1e-6f));
        return values->Sample(uv.x * static_cast<float>(values->width - 1), uv.y * static_cast<float>(values->depth - 1));
    }

    std::optional<TerrainHeightField> DecodeMaskImage(std::span<const std::uint8_t> bytes, int channel, std::string *error)
    {
        const auto fail = [&](std::string message) -> std::optional<TerrainHeightField> {
            if (error)
                *error = std::move(message);
            return std::nullopt;
        };
        if (bytes.empty() || bytes.size() > static_cast<std::size_t>(INT_MAX))
            return fail("The mask image is empty or too large.");
        channel = std::clamp(channel, 0, 3);
        const auto *data = reinterpret_cast<const stbi_uc *>(bytes.data());
        const int size = static_cast<int>(bytes.size());
        int width = 0, height = 0, channels = 0;
        TerrainHeightField field;
        if (stbi_is_16_bit_from_memory(data, size))
        {
            stbi_us *pixels = stbi_load_16_from_memory(data, size, &width, &height, &channels, 4);
            if (!pixels)
                return fail(std::string("Could not decode the mask image: ") + stbi_failure_reason());
            field.samples.resize(static_cast<std::size_t>(width) * height);
            for (std::size_t index = 0; index < field.samples.size(); ++index)
                field.samples[index] = static_cast<float>(pixels[index * 4 + channel]) / 65535.0f;
            stbi_image_free(pixels);
        }
        else
        {
            stbi_uc *pixels = stbi_load_from_memory(data, size, &width, &height, &channels, 4);
            if (!pixels)
                return fail(std::string("Could not decode the mask image: ") + stbi_failure_reason());
            field.samples.resize(static_cast<std::size_t>(width) * height);
            for (std::size_t index = 0; index < field.samples.size(); ++index)
                field.samples[index] = static_cast<float>(pixels[index * 4 + channel]) / 255.0f;
            stbi_image_free(pixels);
        }
        field.width = width;
        field.depth = height;
        if (!field.Valid())
            return fail("A mask needs at least 2 x 2 pixels.");
        // Top row first in the file; the far edge in the world.
        for (int z = 0; z < field.depth / 2; ++z)
            std::swap_ranges(field.samples.begin() + static_cast<std::ptrdiff_t>(z) * field.width,
                             field.samples.begin() + static_cast<std::ptrdiff_t>(z + 1) * field.width,
                             field.samples.begin() + static_cast<std::ptrdiff_t>(field.depth - 1 - z) * field.width);
        return field;
    }

    void PolylineDistanceSet::Add(std::string_view sourceName, std::span<const glm::vec2> points, float halfWidth)
    {
        auto &source = m_sources[std::string(sourceName)];
        halfWidth = std::max(halfWidth, 0.0f);
        const auto cellOf = [this](float value) { return static_cast<int>(std::floor(value / m_cellSize)); };
        for (std::size_t index = 0; index + 1 < points.size(); ++index)
        {
            const Segment segment{points[index], points[index + 1], halfWidth};
            const auto id = static_cast<std::uint32_t>(source.segments.size());
            source.segments.push_back(segment);
            const glm::vec2 low = glm::min(segment.a, segment.b) - halfWidth;
            const glm::vec2 high = glm::max(segment.a, segment.b) + halfWidth;
            for (int z = cellOf(low.y); z <= cellOf(high.y); ++z)
                for (int x = cellOf(low.x); x <= cellOf(high.x); ++x)
                    source.cells[{x, z}].push_back(id);
        }
        if (points.size() == 1)
        {
            // A single point is a round source (a pond, a spring).
            const auto id = static_cast<std::uint32_t>(source.segments.size());
            source.segments.push_back({points[0], points[0], halfWidth});
            for (int z = cellOf(points[0].y - halfWidth); z <= cellOf(points[0].y + halfWidth); ++z)
                for (int x = cellOf(points[0].x - halfWidth); x <= cellOf(points[0].x + halfWidth); ++x)
                    source.cells[{x, z}].push_back(id);
        }
    }

    float PolylineDistanceSet::Distance(std::string_view sourceName, glm::vec2 world, float limit) const
    {
        const auto found = m_sources.find(sourceName);
        limit = std::max(limit, 0.0f);
        if (found == m_sources.end() || found->second.segments.empty())
            return limit;
        const auto &source = found->second;
        float best = limit;
        const auto test = [&](const Segment &segment) {
            best = std::min(best, std::max(SegmentDistance(world, segment.a, segment.b) - segment.halfWidth, 0.0f));
        };
        // Search the cells within the limit; past a few hundred cells, testing every segment is cheaper.
        const int reach = static_cast<int>(std::ceil(limit / m_cellSize));
        if (!std::isfinite(limit) || static_cast<std::size_t>(2 * reach + 1) * (2 * reach + 1) > source.segments.size() * 4 + 64)
        {
            for (const auto &segment : source.segments)
                test(segment);
            return best;
        }
        const int cx = static_cast<int>(std::floor(world.x / m_cellSize));
        const int cz = static_cast<int>(std::floor(world.y / m_cellSize));
        for (int z = cz - reach; z <= cz + reach; ++z)
            for (int x = cx - reach; x <= cx + reach; ++x)
                if (const auto cell = source.cells.find({x, z}); cell != source.cells.end())
                    for (const auto id : cell->second)
                        test(source.segments[id]);
        return best;
    }

    float TerrainSurface::At(int x, int z) const
    {
        x = std::clamp(x, -Margin, m_columns - 1 + Margin);
        z = std::clamp(z, -Margin, m_rows - 1 + Margin);
        return m_heights[Index(x, z)];
    }

    float TerrainSurface::Height(float x, float z) const
    {
        const float fx = std::floor(x), fz = std::floor(z);
        const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
        const float tx = x - fx, tz = z - fz;
        const float lower = At(ix, iz) + (At(ix + 1, iz) - At(ix, iz)) * tx;
        const float upper = At(ix, iz + 1) + (At(ix + 1, iz + 1) - At(ix, iz + 1)) * tx;
        return lower + (upper - lower) * tz;
    }

    glm::vec3 TerrainSurface::NormalAt(glm::vec2 local) const
    {
        const float x = local.x / m_cellSize, z = local.y / m_cellSize;
        const float dx = (Height(x + 1.0f, z) - Height(x - 1.0f, z)) / (2.0f * m_cellSize);
        const float dz = (Height(x, z + 1.0f) - Height(x, z - 1.0f)) / (2.0f * m_cellSize);
        return glm::normalize(glm::vec3(-dx, 1.0f, -dz));
    }

    TerrainPoint TerrainSurface::PointAt(glm::vec2 local) const
    {
        const float x = local.x / m_cellSize, z = local.y / m_cellSize;
        const float centre = Height(x, z);
        const float left = Height(x - 1.0f, z), right = Height(x + 1.0f, z);
        const float down = Height(x, z - 1.0f), up = Height(x, z + 1.0f);
        const float dx = (right - left) / (2.0f * m_cellSize);
        const float dz = (up - down) / (2.0f * m_cellSize);
        TerrainPoint point;
        const glm::vec4 world = m_localToWorld * glm::vec4(local.x, centre, local.y, 1.0f);
        point.world = {world.x, world.z};
        point.height = world.y;
        point.slope = std::atan(std::sqrt(dx * dx + dz * dz)) * RadToDeg;
        point.curvature = (left + right + down + up - 4.0f * centre) / (m_cellSize * m_cellSize);
        return point;
    }

    glm::vec2 TerrainSurface::LocalToWorldXZ(glm::vec2 local, float height) const
    {
        const glm::vec4 world = m_localToWorld * glm::vec4(local.x, height, local.y, 1.0f);
        return {world.x, world.z};
    }

    float ProceduralInputs::Mask(std::string_view name, glm::vec2 world) const
    {
        const auto found = m_masks.find(name);
        return found != m_masks.end() && found->second ? found->second->Sample(world) : 0.0f;
    }

    float ProceduralInputs::Distance(std::string_view source, glm::vec2 world, float limit) const
    {
        return m_distances.Distance(source, world, limit);
    }
}
