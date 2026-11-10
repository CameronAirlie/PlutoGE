#include "PlutoGE/ui/FoliageDiagnostics.h"

#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"

#include <algorithm>
#include <iterator>

namespace PlutoGE::ui
{
    std::vector<FoliageIssue> DiagnoseFoliageAsset(const scene::FoliageTypeAsset &asset, const render::Mesh *mesh,
                                                   bool hasImpostor, bool expectsDetailCost)
    {
        std::vector<FoliageIssue> issues;
        if (asset.sourceMeshPath.empty() && !mesh)
        {
            issues.push_back({FoliageIssueSeverity::Warning, "No mesh assigned: this type draws nothing and cannot be painted."});
        }
        else if (!mesh)
        {
            issues.push_back({FoliageIssueSeverity::Error, "The mesh could not be loaded: " + asset.sourceMeshPath});
        }
        else if (expectsDetailCost)
        {
            std::size_t lodCount = 1;
            for (std::size_t submesh = 0; submesh < mesh->GetSubmeshCount(); ++submesh)
                lodCount = (std::max)(lodCount, mesh->GetSubmeshLodCount(submesh));
            if (lodCount <= 1 && !hasImpostor)
                issues.push_back({FoliageIssueSeverity::Info,
                                  "This mesh has no LODs or impostor, so every instance draws at full detail. "
                                  "Turn on Vegetation LODs in the model's import settings."});
        }
        if (!asset.proxyPrefabReference.empty() && !asset.collisionEnabled)
        {
            issues.push_back({FoliageIssueSeverity::Warning,
                              "This type has a proxy prefab but no collision, so rays and weapons pass through its "
                              "instances. Turn on Capsule Collision so gameplay can find the instance it hits."});
        }
        return issues;
    }

    std::vector<FoliageIssue> DiagnoseFoliageType(const scene::FoliageType &type)
    {
        std::vector<FoliageIssue> issues;
        if (!type.assetLoadError.empty())
            issues.push_back({FoliageIssueSeverity::Error,
                              "Could not read the linked asset, so this type uses the scene's copy of its settings. " + type.assetLoadError});
        auto definition = DiagnoseFoliageAsset(type.asset, type.mesh, type.impostor != nullptr, !type.instances.empty());
        issues.insert(issues.end(), std::make_move_iterator(definition.begin()), std::make_move_iterator(definition.end()));
        return issues;
    }

    std::vector<FoliageIssue> DiagnoseFoliagePainting(bool hasEnabledTerrain, const scene::FoliageType *selectedType)
    {
        std::vector<FoliageIssue> issues;
        if (!hasEnabledTerrain)
            issues.push_back({FoliageIssueSeverity::Warning, "Painting needs an enabled Terrain component on this entity."});
        if (selectedType && !selectedType->mesh)
            issues.push_back({FoliageIssueSeverity::Warning, "Assign a mesh to \"" + selectedType->name + "\" before painting it."});
        return issues;
    }

    FoliageIssueSeverity WorstFoliageIssue(const std::vector<FoliageIssue> &issues)
    {
        auto worst = FoliageIssueSeverity::Info;
        for (const auto &issue : issues) worst = (std::max)(worst, issue.severity);
        return worst;
    }
}
