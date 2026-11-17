#pragma once

#include "PlutoGE/render/Mesh.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace PlutoGE::scene
{
    // More cells than this cost more than they look: pieces are clipped against every neighbouring cell.
    inline constexpr int kMaxFracturePieces = 256;

    struct FractureSettings
    {
        // Voronoi cells to split the mesh into, clamped to [1, kMaxFracturePieces]. Pieces that end up empty or
        // smaller than minPieceSize are dropped, so the result can have fewer.
        int pieceCount = 8;
        // Same seed and input, same pieces.
        std::uint32_t seed = 0;
        // Where the mesh was hit, in mesh space; seeds are pulled towards it by impactBias (0..1), so pieces are
        // smaller there. Points outside the mesh's convex hull are moved onto it.
        std::optional<glm::vec3> impactPoint;
        float impactBias = 0.5f;
        // Pieces whose bounds' largest side is shorter than this (mesh units) are dropped.
        float minPieceSize = 0.0f;
        // Texture repeats per mesh unit on the interior faces (caps).
        float capUvScale = 1.0f;
    };

    struct FracturePiece
    {
        // Positions relative to `centroid`. Surface triangles keep the source's attributes (interpolated where a
        // cell boundary cuts them); interior caps have flat normals and planar UVs and draw with material slot 0.
        render::MeshData mesh;
        // One per material slot used, in ascending slot order; materialIndex is the source submesh's slot.
        std::vector<render::Submesh> submeshes;
        // Centre of mass of the piece's solid (see volume), in mesh space: where the piece's origin sits.
        glm::vec3 centroid{0.0f};
        // Volume of the source's convex hull inside the piece's cell, so the volumes of all pieces add up to the
        // hull's. For a convex source that is the piece's own volume.
        float volume = 0.0f;
        // Bounds of mesh.vertices (relative to centroid).
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
    };

    // Splits a closed triangle mesh into Voronoi pieces (docs/FRACTURE.md). Each piece keeps the source triangles
    // inside its cell, clipped at the cell's walls, and is closed by caps: the faces of (convex hull of the source
    // vertices ∩ cell) that lie on cell walls. A convex source gives watertight pieces; a concave one gives caps that
    // can overhang its hollows. `submeshes` selects the triangles and their material slots (empty: every index,
    // slot 0). Degenerate input (fewer than four points, no volume) gives no pieces. Pure geometry, deterministic.
    [[nodiscard]] std::vector<FracturePiece> FractureMesh(const render::MeshData &source,
                                                          const std::vector<render::Submesh> &submeshes,
                                                          const FractureSettings &settings);
}
