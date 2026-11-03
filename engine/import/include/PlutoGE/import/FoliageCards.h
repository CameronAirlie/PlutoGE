#pragma once

#include "PlutoGE/render/Mesh.h"

#include <cstdint>
#include <span>
#include <vector>

namespace PlutoGE::assetimport
{
    // Geometry with its own vertices: indices refer to `vertices`, not to a mesh. Card functions return it so they can
    // run in parallel over one mesh, and callers append it where they choose.
    struct CardGeometry
    {
        std::vector<render::MeshVertexData> vertices;
        std::vector<std::uint32_t> indices;

        [[nodiscard]] std::size_t Triangles() const noexcept { return indices.size() / 3; }
    };

    // The most a kept card is enlarged (in each direction) to make up for the cards a reduction drops.
    inline constexpr float MaxCardEnlargement = 2.0f;

    // Alpha-tested foliage as cards: one textured quad per modelled leaf. A leaf (a connected part of the index list)
    // whose texture coordinates map affinely onto its surface becomes the quad over its UV rectangle, in the plane that
    // fits it, so the texture's alpha keeps the leaf's outline while 2 triangles replace its own (scanned leaves carry
    // 20 or more). Parts that do not fit (twigs, wrapped UVs) and parts of fewer than 4 triangles are kept as they are.
    CardGeometry MakeLeafCards(const render::MeshData &mesh, std::span<const std::uint32_t> indices);

    // An evenly spread subset of the cards' parts within `targetTriangles` (at least one part), each enlarged about its
    // centre so that together they cover about as much as all of them did (at most MaxCardEnlargement). Foliage keeps
    // its outline and density as it loses leaves, instead of its leaves collapsing into stray triangles.
    CardGeometry ThinCards(const CardGeometry &cards, std::uint32_t targetTriangles);
}
