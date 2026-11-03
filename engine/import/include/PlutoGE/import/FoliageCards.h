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
        // MakeLeafCards: how many source triangles became cards, and how many it was given.
        std::size_t cardedSourceTriangles = 0;
        std::size_t sourceTriangles = 0;

        [[nodiscard]] std::size_t Triangles() const noexcept { return indices.size() / 3; }
        // Whether cards stand for most of the source: many small flat leaves (a canopy), not a few large ones whose
        // shape is their geometry (broadleaf plants), which are better simplified than carded or thinned.
        [[nodiscard]] bool MostlyCards() const noexcept { return cardedSourceTriangles * 2 > sourceTriangles; }
    };

    // Which modelled leaves become cards: small (scanned canopy leaves are about 20 triangles), nearly flat (the fitted
    // plane strays at most this share of the leaf's size from it), and filling enough of their UV rectangle that the
    // card shows the leaf, not neighbouring atlas content.
    inline constexpr std::size_t MaxCardLeafTriangles = 64;
    inline constexpr float MaxCardFitError = 0.15f;
    inline constexpr float MinCardUvFill = 0.3f;

    // The most a kept card is enlarged (in each direction) to make up for the cards a reduction drops.
    inline constexpr float MaxCardEnlargement = 2.0f;

    // Alpha-tested foliage as cards: one textured quad per modelled leaf. A leaf (a connected part of the index list)
    // that passes the card tests above becomes the quad over its UV rectangle, in the plane that fits it, so the
    // texture's alpha keeps the leaf's outline while 2 triangles replace its own. Other parts (large or curved leaves,
    // stems, twigs, wrapped UVs) are kept as they are; so are parts of fewer than 4 triangles, which are already cards
    // (authored leaf cards) and count as carded.
    CardGeometry MakeLeafCards(const render::MeshData &mesh, std::span<const std::uint32_t> indices);

    // An evenly spread subset of the cards' parts within `targetTriangles` (at least one part), each enlarged about its
    // centre so that together they cover about as much as all of them did (at most MaxCardEnlargement). Foliage keeps
    // its outline and density as it loses leaves, instead of its leaves collapsing into stray triangles.
    CardGeometry ThinCards(const CardGeometry &cards, std::uint32_t targetTriangles);
}
