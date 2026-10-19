#pragma once

#include "PlutoGE/render/Mesh.h"

#include <meshoptimizer.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace PlutoGE::assetimport
{
    // Split-normal (faceted) exports put several attribute seams at almost
    // every vertex. The conservative simplifier locks such vertices and cannot
    // reduce the mesh at all, so LOD generation retries permissively. That mode
    // may merge across any discontinuity it is not told to protect: protect the
    // ones that would visibly tear (texture coordinates and skin influences),
    // and leave normal-only splits to the attribute error metric.
    //
    // Returns meshopt vertex_lock flags for vertices that share a position with
    // a vertex whose UVs, lightmap UVs or skin influences differ.
    inline std::vector<unsigned char> BuildTearingSeamLocks(std::span<const render::MeshVertexData> vertices,
                                                            std::span<const std::uint32_t> globalByLocal,
                                                            std::span<const std::array<float, 3>> localPositions)
    {
        const auto count = localPositions.size();
        std::vector<unsigned int> representative(count);
        meshopt_generatePositionRemap(representative.data(), localPositions.front().data(), count, sizeof(localPositions.front()));
        const auto tears = [&](std::size_t a, std::size_t b)
        {
            const auto &first = vertices[globalByLocal[a]];
            const auto &second = vertices[globalByLocal[b]];
            return first.uv != second.uv || first.uv2 != second.uv2 ||
                   first.joints != second.joints || first.weights != second.weights;
        };
        std::vector<unsigned char> discontinuous(count, 0);
        for (std::size_t local = 0; local < count; ++local)
            if (representative[local] != local && tears(local, representative[local]))
                discontinuous[representative[local]] = 1;
        std::vector<unsigned char> locks(count, 0);
        for (std::size_t local = 0; local < count; ++local)
            if (discontinuous[representative[local]])
                locks[local] = meshopt_SimplifyVertex_Protect;
        return locks;
    }
}
