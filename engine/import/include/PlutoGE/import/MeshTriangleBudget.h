#pragma once

#include "PlutoGE/render/Mesh.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace PlutoGE::assetimport
{
    // How a budgeted mesh may lose triangles. PruneIslands lets the simplifier drop small disconnected parts first,
    // as the vegetation LOD profile does.
    enum class TriangleBudgetSimplification
    {
        PreserveTopology,
        PruneIslands,
    };

    // What a submesh's triangles draw, which decides how it is reduced.
    enum class TriangleBudgetSurface
    {
        // Silhouettes from geometry (trunks, rocks): simplified, with a larger share of the budget.
        Solid,
        // Alpha-tested cards (leaves): each card is simplified, then whole cards are dropped, spread evenly, and the
        // rest enlarged about their centres to keep the surface's coverage.
        Cards,
    };

    struct TriangleBudgetResult
    {
        std::size_t sourceTriangles = 0;
        std::size_t keptTriangles = 0;
        bool applied = false;
    };

    // Reduces a static mesh whose submeshes hold more than `budget` triangles in all to about the budget, in place, as
    // the model's new finest geometry. Each distinct submesh range gets a share of the budget by its triangles, Solid
    // ones weighted SolidBudgetWeight times Cards; the source ranges are dropped and unreferenced vertices compacted
    // away. `surfaces` holds one entry per submesh (missing entries are Solid). Submesh LODs are left for
    // GenerateSubmeshLods to build from the result. A mesh within its budget, with no budget (0) or with skin weights is
    // left unchanged. The result exceeds the budget where topology-preserving simplification cannot reach it: the
    // finest LOD never falls back to clustering.
    TriangleBudgetResult ApplyTriangleBudget(render::MeshData &meshData, std::vector<render::Submesh> &submeshes,
                                             std::uint32_t budget, TriangleBudgetSimplification simplification,
                                             std::span<const TriangleBudgetSurface> surfaces = {});

    // Part of the artifact settings of budgeted models: bump whenever ApplyTriangleBudget's output changes, so their
    // cached imports are rebuilt (models without a budget are unaffected).
    inline constexpr std::uint32_t TriangleBudgetVersion = 5;

    inline constexpr float SolidBudgetWeight = 4.0f;
    // The most a card is enlarged (in each direction) to make up for dropped cards.
    inline constexpr float MaxCardEnlargement = 1.75f;
}
