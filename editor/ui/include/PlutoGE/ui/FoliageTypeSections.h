#pragma once

#include "PlutoGE/scene/FoliageTypeAsset.h"
#include "PlutoGE/ui/FoliageDiagnostics.h"

#include <imgui.h>

#include <functional>
#include <vector>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::render
{
    class Mesh;
}

// The controls that edit a foliage type's settings. They change a FoliageTypeAsset value and report it, knowing nothing
// of who owns it, so the inspector (a type in the scene) and the asset editor (a .plutofoliage draft) are the same UI.
// Adding a setting means adding it to FoliageTypeAsset and to one section here.
namespace PlutoGE::ui
{
    struct FoliageTypeSectionContext
    {
        const assets::Project *project = nullptr;
        // The mesh the asset's reference loads, when known: lists parts, sizes wind and reports detail.
        const render::Mesh *mesh = nullptr;
        bool hasImpostor = false;
        bool sceneHasWind = true;
        // Sections still open and close, but their controls are shown disabled.
        bool readOnly = false;
        // Draws actions only the host can perform at the end of the Source section; optional.
        std::function<void()> drawSourceActions;
    };

    [[nodiscard]] ImVec4 FoliageIssueColor(FoliageIssueSeverity severity);
    void DrawFoliageIssues(const std::vector<FoliageIssue> &issues);

    // Each returns true when `asset` changed.
    bool DrawFoliageSourceSection(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context);
    bool DrawFoliagePlacementSection(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context);
    bool DrawFoliageRenderingSection(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context);
    bool DrawFoliageCollisionSection(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context);
    bool DrawFoliageWindSection(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context);
    bool DrawFoliageInteractionSection(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context);

    // All of them, in order.
    bool DrawFoliageTypeSections(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context);

    // Whether `asset`'s selected parts are exactly `indices` (empty for the whole mesh).
    [[nodiscard]] bool FoliageSubmeshSelectionMatches(const scene::FoliageTypeAsset &asset, const std::vector<int> &indices);
}
