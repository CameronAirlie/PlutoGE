#pragma once

#include "PlutoGE/render/BasicRenderer.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <unordered_map>

namespace PlutoGE::render
{
    struct GeometryRange
    {
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
        std::uint64_t packingGroup = 0;
    };

    inline std::uint64_t GeometryRangeKey(GeometryRange range)
    {
        return (std::uint64_t(range.firstIndex) << 32) | range.indexCount;
    }

    // Resolve duplicate imported primitives to one existing index range. Exact
    // vertex attributes and triangle order are required: no welding, tolerance,
    // transform baking, or change to source assets/object identities is involved.
    // Run once with the GPU mesh upload, never in per-frame draw translation.
    inline std::unordered_map<std::uint64_t, std::uint32_t> FindCanonicalGeometry(const BasicMeshData &data,
                                                                                  std::span<const GeometryRange> ranges)
    {
        if (ranges.size() < 2)
            return {};
        const auto combine = [](std::size_t &hash, std::size_t value) {
            hash ^= value + 0x9e3779b9u + (hash << 6) + (hash >> 2);
        };
        std::vector<std::size_t> vertexHashes;
        vertexHashes.reserve(data.vertices.size());
        for (const auto &vertex : data.vertices)
        {
            std::size_t hash = 0;
            const auto add = [&](const auto &attribute) {
                for (float value : attribute)
                    combine(hash, value == 0.0f ? 0u : std::bit_cast<std::uint32_t>(value));
            };
            add(vertex.position);
            add(vertex.normal);
            add(vertex.uv);
            add(vertex.uv2);
            add(vertex.tangent);
            add(vertex.previousPosition);
            vertexHashes.push_back(hash);
        }
        const auto sameVertex = [](const BasicVertex &a, const BasicVertex &b) {
            return a.position == b.position && a.normal == b.normal && a.uv == b.uv && a.tangent == b.tangent &&
                   a.previousPosition == b.previousPosition && a.uv2 == b.uv2;
        };
        std::unordered_map<std::size_t, std::vector<GeometryRange>> buckets;
        std::unordered_map<std::uint64_t, std::uint32_t> result;
        for (const auto range : ranges)
        {
            const auto key = GeometryRangeKey(range);
            if (result.contains(key) || range.indexCount == 0 ||
                std::uint64_t(range.firstIndex) + range.indexCount > data.indices.size())
                continue;
            std::size_t hash = range.indexCount;
            bool valid = true;
            for (std::uint32_t i = 0; i < range.indexCount; ++i)
            {
                const auto vertexIndex = data.indices[range.firstIndex + i];
                if (vertexIndex >= vertexHashes.size())
                {
                    valid = false;
                    break;
                }
                combine(hash, vertexHashes[vertexIndex]);
            }
            if (!valid)
                continue;
            auto &candidates = buckets[hash];
            auto canonical = range.firstIndex;
            bool found = false;
            for (const auto candidate : candidates)
            {
                if (candidate.indexCount != range.indexCount)
                    continue;
                bool equal = true;
                for (std::uint32_t i = 0; i < range.indexCount && equal; ++i)
                    equal = sameVertex(data.vertices[data.indices[range.firstIndex + i]],
                                       data.vertices[data.indices[candidate.firstIndex + i]]);
                if (equal)
                {
                    canonical = candidate.firstIndex;
                    found = true;
                    break;
                }
            }
            result.emplace(key, canonical);
            if (!found)
                candidates.push_back(range);
        }
        return result;
    }

    struct PackedGeometry
    {
        std::vector<std::uint32_t> indices;
        std::unordered_map<std::uint64_t, std::uint32_t> firstIndices;
    };

    inline PackedGeometry PackGeometryRanges(const BasicMeshData &data, std::span<const GeometryRange> ranges)
    {
        PackedGeometry packed;
        // Preserve the original stream for whole-mesh draws and unsupported
        // ranges. Extra packed ranges let visible neighbouring primitives merge
        // without changing their vertices, assets, culling or LOD selection.
        packed.indices.assign(data.indices.begin(), data.indices.end());
        if (ranges.size() < 2)
            return packed;
        const auto canonical = FindCanonicalGeometry(data, ranges);
        std::unordered_map<std::uint64_t, std::size_t> groupCounts;
        for (const auto range : ranges)
            ++groupCounts[range.packingGroup];
        std::vector<GeometryRange> ordered(ranges.begin(), ranges.end());
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const auto &a, const auto &b) { return a.packingGroup < b.packingGroup; });
        std::unordered_map<std::uint64_t, std::uint32_t> destinations;
        for (const auto range : ordered)
        {
            const auto key = GeometryRangeKey(range);
            const auto found = canonical.find(key);
            if (found == canonical.end())
                continue;
            const GeometryRange source{found->second, range.indexCount};
            const auto sourceKey = GeometryRangeKey(source);
            // A lone primitive in a material/LOD group cannot merge. Keep its
            // original range instead of increasing GPU index-buffer storage.
            if (groupCounts[range.packingGroup] < 2)
            {
                packed.firstIndices.emplace(key, source.firstIndex);
                continue;
            }
            if (!destinations.contains(sourceKey))
            {
                if (packed.indices.size() + source.indexCount > std::numeric_limits<std::uint32_t>::max())
                    continue;
                const auto first = static_cast<std::uint32_t>(packed.indices.size());
                packed.indices.insert(packed.indices.end(), data.indices.begin() + source.firstIndex,
                                      data.indices.begin() + source.firstIndex + source.indexCount);
                destinations.emplace(sourceKey, first);
            }
            packed.firstIndices.emplace(key, destinations.at(sourceKey));
        }
        return packed;
    }
} // namespace PlutoGE::render
