#include "PlutoGE/ui/panels/TerrainEditorPanel.h"

#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/DetailGrassComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/ProceduralTerrainComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/TerrainVolumeComponent.h"
#include "PlutoGE/scene/components/TerrainWorldComponent.h"
#include "PlutoGE/ui/DetailGrassInspector.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/TerrainEditorSections.h"
#include "PlutoGE/ui/TerrainHeightEditing.h"
#include "PlutoGE/ui/TerrainLayerEditing.h"
#include "PlutoGE/ui/TerrainViewportTool.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>

namespace PlutoGE::ui
{
    namespace
    {
        using Section = TerrainEditorSection;

        TerrainEditTarget SelectedTarget(EditorShell &shell)
        {
            const auto selected = shell.GetSelectedEntities();
            return TerrainEditTarget::Resolve(selected.size() == 1 ? selected.front() : nullptr);
        }

        bool EditName(const char *id, std::string &value)
        {
            std::array<char, 128> buffer{};
            value.copy(buffer.data(), buffer.size() - 1);
            if (!ImGui::InputText(id, buffer.data(), buffer.size()))
                return false;
            value = buffer.data();
            return true;
        }

        void Muted(const char *text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        }

        const char *SectionIcon(Section section)
        {
            switch (section)
            {
            case Section::Sculpt: return icons::EntityTerrain;
            case Section::Paint: return icons::Paint;
            case Section::Biomes: return icons::Swatches;
            case Section::Grass: return icons::EntityFoliage;
            case Section::Heights: return icons::Import;
            case Section::World: return icons::Grid;
            case Section::Setup: return icons::Settings;
            }
            return icons::Settings;
        }

        bool Applies(Section section, const TerrainEditTarget &target)
        {
            return section != Section::World || target.kind == TerrainEditTarget::Kind::World;
        }

        // The sections that need the settings entity (a world's tile template) say so when there is none.
        bool NeedsSettings(const TerrainEditTarget &target)
        {
            if (target.settings)
                return true;
            widgets::SectionHeading("No Tile Template", "Tiles copy their layers, plants and grass from the world's Tile Template.");
            Muted(target.world && !target.world->GetTilePrefab().empty() ? "This world's tiles come from a prefab: edit the prefab."
                                                                         : "This world has none, so its tiles are plain terrains.");
            if (target.world && target.world->GetTilePrefab().empty() && widgets::PrimaryButton("Add Tile Template"))
                EditorShell::GetInstance().ExecuteSceneEdit("Add Tile Template", [&] { target.world->EnsureTileTemplate(); });
            return false;
        }

        // Turns the terrain tool to `mode` for the target (Off when it already is).
        void ToggleTool(EditorShell &shell, const TerrainEditTarget &target, TerrainToolMode mode)
        {
            auto &tool = shell.GetTerrainTool();
            const bool active = tool.IsActiveFor(target.RootId()) && tool.GetMode() == mode;
            tool.SetMode(active ? TerrainToolMode::Off : mode, target.RootId());
        }

        bool ToolActive(EditorShell &shell, const TerrainEditTarget &target, TerrainToolMode mode)
        {
            const auto &tool = shell.GetTerrainTool();
            return tool.IsActiveFor(target.RootId()) && tool.GetMode() == mode;
        }

        // A button that starts (or stops) a tool, looking primary while it is off.
        void ToolButton(EditorShell &shell, const TerrainEditTarget &target, TerrainToolMode mode, const char *start, const char *stop)
        {
            if (ToolActive(shell, target, mode))
            {
                if (ImGui::Button(stop))
                    ToggleTool(shell, target, mode);
            }
            else if (widgets::PrimaryButton(start))
                ToggleTool(shell, target, mode);
        }

        // Toggle rows for the target's parts (components on its settings entity).
        void DrawParts(EditorShell &shell, scene::Entity &settings, std::initializer_list<std::pair<TerrainAddOn, const char *>> parts)
        {
            if (!widgets::BeginSettingsTable("##Parts"))
                return;
            for (const auto &[addOn, description] : parts)
            {
                widgets::SettingsRow(TerrainAddOnName(addOn), description);
                bool enabled = HasTerrainAddOn(settings, addOn);
                ImGui::PushID(static_cast<int>(addOn));
                if (widgets::ToggleSwitch("##Part", &enabled))
                {
                    auto *entity = &settings;
                    shell.ExecuteSceneEdit(std::string(enabled ? "Add Terrain " : "Remove Terrain ") + TerrainAddOnName(addOn),
                                           [entity, addOn, enabled] { SetTerrainAddOn(*entity, addOn, enabled); });
                }
                ImGui::PopID();
            }
            widgets::EndSettingsTable();
        }

        // Shadows for the plants biomes grow (and any painted ones): the Foliage component's default, and each foliage
        // type's own choice. Types linked to a .plutofoliage asset take theirs from the file, so they open in the
        // Foliage Editor; types defined in the scene are edited here.
        void DrawPlantShadows(EditorShell &shell, scene::Entity &settings, scene::FoliageComponent &foliage)
        {
            const auto changed = [&](const std::string &property)
            {
                settings.AddPrefabOverride("Component:FoliageComponent:" + property);
                shell.MarkSceneDirty();
            };
            ImGui::Spacing();
            widgets::SectionHeading("Plant Shadows", "Trees and bushes usually cast shadows; grass and small ground cover rarely "
                                                     "need to, and skipping them saves shadow rendering.");
            if (widgets::BeginSettingsTable("##PlantShadows"))
            {
                widgets::SettingsRow("Cast Shadows", "For plants whose foliage type follows the Foliage component's setting.");
                bool castShadows = foliage.GetCastShadows();
                if (widgets::ToggleSwitch("##CastShadows", &castShadows))
                {
                    foliage.SetCastShadows(castShadows);
                    changed("CastShadows");
                }
                widgets::SettingsRow("Shadow Distance", "How far from the camera plants still cast shadows, unless their type sets "
                                                        "its own. 0 is no limit.");
                const float stored = foliage.GetMaxShadowDistance();
                float distance = stored >= FLT_MAX ? 0.0f : stored;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##ShadowDistance", &distance, 1.0f, 0.0f, 100000.0f, distance > 0.0f ? "%.0f m" : "No Limit"))
                {
                    foliage.SetMaxShadowDistance(distance);
                    changed("MaxShadowDistance");
                }
                widgets::EndSettingsTable();
            }
            if (foliage.GetTypeCount() == 0)
                return;

            constexpr const char *ModeNames[] = {"Foliage Component's Setting", "Cast Shadows", "No Shadows"};
            if (!ImGui::BeginTable("##PlantShadowTypes", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX))
                return;
            ImGui::TableSetupColumn("Plant", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableSetupColumn("Shadows", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            for (std::size_t index = 0; index < foliage.GetTypeCount(); ++index)
            {
                const auto *type = foliage.GetType(index);
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(type->name.c_str());
                ImGui::TableNextColumn();
                const bool casts = type->asset.shadows == scene::FoliageShadowMode::Inherit ? foliage.GetCastShadows()
                                                                                             : type->asset.shadows == scene::FoliageShadowMode::On;
                if (!type->asset.assetReference.empty())
                {
                    const auto reference = type->asset.assetReference;
                    if (widgets::IconButton("##Edit", icons::OpenExternal, "Open this foliage type in the Foliage Editor: Rendering > Shadows."))
                        shell.OpenFoliageTypeAsset(reference);
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(GetEditorColor(casts ? EditorColor::Text : EditorColor::TextMuted), "%s%s",
                                       casts ? "Casts shadows" : "No shadows",
                                       type->asset.shadows == scene::FoliageShadowMode::Inherit ? " (component)" : " (foliage type)");
                }
                else
                {
                    int mode = static_cast<int>(type->asset.shadows);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::Combo("##Shadows", &mode, ModeNames, IM_ARRAYSIZE(ModeNames)))
                    {
                        auto asset = type->asset;
                        asset.shadows = static_cast<scene::FoliageShadowMode>(mode);
                        foliage.ApplyTypeAsset(index, asset);
                        changed("Type." + std::to_string(index) + ".Shadows");
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    void TerrainEditorPanel::Show(TerrainEditorSection section)
    {
        m_section = section;
        Focus();
    }

    void TerrainEditorPanel::Render()
    {
        auto &shell = EditorShell::GetInstance();
        if (!shell.GetScene())
        {
            widgets::PageTitle("Terrain", "Shape, paint and plant the ground of a level or a whole open world.");
            Muted("Open a scene to work on terrain.");
            return;
        }
        const auto target = SelectedTarget(shell);
        if (!target)
        {
            DrawEmptyState(shell);
            return;
        }
        if (!Applies(m_section, target))
            m_section = Section::Sculpt;

        const auto issues = AnalyzeTerrain(target);
        DrawToolbar(shell, target);
        DrawProblems(issues);
        ImGui::Spacing();

        const bool runtime = shell.GetEngine().IsRuntimeRunning();
        if (runtime)
            Muted("Stop the game to edit terrain.");
        ImGui::BeginDisabled(runtime);
        ImGui::BeginChild("##TerrainSections", ImVec2(ImGui::GetFontSize() * 12.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        DrawSectionList(target, issues);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##TerrainDetails", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        DrawSection(shell, target);
        ImGui::EndChild();
        ImGui::EndDisabled();
    }

    void TerrainEditorPanel::DrawEmptyState(EditorShell &shell)
    {
        widgets::PageTitle("Terrain", "Shape, paint and plant the ground of a level or a whole open world.");
        Muted("Select a terrain or a terrain world in the Hierarchy to edit it here, or create one. A terrain is one "
              "heightfield, good up to about two kilometres. A terrain world streams kilometres of tiles around the "
              "camera, every tile copying its layers, plants and grass from the world's Tile Template.");
        ImGui::Spacing();
        auto *scene = shell.GetScene();

        widgets::BeginCard("##NewTerrain");
        widgets::SectionHeading("New Terrain", "A level, an island, a valley.");
        if (widgets::BeginSettingsTable("##TerrainOptions"))
        {
            widgets::SettingsRow("Name");
            EditName("##Name", m_terrainOptions.name);
            widgets::SettingsRow("Size", "Metres a side.");
            ImGui::DragFloat("##Size", &m_terrainOptions.size, 4.0f, 16.0f, 4096.0f, "%.0f m");
            const int samples = TerrainSamplesFor(m_terrainOptions);
            const std::string detail = std::to_string(samples) + " x " + std::to_string(samples) + " samples.";
            widgets::SettingsRow("Cell Size", detail.c_str());
            ImGui::DragFloat("##Cell", &m_terrainOptions.cellSize, 0.05f, 0.25f, 16.0f, "%.2f m");
            widgets::SettingsRow("Height Scale", "The highest the ground can rise.");
            ImGui::DragFloat("##Height", &m_terrainOptions.heightScale, 1.0f, 1.0f, 5000.0f, "%.0f m");
            widgets::SettingsRow("Collision");
            widgets::ToggleSwitch("##Collision", &m_terrainOptions.collision);
            widgets::SettingsRow("Foliage", "Plants, painted or grown by biomes.");
            widgets::ToggleSwitch("##Foliage", &m_terrainOptions.foliage);
            widgets::SettingsRow("Detail Grass", "Dense grass grown around the camera.");
            widgets::ToggleSwitch("##Grass", &m_terrainOptions.grass);
            widgets::SettingsRow("Biomes", "Grass and rock layers painted by slope, ready for plants.");
            widgets::ToggleSwitch("##Biomes", &m_terrainOptions.biomes);
            widgets::EndSettingsTable();
            if (samples >= MaxSingleTerrainSamples)
                ImGui::TextColored(GetEditorColor(EditorColor::Warning), "%s  That is as large as one terrain goes: make a terrain world.",
                                   icons::Warning);
        }
        if (widgets::PrimaryButton("Create Terrain"))
        {
            scene::Entity *created = nullptr;
            shell.ExecuteSceneEdit("Create Terrain", [&] { created = CreateTerrainEntity(*scene, nullptr, m_terrainOptions); });
            if (created)
            {
                shell.SetSelectedEntity(created);
                shell.GetTerrainTool().SetMode(TerrainToolMode::Sculpt, created->GetID());
                m_section = Section::Sculpt;
            }
        }
        widgets::EndCard();
        ImGui::Spacing();

        widgets::BeginCard("##NewWorld");
        widgets::SectionHeading("New Terrain World", "Kilometres of streamed tiles: an open world.");
        if (widgets::BeginSettingsTable("##WorldOptions"))
        {
            widgets::SettingsRow("Name");
            EditName("##WorldName", m_worldOptions.name);
            widgets::SettingsRow("Tiles", "A side.");
            ImGui::SliderInt("##Tiles", &m_worldOptions.tiles, 1, 32);
            widgets::SettingsRow("Tile Samples", "Detail of each tile.");
            constexpr std::array sampleOptions{129, 257, 513};
            if (ImGui::BeginCombo("##TileSamples", std::to_string(m_worldOptions.tileSamples).c_str()))
            {
                for (const int option : sampleOptions)
                    if (ImGui::Selectable(std::to_string(option).c_str(), option == m_worldOptions.tileSamples))
                        m_worldOptions.tileSamples = option;
                ImGui::EndCombo();
            }
            const float side = static_cast<float>(m_worldOptions.tiles) * static_cast<float>(m_worldOptions.tileSamples - 1) * m_worldOptions.cellSize;
            char size[64];
            std::snprintf(size, sizeof(size), "The world is %.1f km a side.", side / 1000.0f);
            widgets::SettingsRow("Cell Size", size);
            ImGui::DragFloat("##WorldCell", &m_worldOptions.cellSize, 0.05f, 0.25f, 16.0f, "%.2f m");
            widgets::SettingsRow("Height Scale", "The highest the ground can rise.");
            ImGui::DragFloat("##WorldHeight", &m_worldOptions.heightScale, 1.0f, 1.0f, 10000.0f, "%.0f m");
            widgets::SettingsRow("Ground Height", "It starts flat at this height; build it from a heightmap later.");
            ImGui::DragFloat("##Ground", &m_worldGroundHeight, 0.5f, 0.0f, m_worldOptions.heightScale, "%.0f m");
            widgets::SettingsRow("Foliage");
            widgets::ToggleSwitch("##WorldFoliage", &m_worldOptions.foliage);
            widgets::SettingsRow("Detail Grass");
            widgets::ToggleSwitch("##WorldGrass", &m_worldOptions.grass);
            widgets::SettingsRow("Biomes");
            widgets::ToggleSwitch("##WorldBiomes", &m_worldOptions.biomes);
            widgets::EndSettingsTable();
        }
        ImGui::BeginDisabled(!shell.GetProject());
        if (widgets::PrimaryButton("Create Terrain World"))
        {
            scene::Entity *created = nullptr;
            shell.ExecuteSceneEdit("Create Terrain World", [&] { created = CreateTerrainWorldEntity(*scene, nullptr, m_worldOptions); });
            if (created)
            {
                if (auto *world = created->GetComponent<scene::TerrainWorldComponent>())
                    WriteFlatTerrainWorld(shell, *created, *world, m_worldGroundHeight);
                shell.SetSelectedEntity(created);
                m_section = Section::Sculpt;
            }
        }
        ImGui::EndDisabled();
        if (!shell.GetProject())
            Muted("A terrain world keeps its tiles in the project: open one first.");
        widgets::EndCard();
    }

    void TerrainEditorPanel::DrawToolbar(EditorShell &shell, const TerrainEditTarget &target)
    {
        const bool world = target.kind == TerrainEditTarget::Kind::World;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(world ? icons::Grid : icons::EntityTerrain);
        ImGui::SameLine();
        ImGui::TextUnformatted(target.Name().c_str());
        ImGui::SameLine();
        widgets::Pill(world ? "Terrain World" : "Terrain", GetEditorColor(EditorColor::Accent));
        ImGui::SameLine();
        char detail[96] = "";
        if (world)
        {
            const auto extent = target.world->GetGrid().Extent();
            std::snprintf(detail, sizeof(detail), "%.0f x %.0f m, %zu tiles loaded", extent.x, extent.y, target.world->GetActiveTileCount());
        }
        else if (const auto *terrain = target.Terrain())
            std::snprintf(detail, sizeof(detail), "%.0f x %.0f m", (terrain->GetWidth() - 1) * terrain->GetCellSize(),
                          (terrain->GetDepth() - 1) * terrain->GetCellSize());
        ImGui::TextDisabled("%s", detail);

        // The tool sits at the right edge: what the mouse does in the viewport.
        static constexpr std::array<const char *, 5> Tools{"Select", "Sculpt", "Paint", "Holes", "Dig"};
        static_assert(Tools.size() == static_cast<std::size_t>(TerrainToolMode::Dig) + 1, "One segment per tool mode, in order");
        const float width = ImGui::GetFontSize() * 21.0f;
        const float frame = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width - frame));
        auto &tool = shell.GetTerrainTool();
        int current = tool.IsActiveFor(target.RootId()) ? static_cast<int>(tool.GetMode()) : 0;
        if (widgets::SegmentedControl("##TerrainTool", &current, Tools, width))
        {
            const auto mode = static_cast<TerrainToolMode>(current);
            const auto *terrain = target.Terrain();
            if (mode == TerrainToolMode::Paint && (!terrain || terrain->GetLayers().Empty()))
                m_section = Section::Paint; // the Paint section explains adding layers
            else if (mode == TerrainToolMode::Dig && !target.HasVolume())
                m_section = Section::Sculpt; // the Dig block offers to add a terrain volume
            else
            {
                tool.SetMode(mode, target.RootId());
                if (mode == TerrainToolMode::Sculpt || mode == TerrainToolMode::Holes || mode == TerrainToolMode::Dig)
                    m_section = Section::Sculpt;
                else if (mode == TerrainToolMode::Paint)
                    m_section = Section::Paint;
            }
        }
        ImGui::SetItemTooltip("What dragging in the viewport does. Esc goes back to Select.");
        ImGui::SameLine();
        if (widgets::IconButton("##Frame", icons::Frame, "Frame the terrain in the viewport (F)"))
            shell.FrameSelectedEntity();
    }

    void TerrainEditorPanel::DrawProblems(const std::vector<TerrainEditorIssue> &issues)
    {
        if (issues.empty())
            return;
        const auto warnings = std::count_if(issues.begin(), issues.end(),
                                            [](const auto &issue) { return issue.severity == TerrainEditorIssue::Severity::Warning; });
        const auto notes = static_cast<long long>(issues.size()) - warnings;
        std::string heading = warnings > 0 ? std::to_string(warnings) + (warnings == 1 ? " problem" : " problems") : std::string{};
        if (notes > 0)
            heading += (heading.empty() ? "" : ", ") + std::to_string(notes) + (notes == 1 ? " note" : " notes");
        ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(warnings > 0 ? EditorColor::Warning : EditorColor::TextMuted));
        const bool open = ImGui::TreeNode("##TerrainProblems", "%s  %s", warnings > 0 ? icons::Warning : icons::Info, heading.c_str());
        ImGui::PopStyleColor();
        if (!open)
            return;
        for (std::size_t index = 0; index < issues.size(); ++index)
        {
            const auto &issue = issues[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(issue.severity == TerrainEditorIssue::Severity::Warning ? EditorColor::Text
                                                                                                                          : EditorColor::TextMuted));
            if (ImGui::Selectable(issue.message.c_str()))
                m_section = issue.section;
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("Show %s", TerrainEditorSectionName(issue.section));
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    void TerrainEditorPanel::DrawSectionList(const TerrainEditTarget &target, const std::vector<TerrainEditorIssue> &issues)
    {
        for (const auto section : {Section::Sculpt, Section::Paint, Section::Biomes, Section::Grass, Section::Heights, Section::World, Section::Setup})
        {
            if (!Applies(section, target))
                continue;
            const bool flagged = std::any_of(issues.begin(), issues.end(), [&](const auto &issue) {
                return issue.section == section && issue.severity == TerrainEditorIssue::Severity::Warning;
            });
            ImGui::PushID(static_cast<int>(section));
            if (widgets::NavItem(flagged ? icons::Warning : SectionIcon(section), TerrainEditorSectionName(section), m_section == section))
                m_section = section;
            ImGui::PopID();
        }
    }

    void TerrainEditorPanel::DrawSection(EditorShell &shell, const TerrainEditTarget &target)
    {
        switch (m_section)
        {
        case Section::Sculpt: DrawSculpt(shell, target); break;
        case Section::Paint: DrawPaint(shell, target); break;
        case Section::Biomes: DrawBiomes(shell, target); break;
        case Section::Grass: DrawGrass(shell, target); break;
        case Section::Heights: DrawHeights(shell, target); break;
        case Section::World:
            widgets::SectionHeading("World", "The grid of tiles, and how they stream around the camera.");
            DrawTerrainWorldSettings(shell, *target.root, *target.world);
            break;
        case Section::Setup: DrawSetup(shell, target); break;
        }
    }

    void TerrainEditorPanel::DrawSculpt(EditorShell &shell, const TerrainEditTarget &target)
    {
        const bool world = target.kind == TerrainEditTarget::Kind::World;
        auto &tool = shell.GetTerrainTool();
        widgets::SectionHeading("Sculpt", world ? "Raise, lower, smooth and flatten the loaded tiles. Strokes keep tile edges matched, "
                                                  "and each tile saves into its own height file."
                                                : "Raise, lower, smooth and flatten the ground.");
        ToolButton(shell, target, TerrainToolMode::Sculpt, "Start Sculpting", "Stop Sculpting");
        if (world && target.Surface().Empty())
            ImGui::TextColored(GetEditorColor(EditorColor::Warning), "%s  No tiles are loaded around the camera.", icons::Warning);
        if (widgets::BeginSettingsTable("##SculptBrush"))
        {
            widgets::SettingsRow("Brush", "Shift swaps Raise and Lower while dragging.");
            static constexpr std::array<const char *, 4> SculptModes{"Raise", "Lower", "Smooth", "Flatten"};
            static_assert(SculptModes.size() == scene::TerrainSculptModeNames.size());
            int mode = static_cast<int>(tool.sculpt.mode);
            if (widgets::SegmentedControl("##SculptMode", &mode, SculptModes))
                tool.sculpt.mode = static_cast<scene::TerrainSculptMode>(mode);
            widgets::SettingsRow("Radius", "[ and ] resize it in the viewport.");
            ImGui::DragFloat("##SculptRadius", &tool.sculpt.radius, 0.1f, 0.1f, 1024.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
            widgets::SettingsRow("Strength", "How fast the ground moves under the brush.");
            ImGui::DragFloat("##SculptStrength", &tool.sculpt.strength, 0.05f, 0.0f, 200.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            if (tool.sculpt.mode == scene::TerrainSculptMode::Flatten)
            {
                widgets::SettingsRow("Flatten Height", "The world height Flatten pulls towards. Ctrl+click the ground to pick it.");
                ImGui::DragFloat("##FlattenHeight", &tool.sculpt.flattenHeight, 0.1f, -10000.0f, 10000.0f, "%.1f m");
            }
            widgets::EndSettingsTable();
        }

        ImGui::Spacing();
        widgets::SectionHeading("Holes", world ? "Cut cave mouths and shafts. Shift fills cut cells back. Each tile keeps its holes in a "
                                                 "file beside its heights."
                                               : "Cut cave mouths and shafts. Shift fills cut cells back.");
        ToolButton(shell, target, TerrainToolMode::Holes, "Cut Holes", "Stop Cutting");
        if (widgets::BeginSettingsTable("##HoleBrush"))
        {
            widgets::SettingsRow("Brush");
            int cut = tool.holes.cut ? 0 : 1;
            static constexpr std::array<const char *, 2> HoleModes{"Cut", "Fill"};
            if (widgets::SegmentedControl("##HoleMode", &cut, HoleModes))
                tool.holes.cut = cut == 0;
            widgets::SettingsRow("Radius");
            ImGui::DragFloat("##HoleRadius", &tool.holes.radius, 0.05f, 0.1f, 1024.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
            widgets::EndSettingsTable();
        }

        ImGui::Spacing();
        widgets::SectionHeading("Dig", world ? "Dig pits, caves and overhangs in 3D, or build rock up. Shift swaps Dig and Fill. Each tile "
                                               "keeps its dug ground in a file beside its heights."
                                             : "Dig pits, caves and overhangs in 3D, or build rock up. Shift swaps Dig and Fill.");
        if (!target.HasVolume())
        {
            Muted("Digging needs a Terrain Volume: 3D ground over the heightfield, which also carves the biomes' caves and any "
                  "Volume Stamps placed in the scene.");
            if (target.settings && widgets::PrimaryButton("Add Terrain Volume"))
                shell.ExecuteSceneEdit("Add Terrain Volume", [&] { target.settings->CreateComponent<scene::TerrainVolumeComponent>(); });
            return;
        }
        ToolButton(shell, target, TerrainToolMode::Dig, "Start Digging", "Stop Digging");
        if (widgets::BeginSettingsTable("##DigBrush"))
        {
            widgets::SettingsRow("Brush", "Dig removes ground, Fill adds rock of the layer, Paint recolours rock without moving it.");
            static constexpr std::array<const char *, 3> DigModes{"Dig", "Fill", "Paint"};
            int mode = static_cast<int>(tool.dig.mode);
            if (widgets::SegmentedControl("##DigMode", &mode, DigModes))
                tool.dig.mode = static_cast<scene::volume::VolumeEditMode>(mode);
            widgets::SettingsRow("Radius", "[ and ] resize it in the viewport.");
            ImGui::DragFloat("##DigRadius", &tool.dig.radius, 0.05f, 0.25f, 64.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
            widgets::SettingsRow("Rate", "Dabs a second while the mouse is held: each digs a little deeper.");
            ImGui::DragFloat("##DigRate", &tool.dig.rate, 0.1f, 0.5f, 30.0f, "%.1f /s", ImGuiSliderFlags_AlwaysClamp);
            if (tool.dig.mode != scene::volume::VolumeEditMode::Subtract)
            {
                const auto *terrain = target.Terrain();
                const auto &layers = terrain ? terrain->GetLayers().GetLayers() : std::vector<scene::TerrainLayer>{};
                widgets::SettingsRow("Layer", "The terrain layer the rock shows.");
                const auto label = [&](int index) {
                    return index < static_cast<int>(layers.size()) && !layers[index].name.empty() ? layers[index].name
                                                                                                    : "Layer " + std::to_string(index + 1);
                };
                if (ImGui::BeginCombo("##DigLayer", label(tool.dig.layer).c_str()))
                {
                    for (int index = 0; index < std::max<int>(static_cast<int>(layers.size()), 1); ++index)
                        if (ImGui::Selectable((label(index) + "##" + std::to_string(index)).c_str(), index == tool.dig.layer))
                            tool.dig.layer = index;
                    ImGui::EndCombo();
                }
            }
            widgets::EndSettingsTable();
        }
    }

    void TerrainEditorPanel::DrawPaint(EditorShell &shell, const TerrainEditTarget &target)
    {
        widgets::SectionHeading("Paint", "Material layers blended over the ground. Grass and biomes read them too: grass grows where its "
                                         "layer is painted.");
        if (!NeedsSettings(target))
            return;
        auto *terrain = target.Terrain();
        if (!terrain)
            return;
        auto &tool = shell.GetTerrainTool();
        const auto &layers = terrain->GetLayers().GetLayers();
        if (layers.empty())
            Muted("Add a layer below to start painting.");
        else
        {
            ToolButton(shell, target, TerrainToolMode::Paint, "Start Painting", "Stop Painting");
            if (target.Procedural() && target.Procedural()->GetTexture())
                ImGui::TextColored(GetEditorColor(EditorColor::Warning), "%s  Biomes paint this terrain: generating replaces hand painting.",
                                   icons::Info);
            if (widgets::BeginSettingsTable("##PaintBrush"))
            {
                widgets::SettingsRow("Layer", "Or pick it with the circle beside a layer below.");
                const auto label = [&](std::size_t index) {
                    return index < layers.size() ? (layers[index].name.empty() ? "Layer " + std::to_string(index + 1) : layers[index].name)
                                                 : std::string("(none)");
                };
                if (ImGui::BeginCombo("##PaintLayer", label(tool.paint.layer).c_str()))
                {
                    for (std::size_t index = 0; index < layers.size(); ++index)
                        if (ImGui::Selectable((label(index) + "##" + std::to_string(index)).c_str(), index == tool.paint.layer))
                            tool.paint.layer = index;
                    ImGui::EndCombo();
                }
                widgets::SettingsRow("Radius", "[ and ] resize it in the viewport.");
                ImGui::DragFloat("##PaintRadius", &tool.paint.radius, 0.1f, 0.1f, 1024.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
                widgets::SettingsRow("Opacity", "How much each dab adds.");
                ImGui::SliderFloat("##PaintOpacity", &tool.paint.dab.strength, 0.01f, 1.0f, "%.2f");
                widgets::SettingsRow("Hardness", "How much of the brush paints at full strength before it fades.");
                ImGui::SliderFloat("##PaintHardness", &tool.paint.dab.hardness, 0.0f, 1.0f, "%.2f");
                widgets::EndSettingsTable();
            }
        }
        ImGui::Spacing();
        widgets::SectionHeading("Layers", target.kind == TerrainEditTarget::Kind::World
                                              ? "Shared by every tile; each tile keeps its own paint beside its height file."
                                              : nullptr);
        RenderTerrainLayerInspector(shell, *target.settings, *terrain);
    }

    void TerrainEditorPanel::DrawBiomes(EditorShell &shell, const TerrainEditTarget &target)
    {
        const bool world = target.kind == TerrainEditTarget::Kind::World;
        widgets::SectionHeading("Biomes", "Rules that paint the layers and grow the plants: by height, slope, noise, masks, or distance "
                                          "to rivers and roads.");
        if (!NeedsSettings(target))
            return;
        auto *procedural = target.Procedural();
        if (!procedural)
        {
            Muted("This terrain has no biomes. Adding them starts with grass on flat ground and rock on slopes, and a Foliage "
                  "component for plants: add scatter rules to a biome to grow them.");
            if (widgets::PrimaryButton("Add Biomes"))
                shell.ExecuteSceneEdit("Add Terrain Biomes", [&] { SetTerrainAddOn(*target.settings, TerrainAddOn::Biomes, true); });
            return;
        }
        if (world)
            Muted("Loaded tiles regenerate a moment after each change.");
        DrawProceduralTerrainEditor(shell, *target.settings, *procedural, !world);
        if (auto *foliage = target.Foliage())
            DrawPlantShadows(shell, *target.settings, *foliage);
    }

    void TerrainEditorPanel::DrawGrass(EditorShell &shell, const TerrainEditTarget &target)
    {
        widgets::SectionHeading("Detail Grass", "Dense grass the GPU grows around the camera. Each layer grows where a terrain layer is "
                                                "painted (or everywhere), within slope and height limits, with bare patches if you like.");
        if (!NeedsSettings(target))
            return;
        auto *grass = target.Grass();
        if (!grass)
        {
            Muted("This terrain has no detail grass.");
            if (widgets::PrimaryButton("Add Detail Grass"))
                shell.ExecuteSceneEdit("Add Terrain Detail Grass", [&] { SetTerrainAddOn(*target.settings, TerrainAddOn::Grass, true); });
            return;
        }
        if (target.kind == TerrainEditTarget::Kind::World)
            Muted("Every tile grows these layers.");
        DrawDetailGrassEditor(shell, *target.settings, *grass);
    }

    void TerrainEditorPanel::DrawHeights(EditorShell &shell, const TerrainEditTarget &target)
    {
        if (target.kind == TerrainEditTarget::Kind::World)
        {
            widgets::SectionHeading("Heights", "Build every tile from one heightmap for the whole world, or flat ground.");
            DrawTerrainWorldBuilder(shell, *target.root, *target.world);
            return;
        }
        widgets::SectionHeading("Heights", "Import a heightmap from a terrain tool, and where the heights are kept.");
        if (auto *terrain = target.Terrain())
            RenderTerrainHeightInspector(shell, *target.root, *terrain);
    }

    void TerrainEditorPanel::DrawSetup(EditorShell &shell, const TerrainEditTarget &target)
    {
        const bool world = target.kind == TerrainEditTarget::Kind::World;
        widgets::SectionHeading("Setup", world ? "What every tile is made of: these live on the world's Tile Template."
                                               : "The terrain's material and the parts it is made of.");
        if (!NeedsSettings(target))
            return;
        if (auto *terrain = target.Terrain())
        {
            DrawTerrainMaterialPicker(shell, *target.settings, *terrain);
            Muted("Drawn where there are no layers, and the base under them.");
        }
        ImGui::Spacing();
        widgets::SectionHeading("Parts");
        DrawParts(shell, *target.settings,
                  {{TerrainAddOn::Collision, "Characters and physics stand on the ground."},
                   {TerrainAddOn::Foliage, "Plants: painted with the foliage tool, or grown by biomes."},
                   {TerrainAddOn::Grass, "Dense grass grown around the camera (Grass section)."},
                   {TerrainAddOn::Biomes, "Rules that paint layers and grow plants (Biomes section)."}});
        if (world)
        {
            ImGui::Spacing();
            widgets::SectionHeading("Tile Template", "An inactive child entity every tile copies. Edit it here, or select it and use the "
                                                     "Inspector like any entity: loaded tiles follow within a moment.");
            if (ImGui::Button("Select Tile Template"))
                shell.SetSelectedEntity(target.settings);
        }
    }
}
