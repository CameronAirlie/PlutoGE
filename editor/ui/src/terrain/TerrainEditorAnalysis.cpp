#include "PlutoGE/ui/TerrainEditorAnalysis.h"

#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/components/DetailGrassComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/ProceduralTerrainComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/TerrainWorldComponent.h"
#include "PlutoGE/ui/TerrainCreation.h"

#include <algorithm>
#include <set>

namespace PlutoGE::ui
{
    namespace
    {
        using Severity = TerrainEditorIssue::Severity;
        using Section = TerrainEditorSection;
    }

    const char *TerrainEditorSectionName(TerrainEditorSection section)
    {
        switch (section)
        {
        case Section::Sculpt: return "Sculpt";
        case Section::Paint: return "Paint";
        case Section::Biomes: return "Biomes";
        case Section::Grass: return "Grass";
        case Section::Heights: return "Heights";
        case Section::World: return "World";
        case Section::Setup: return "Setup";
        }
        return "";
    }

    std::vector<TerrainEditorIssue> AnalyzeTerrain(const TerrainEditTarget &target)
    {
        std::vector<TerrainEditorIssue> issues;
        if (!target)
            return issues;
        const auto add = [&](Severity severity, std::string message, Section section) {
            issues.push_back({severity, std::move(message), section});
        };

        if (target.kind == TerrainEditTarget::Kind::World && target.world)
        {
            if (!target.settings && target.world->GetTilePrefab().empty())
                add(Severity::Warning, "Tiles have no Tile Template: no layers, plants or grass. Add one in Setup.", Section::Setup);
            if (!target.world->GetStreamingErrors().empty())
                add(Severity::Warning, target.world->GetStreamingErrors().back(), Section::World);
            if (!target.world->GetStreamInEditor())
                add(Severity::Note, "Tiles do not stream around the editor camera, so there is nothing to sculpt or paint.", Section::World);
        }
        if (!target.settings)
            return issues;

        const auto *terrain = target.Terrain();
        if (terrain && !HasTerrainAddOn(*target.settings, TerrainAddOn::Collision))
            add(Severity::Note, "No collision: characters and physics fall through the ground.", Section::Setup);
        std::set<std::string> layerNames;
        if (terrain)
            for (const auto &layer : terrain->GetLayers().GetLayers())
                layerNames.insert(layer.name);
        if (terrain && layerNames.empty())
            add(Severity::Note, "No material layers: the ground has a single material.", Section::Paint);

        if (const auto *procedural = target.Procedural())
        {
            std::set<std::string> types;
            if (const auto *foliage = target.Foliage())
                for (std::size_t index = 0; index < foliage->GetTypeCount(); ++index)
                    types.insert(foliage->GetType(index)->name);
            for (const auto &biome : procedural->GetDefinition().biomes)
            {
                for (const auto &layer : biome.layers)
                    if (!layerNames.contains(layer.layer))
                        add(Severity::Warning, "Biome \"" + biome.name + "\" paints \"" + layer.layer + "\", which is not a terrain layer.",
                            Section::Biomes);
                for (const auto &scatter : biome.scatter)
                    if (scatter.foliageType.empty())
                        add(Severity::Warning, "A scatter rule of \"" + biome.name + "\" has no foliage type: it grows nothing.", Section::Biomes);
                    else if (!types.contains(scatter.foliageType))
                        add(Severity::Warning, "Biome \"" + biome.name + "\" grows \"" + scatter.foliageType + "\", which is not a foliage type.",
                            Section::Biomes);
            }
            for (const auto &warning : procedural->GetLastWarnings())
                if (std::none_of(issues.begin(), issues.end(), [&](const auto &issue) { return issue.message == warning; }))
                    add(Severity::Warning, warning, Section::Biomes);
        }

        if (const auto *grass = target.Grass())
        {
            for (const auto &layer : grass->GetLayers())
                if (layer.settings.enabled && !layer.densityLayer.empty() && !layerNames.contains(layer.densityLayer))
                    add(Severity::Warning, "Grass \"" + layer.settings.name + "\" grows on \"" + layer.densityLayer +
                                               "\", which is not a terrain layer: it grows nothing.",
                        Section::Grass);
            if (grass->GetLayers().empty())
                add(Severity::Note, "Detail grass has no layers.", Section::Grass);
        }
        return issues;
    }
}
