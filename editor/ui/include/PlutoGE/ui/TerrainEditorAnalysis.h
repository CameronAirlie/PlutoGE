#pragma once

#include "PlutoGE/ui/TerrainEditTool.h"

#include <string>
#include <vector>

// What the Terrain Editor flags about a terrain or terrain world: things that stop it looking or playing as meant, and
// where to fix each. No UI, so it can be tested.
namespace PlutoGE::ui
{
    // The Terrain Editor's sections, in the order they are listed.
    enum class TerrainEditorSection
    {
        Sculpt,
        Paint,
        Biomes,
        Grass,
        Heights,
        World,
        Setup,
    };

    struct TerrainEditorIssue
    {
        enum class Severity
        {
            Warning, // something does not work: nothing grows, tiles fail to load
            Note,    // worth knowing: no collision, no layers yet
        };
        Severity severity = Severity::Note;
        std::string message;
        TerrainEditorSection section = TerrainEditorSection::Setup;
    };

    [[nodiscard]] std::vector<TerrainEditorIssue> AnalyzeTerrain(const TerrainEditTarget &target);
    [[nodiscard]] const char *TerrainEditorSectionName(TerrainEditorSection section);
}
