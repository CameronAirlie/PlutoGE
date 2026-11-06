#pragma once

#include "PlutoGE/scene/components/FoliageComponent.h"

#include <string>
#include <vector>

// Plain-language checks of foliage setups, shown in the inspector and viewport tool bar. They read data only, so they
// can be tested without a UI, and new checks need no changes to whatever shows them.
namespace PlutoGE::ui
{
    enum class FoliageIssueSeverity
    {
        Info,    // Works, but probably not as intended.
        Warning, // Something will not work until it is fixed.
        Error,   // Authored data could not be used.
    };

    struct FoliageIssue
    {
        FoliageIssueSeverity severity = FoliageIssueSeverity::Info;
        std::string message;
    };

    // What is wrong with a definition drawing `mesh` (null when it did not load). `expectsDetailCost` adds the note about
    // meshes without LODs, which only matters once instances exist.
    [[nodiscard]] std::vector<FoliageIssue> DiagnoseFoliageAsset(const scene::FoliageTypeAsset &asset, const render::Mesh *mesh,
                                                                 bool hasImpostor, bool expectsDetailCost);

    // What is wrong with one type's definition.
    [[nodiscard]] std::vector<FoliageIssue> DiagnoseFoliageType(const scene::FoliageType &type);

    // Why painting would do nothing right now; empty when it would work. `selectedType` may be null.
    [[nodiscard]] std::vector<FoliageIssue> DiagnoseFoliagePainting(bool hasEnabledTerrain, const scene::FoliageType *selectedType);

    // The most severe of `issues`, for a one-glyph summary. Info when there are none.
    [[nodiscard]] FoliageIssueSeverity WorstFoliageIssue(const std::vector<FoliageIssue> &issues);
}
