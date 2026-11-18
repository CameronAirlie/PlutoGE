#include "PlutoGE/scene/volume/VolumeMesher.h"

#include <unordered_map>

namespace PlutoGE::scene::volume
{
    void EncodeTriangleMaterials(VolumeMesh &mesh)
    {
        auto &vertices = mesh.mesh.vertices;
        auto &indices = mesh.mesh.indices;
        if (mesh.materials.size() != vertices.size())
            mesh.materials.resize(vertices.size(), 0);

        // A vertex keeps its own material; triangles of another material use a copy, made once per material.
        std::unordered_map<std::uint64_t, unsigned> copies;
        const auto vertexFor = [&](unsigned vertex, VolumeMaterial material) -> unsigned {
            if (mesh.materials[vertex] == material)
                return vertex;
            const std::uint64_t key = (static_cast<std::uint64_t>(vertex) << 8) | material;
            const auto [found, inserted] = copies.try_emplace(key, static_cast<unsigned>(vertices.size()));
            if (inserted)
            {
                const auto copy = vertices[vertex]; // copied first: push_back may reallocate
                vertices.push_back(copy);
                mesh.materials.push_back(material);
            }
            return found->second;
        };

        for (std::size_t triangle = 0; triangle + 2 < indices.size(); triangle += 3)
        {
            const VolumeMaterial a = mesh.materials[indices[triangle]];
            const VolumeMaterial b = mesh.materials[indices[triangle + 1]];
            const VolumeMaterial c = mesh.materials[indices[triangle + 2]];
            const VolumeMaterial material = (b == c && a != b) ? b : a;
            for (std::size_t corner = 0; corner < 3; ++corner)
                indices[triangle + corner] = vertexFor(indices[triangle + corner], material);
        }

        for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex)
            vertices[vertex].uv = {static_cast<float>(mesh.materials[vertex]), 0.0f};
    }
}
