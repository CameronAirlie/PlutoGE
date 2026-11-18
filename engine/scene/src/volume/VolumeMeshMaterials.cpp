#include "PlutoGE/scene/volume/VolumeMesher.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace PlutoGE::scene::volume
{
    namespace VertexLayerEncoding
    {
        float PackLayers(VolumeMaterial a, VolumeMaterial b, VolumeMaterial c)
        {
            // Below 2^24: exact in a float, and identical on all three vertices, so interpolation leaves it whole.
            return static_cast<float>(static_cast<std::uint32_t>(a) | (static_cast<std::uint32_t>(b) << 8) | (static_cast<std::uint32_t>(c) << 16));
        }

        std::array<VolumeMaterial, 3> UnpackLayers(float packed)
        {
            const auto bits = static_cast<std::uint32_t>(std::max(std::lround(packed), 0l));
            return {static_cast<VolumeMaterial>(bits & 0xffu), static_cast<VolumeMaterial>((bits >> 8) & 0xffu),
                    static_cast<VolumeMaterial>((bits >> 16) & 0xffu)};
        }
    }

    void EncodeTriangleMaterials(VolumeMesh &mesh)
    {
        auto &vertices = mesh.mesh.vertices;
        auto &indices = mesh.mesh.indices;
        mesh.materials.resize(vertices.size(), 0);
        mesh.occlusion.resize(vertices.size(), 1.0f);

        // A vertex keeps its slot for the first layer set that uses it; other sets use a copy, made once per set.
        std::vector<std::int64_t> firstSet(vertices.size(), -1);
        std::unordered_map<std::uint64_t, unsigned> copies;
        const auto vertexFor = [&](unsigned vertex, std::uint32_t set) -> unsigned {
            if (firstSet[vertex] < 0)
                firstSet[vertex] = set;
            if (firstSet[vertex] == static_cast<std::int64_t>(set))
                return vertex;
            const std::uint64_t key = (static_cast<std::uint64_t>(vertex) << 24) | set;
            const auto [found, inserted] = copies.try_emplace(key, static_cast<unsigned>(vertices.size()));
            if (inserted)
            {
                const auto copy = vertices[vertex]; // copied first: push_back may reallocate
                const auto material = mesh.materials[vertex];
                const auto occlusion = mesh.occlusion[vertex];
                vertices.push_back(copy);
                mesh.materials.push_back(material);
                mesh.occlusion.push_back(occlusion);
                firstSet.push_back(set);
            }
            return found->second;
        };

        std::vector<std::uint32_t> vertexSet(vertices.size(), 0);
        for (std::size_t triangle = 0; triangle + 2 < indices.size(); triangle += 3)
        {
            // The triangle's distinct materials in a fixed order (a repeated when fewer than three).
            std::array<VolumeMaterial, 3> layers{mesh.materials[indices[triangle]], mesh.materials[indices[triangle + 1]],
                                                 mesh.materials[indices[triangle + 2]]};
            std::sort(layers.begin(), layers.end());
            const auto end = std::unique(layers.begin(), layers.end());
            for (auto it = end; it != layers.end(); ++it)
                *it = layers[0];
            const std::uint32_t set = layers[0] | (static_cast<std::uint32_t>(layers[1]) << 8) | (static_cast<std::uint32_t>(layers[2]) << 16);
            for (std::size_t corner = 0; corner < 3; ++corner)
                indices[triangle + corner] = vertexFor(indices[triangle + corner], set);
        }

        for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex)
        {
            const std::uint32_t set = firstSet[vertex] < 0 ? mesh.materials[vertex] * 0x010101u : static_cast<std::uint32_t>(firstSet[vertex]);
            const VolumeMaterial a = set & 0xffu, b = (set >> 8) & 0xffu;
            const VolumeMaterial own = mesh.materials[vertex];
            vertices[vertex].uv = {static_cast<float>(set), own == a ? 1.0f : 0.0f};
            vertices[vertex].uv2 = {own != a && own == b ? 1.0f : 0.0f, std::clamp(mesh.occlusion[vertex], 0.0f, 1.0f)};
        }
    }
}
