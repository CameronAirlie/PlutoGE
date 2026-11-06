#include "PlutoGE/ui/FoliageDiagnostics.h"

#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"

#include <algorithm>

namespace PlutoGE::ui
{
    std::vector<FoliageIssue> DiagnoseFoliageType(const scene::FoliageType &type)
    {
        std::vector<FoliageIssue> issues;
        if (!type.assetLoadError.empty())
            issues.push_back({FoliageIssueSeverity::Error,
                              "Could not read the linked asset, so this type uses the scene's copy of its settings. " + type.assetLoadError});

        if (type.asset.sourceMeshPath.empty() && !type.mesh)
        {
            issues.push_back({FoliageIssueSeverity::Warning, "No mesh assigned: this type draws nothing and cannot be painted."});
        }
        else if (!type.mesh)
        {
            issues.push_back({FoliageIssueSeverity::Error, "The mesh could not be loaded: " + type.asset.sourceMeshPath});
        }
        else if (!type.instances.empty())
        {
            std::size_t lodCount = 1;
            for (std::size_t submesh = 0; submesh < type.mesh->GetSubmeshCount(); ++submesh)
                lodCount = (std::max)(lodCount, type.mesh->GetSubmeshLodCount(submesh));
            if (lodCount <= 1 && !type.impostor)
                issues.push_back({FoliageIssueSeverity::Info,
                                  "This mesh has no LODs or impostor, so every instance draws at full detail. "
                                  "Turn on Vegetation LODs in the model's import settings."});
        }
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
