#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
#include <glm/glm.hpp>

namespace PlutoGE::render
{
    inline constexpr std::uint32_t ShadowClusterTriangleCount = 1024;

    // Contiguous index ranges preserve material/submesh boundaries and require
    // no duplicate GPU geometry. Importers can improve locality independently.
    struct ShadowGeometryCluster
    {
        std::uint32_t firstIndex = 0, indexCount = 0;
        glm::vec3 center{0}, extents{-1};
    };

    inline std::span<const ShadowGeometryCluster> SelectShadowGeometryClusters(
        std::span<const ShadowGeometryCluster> clusters, std::uint32_t firstIndex, std::uint32_t count)
    {
        const auto first = std::lower_bound(clusters.begin(), clusters.end(), firstIndex,
            [](const auto &cluster, auto index) { return cluster.firstIndex + cluster.indexCount <= index; });
        const auto last = std::lower_bound(first, clusters.end(), std::uint64_t(firstIndex) + count,
            [](const auto &cluster, auto index) { return cluster.firstIndex < index; });
        return {first, last};
    }

    template<class Vertex>
    std::vector<ShadowGeometryCluster> BuildShadowGeometryClusters(
        std::span<const Vertex> vertices, std::span<const std::uint32_t> indices)
    {
        constexpr std::uint32_t IndicesPerCluster = ShadowClusterTriangleCount * 3;
        std::vector<ShadowGeometryCluster> result;
        result.reserve((indices.size() + IndicesPerCluster - 1) / IndicesPerCluster);
        for (std::size_t first = 0; first < indices.size(); first += IndicesPerCluster)
        {
            const auto count = std::min<std::size_t>(IndicesPerCluster, indices.size() - first);
            glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
            bool valid = true;
            for (std::size_t i = first; i < first + count; ++i)
            {
                if (indices[i] >= vertices.size()) { valid = false; break; }
                const auto &position = vertices[indices[i]].position;
                const glm::vec3 p(position[0], position[1], position[2]);
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) { valid = false; break; }
                lo = glm::min(lo, p); hi = glm::max(hi, p);
            }
            result.push_back({static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(count),
                valid ? (lo + hi) * 0.5f : glm::vec3(0), valid ? (hi - lo) * 0.5f : glm::vec3(-1)});
        }
        return result;
    }

    inline glm::vec4 ShadowClusterWorldSphere(const ShadowGeometryCluster &cluster,
                                             std::span<const glm::mat4> models)
    {
        if (cluster.extents.x < 0 || models.empty()) return {0, 0, 0, -1};
        glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
        for (const auto &model : models)
        {
            const auto center = glm::vec3(model * glm::vec4(cluster.center, 1));
            const auto extents = glm::abs(glm::vec3(model[0])) * cluster.extents.x +
                glm::abs(glm::vec3(model[1])) * cluster.extents.y + glm::abs(glm::vec3(model[2])) * cluster.extents.z;
            lo = glm::min(lo, center - extents); hi = glm::max(hi, center + extents);
        }
        const auto center = (lo + hi) * 0.5f;
        const float radius = glm::length((hi - lo) * 0.5f);
        if (!std::isfinite(radius)) return {0, 0, 0, -1};
        return {center, radius};
    }
}
