#pragma once

#include "PlutoGE/ui/TerrainCreation.h"
#include "PlutoGE/ui/TerrainEditTool.h"
#include "PlutoGE/ui/TerrainEditorAnalysis.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <optional>
#include <vector>

namespace PlutoGE::ui
{
    class EditorShell;

    // The Terrain Editor: one place to create and edit terrains and terrain worlds (docs/TERRAIN_WORLDS.md), laid out
    // like the Post Process Editor. It follows the selection (a terrain, a world, a world's tile or its template). With
    // none it offers to create one. Otherwise: a toolbar with the active tool, the problems found, and a list of
    // sections (sculpt, paint, biomes, grass, heights, world, setup) beside the selected one's settings.
    class TerrainEditorPanel final : public Panel
    {
    public:
        explicit TerrainEditorPanel(const PanelConfig &config) : Panel(config) {}
        void Render() override;
        // Opens the panel on a section.
        void Show(TerrainEditorSection section);

    private:
        void DrawEmptyState(EditorShell &shell);
        void DrawToolbar(EditorShell &shell, const TerrainEditTarget &target);
        void DrawProblems(const std::vector<TerrainEditorIssue> &issues);
        void DrawSectionList(const TerrainEditTarget &target, const std::vector<TerrainEditorIssue> &issues);
        void DrawSection(EditorShell &shell, const TerrainEditTarget &target);

        void DrawSculpt(EditorShell &shell, const TerrainEditTarget &target);
        void DrawPaint(EditorShell &shell, const TerrainEditTarget &target);
        void DrawBiomes(EditorShell &shell, const TerrainEditTarget &target);
        void DrawGrass(EditorShell &shell, const TerrainEditTarget &target);
        void DrawHeights(EditorShell &shell, const TerrainEditTarget &target);
        void DrawSetup(EditorShell &shell, const TerrainEditTarget &target);

        TerrainEditorSection m_section = TerrainEditorSection::Sculpt;
        TerrainCreateOptions m_terrainOptions;
        TerrainWorldCreateOptions m_worldOptions;
        float m_worldGroundHeight = 20.0f;
    };
}
