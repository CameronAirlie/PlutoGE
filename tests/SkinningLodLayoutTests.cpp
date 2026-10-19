#include "SkinningLodLayout.h"
#include "PlutoGE/render/Mesh.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace PlutoGE::render;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    // Body: a 4x4 vertex grid (LOD0 all 9 quads, LOD1 the outer corners, LOD2 one
    // triangle). Eyes: two triangles with no generated LODs. Vertex 22 is unused.
    MeshConfig MakeCharacter()
    {
        MeshConfig config;
        config.data.vertices.resize(23);
        for (std::uint32_t i = 0; i < config.data.vertices.size(); ++i)
            config.data.vertices[i].position = {float(i % 4), float(i / 4), 0};
        auto &indices = config.data.indices;
        for (std::uint32_t y = 0; y < 3; ++y)
            for (std::uint32_t x = 0; x < 3; ++x)
            {
                const std::uint32_t v = y * 4 + x;
                indices.insert(indices.end(), {v, v + 1, v + 4, v + 1, v + 5, v + 4});
            }
        Submesh body{.indexOffset = 0, .indexCount = static_cast<std::uint32_t>(indices.size())};
        body.lods.push_back({body.indexOffset, body.indexCount});
        const auto lod1 = static_cast<std::uint32_t>(indices.size());
        indices.insert(indices.end(), {0, 3, 12, 3, 15, 12});
        body.lods.push_back({lod1, 6});
        const auto lod2 = static_cast<std::uint32_t>(indices.size());
        indices.insert(indices.end(), {0, 15, 12});
        body.lods.push_back({lod2, 3});
        const auto eyes = static_cast<std::uint32_t>(indices.size());
        indices.insert(indices.end(), {16, 17, 18, 19, 20, 21});
        Submesh eyeSubmesh{.indexOffset = eyes, .indexCount = 6, .materialIndex = 1};
        config.submeshes = {body, eyeSubmesh};
        return config;
    }

    std::vector<std::uint32_t> Slots(const SkinningLodLayout &layout, const Mesh &mesh, std::size_t submesh, std::size_t lod)
    {
        const auto range = mesh.GetSubmeshLodRange(submesh, lod);
        return {layout.indices.begin() + range.indexOffset, layout.indices.begin() + range.indexOffset + range.indexCount};
    }
}

int main()
{
    try
    {
        const Mesh mesh(MakeCharacter());
        const auto layout = BuildSkinningLodLayout(mesh);
        const auto &source = mesh.GetMeshData();

        Require(layout.sourceVertexOrder.size() == source.vertices.size(), "Layout must cover every vertex");
        auto sorted = layout.sourceVertexOrder;
        std::sort(sorted.begin(), sorted.end());
        Require(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end(), "Layout must be a permutation");
        Require(layout.indices.size() == source.indices.size(), "Remapped indices must keep topology");
        for (std::size_t i = 0; i < source.indices.size(); ++i)
            Require(layout.sourceVertexOrder[layout.indices[i]] == source.indices[i], "Remapped index changed its vertex");

        // Level 2: body LOD2 (3 vertices) + eyes (6). Level 1 adds body LOD1's
        // fourth corner. Level 0 is the whole stream, including the unused vertex.
        Require(layout.levelVertexCounts == std::vector<std::uint32_t>{23, 10, 9}, "Unexpected LOD prefix sizes");
        for (std::uint32_t level = 0; level < 3; ++level)
            for (std::size_t lod = level; lod < 3; ++lod)
                for (const auto slot : Slots(layout, mesh, 0, lod))
                    Require(slot < layout.VertexCount(level), "Body LOD " + std::to_string(lod) + " escapes level " + std::to_string(level));
        for (std::uint32_t level = 0; level < 4; ++level)
            for (const auto slot : Slots(layout, mesh, 1, 0))
                Require(slot < layout.VertexCount(level), "LOD-less submesh must be deformed at every level");

        Require(layout.VertexCount(7) == 9, "Levels beyond the coarsest clamp to it");
        Require(layout.FinestLevelWithin(23) == 0 && layout.FinestLevelWithin(10) == 1 &&
                layout.FinestLevelWithin(9) == 2 && layout.FinestLevelWithin(0) == 2, "Deformed prefix maps to the wrong level");
        Require(MinimumSkinnedDrawLod(mesh, 0, 2) == 2 && MinimumSkinnedDrawLod(mesh, 1, 2) == 0 && MinimumSkinnedDrawLod(mesh, 0, 0) == 0,
                "Skinned draw clamp must respect each submesh's LOD count");

        MeshConfig plain = MakeCharacter();
        for (auto &submesh : plain.submeshes) submesh.lods.clear();
        const auto identity = BuildSkinningLodLayout(Mesh(plain));
        Require(identity.sourceVertexOrder.empty() && identity.levelVertexCounts == std::vector<std::uint32_t>{23} &&
                identity.indices == plain.data.indices, "Meshes without LODs keep the identity layout");

        std::cout << "PASS: skinning LOD layout prefixes, permutation, LOD-less submeshes and draw clamps\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
