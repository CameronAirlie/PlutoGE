#include "SkinningLodLayout.h"
#include "PlutoGE/render/Mesh.h"

#include <algorithm>
#include <limits>

namespace PlutoGE::render
{
    SkinningLodLayout BuildSkinningLodLayout(const Mesh &mesh)
    {
        const auto &data = mesh.GetMeshData();
        const auto vertexCount = static_cast<std::uint32_t>(data.vertices.size());
        SkinningLodLayout layout;
        std::size_t levelCount = 1;
        for (std::size_t submesh = 0; submesh < mesh.GetSubmeshCount(); ++submesh)
            levelCount = std::max(levelCount, mesh.GetSubmeshLodCount(submesh));
        if (mesh.GetSubmeshCount() == 0 || levelCount == 1)
        {
            layout.indices = data.indices;
            layout.levelVertexCounts = {vertexCount};
            return layout;
        }

        constexpr auto unassigned = std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> slotBySource(vertexCount, unassigned);
        layout.sourceVertexOrder.reserve(vertexCount);
        const auto claim = [&](std::uint32_t source)
        {
            if (source < vertexCount && slotBySource[source] == unassigned)
            {
                slotBySource[source] = static_cast<std::uint32_t>(layout.sourceVertexOrder.size());
                layout.sourceVertexOrder.push_back(source);
            }
        };

        // Level k adds each submesh's LOD min(k, last). Walking from the
        // coarsest level makes every finer level a superset of the next one.
        layout.levelVertexCounts.assign(levelCount, 0);
        for (std::size_t level = levelCount; level-- > 0;)
        {
            for (std::size_t submesh = 0; submesh < mesh.GetSubmeshCount(); ++submesh)
            {
                const auto lod = std::min(level, mesh.GetSubmeshLodCount(submesh) - 1);
                const auto range = mesh.GetSubmeshLodRange(submesh, lod);
                const auto end = std::min<std::size_t>(std::size_t(range.indexOffset) + range.indexCount, data.indices.size());
                for (std::size_t i = range.indexOffset; i < end; ++i)
                    claim(data.indices[i]);
            }
            layout.levelVertexCounts[level] = static_cast<std::uint32_t>(layout.sourceVertexOrder.size());
        }
        // Vertices no submesh references still belong to the full stream.
        for (std::uint32_t source = 0; source < vertexCount; ++source)
            claim(source);
        layout.levelVertexCounts[0] = vertexCount;

        layout.indices.resize(data.indices.size());
        std::transform(data.indices.begin(), data.indices.end(), layout.indices.begin(),
                       [&](std::uint32_t source) { return source < vertexCount ? slotBySource[source] : source; });
        return layout;
    }

    std::uint32_t MinimumSkinnedDrawLod(const Mesh &mesh, std::uint32_t submeshIndex, std::uint32_t deformedLevel)
    {
        const auto lodCount = static_cast<std::uint32_t>(mesh.GetSubmeshLodCount(submeshIndex));
        return std::min(deformedLevel, lodCount - 1);
    }
}
