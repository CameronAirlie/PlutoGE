#pragma once

#include "PlutoGE/render/Material.h"
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
        // Silhouettes from geometry (trunks, branches, rocks): simplified.
        Solid,
        // Alpha-tested or blended foliage (leaves): each modelled leaf becomes one card (FoliageCards.h), then whole
        // leaves are dropped, spread evenly, and the rest enlarged to keep the canopy's coverage. Never simplified,
        // which collapses leaves into stray triangles.
        Cards,
    };

    // A submesh's surface from its material: anything not opaque is foliage cards.
    [[nodiscard]] inline TriangleBudgetSurface SurfaceFor(render::AlphaMode alphaMode) noexcept
    {
        return alphaMode == render::AlphaMode::Opaque ? TriangleBudgetSurface::Solid : TriangleBudgetSurface::Cards;
    }

    struct TriangleBudgetResult
    {
        std::size_t sourceTriangles = 0;
        std::size_t keptTriangles = 0;
        bool applied = false;
    };

    // Reduces a static mesh whose submeshes hold more than `budget` triangles in all to about the budget, in place, as
    // the model's new finest geometry. Cards ranges are first made into leaf cards; then each distinct submesh range
    // gets a share of the budget by its triangles, Cards weighted CardBudgetWeight times Solid (leaves define a plant's
    // look, and its branches mostly hide behind them). The source ranges are dropped and unreferenced vertices compacted
    // away. `surfaces` holds one entry per submesh (missing entries are Solid). Submesh LODs are left for
    // GenerateSubmeshLods to build from the result. A mesh within its budget, with no budget (0) or with skin weights is
    // left unchanged. Solid ranges may exceed their share where topology-preserving simplification cannot reach it:
    // the finest LOD never falls back to clustering.
    TriangleBudgetResult ApplyTriangleBudget(render::MeshData &meshData, std::vector<render::Submesh> &submeshes,
                                             std::uint32_t budget, TriangleBudgetSimplification simplification,
                                             std::span<const TriangleBudgetSurface> surfaces = {});

    // Part of the artifact settings of budgeted models: bump whenever ApplyTriangleBudget's output changes, so their
    // cached imports are rebuilt (models without a budget are unaffected).
    inline constexpr std::uint32_t TriangleBudgetVersion = 7;

    inline constexpr float CardBudgetWeight = 6.0f;
}
