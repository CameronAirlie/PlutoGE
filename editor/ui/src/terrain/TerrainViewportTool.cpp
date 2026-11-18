#include "PlutoGE/ui/TerrainViewportTool.h"

#include "PlutoGE/render/Material.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/TerrainWorldComponent.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/TerrainHeightEditing.h"
#include "PlutoGE/ui/TerrainLayerEditing.h"

#include <algorithm>
#include <map>
#include <memory>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr ImU32 kSculptColor = IM_COL32(120, 220, 140, 230);
        constexpr ImU32 kLowerColor = IM_COL32(255, 170, 90, 230);
        constexpr ImU32 kPaintColor = IM_COL32(100, 180, 255, 230);
        constexpr ImU32 kHoleColor = IM_COL32(255, 110, 90, 230);
        constexpr float kMinRadius = 0.1f;
        constexpr float kMaxRadius = 1024.0f;

        TerrainEditTarget SelectedTarget(EditorShell &shell)
        {
            const auto selected = shell.GetSelectedEntities();
            return TerrainEditTarget::Resolve(selected.size() == 1 ? selected.front() : nullptr);
        }

        scene::TerrainWorldComponent *FindWorld(scene::EntityID id)
        {
            auto *scene = EditorShell::GetInstance().GetScene();
            auto *entity = scene ? scene->FindEntityByID(id) : nullptr;
            return entity ? entity->GetComponent<scene::TerrainWorldComponent>() : nullptr;
        }

        scene::TerrainComponent *FindTerrain(scene::EntityID id)
        {
            auto *scene = EditorShell::GetInstance().GetScene();
            auto *entity = scene ? scene->FindEntityByID(id) : nullptr;
            return entity ? entity->GetComponent<scene::TerrainComponent>() : nullptr;
        }

        // Writes a terrain's height file (a tile's own file, or the terrain's, created on first use).
        void SaveHeights(scene::TerrainComponent &terrain)
        {
            if (auto *owner = terrain.GetOwner())
                SaveTerrainHeights(EditorShell::GetInstance(), *owner, terrain);
        }

        void SaveWeights(scene::TerrainComponent &terrain)
        {
            if (auto *owner = terrain.GetOwner())
                SaveTerrainWeightMaps(EditorShell::GetInstance(), *owner, terrain);
        }

        bool ToolButton(const char *label, bool active, bool enabled, const char *tooltip)
        {
            ImGui::BeginDisabled(!enabled);
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            const bool clicked = ImGui::Button(label);
            if (active)
                ImGui::PopStyleColor();
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("%s", tooltip);
            return clicked;
        }
    }

    void DrawTerrainToolModeButtons(TerrainEditTool &tool, const TerrainEditTarget &target)
    {
        const auto root = target.RootId();
        const auto button = [&](const char *label, TerrainToolMode mode, bool enabled, const char *tooltip) {
            const bool active = tool.IsActiveFor(root) && tool.GetMode() == mode;
            if (ToolButton(label, active, enabled, tooltip))
                tool.SetMode(active ? TerrainToolMode::Off : mode, root);
        };
        button("Sculpt", TerrainToolMode::Sculpt, true, "Raise, lower, smooth and flatten the ground. Shift swaps raise and lower.\n"
                                                        "[ and ] change the radius, Esc leaves the tool.");
        ImGui::SameLine();
        const auto *terrain = target.Terrain();
        button("Paint", TerrainToolMode::Paint, terrain && !terrain->GetLayers().Empty(),
               terrain && !terrain->GetLayers().Empty() ? "Paint material layers onto the ground."
                                                        : "Add material layers first (Terrain Editor > Paint).");
        ImGui::SameLine();
        button("Holes", TerrainToolMode::Holes, true, "Cut cave mouths and shafts into the ground. Shift fills.");
    }

    void DrawTerrainBrushSettings(TerrainEditTool &tool, const TerrainEditTarget &target, bool compact)
    {
        const float width = compact ? 90.0f : 0.0f;
        const auto item = [&] {
            if (compact)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(width);
            }
        };
        switch (tool.GetMode())
        {
        case TerrainToolMode::Sculpt:
        {
            for (std::size_t index = 0; index < scene::TerrainSculptModeNames.size(); ++index)
            {
                const auto mode = static_cast<scene::TerrainSculptMode>(index);
                if (index > 0 || compact)
                    ImGui::SameLine();
                if (ImGui::RadioButton(std::string(scene::TerrainSculptModeNames[index]).c_str(), tool.sculpt.mode == mode))
                    tool.sculpt.mode = mode;
            }
            item();
            ImGui::DragFloat("Radius##TerrainSculpt", &tool.sculpt.radius, 0.1f, kMinRadius, kMaxRadius, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
            item();
            ImGui::DragFloat("Strength##TerrainSculpt", &tool.sculpt.strength, 0.05f, 0.0f, 200.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            if (tool.sculpt.mode == scene::TerrainSculptMode::Flatten)
            {
                item();
                ImGui::DragFloat("Height##TerrainSculpt", &tool.sculpt.flattenHeight, 0.1f, -10000.0f, 10000.0f, "%.1f m");
                ImGui::SetItemTooltip("The world height Flatten pulls the ground to. Ctrl+click the ground to pick it.");
            }
            break;
        }
        case TerrainToolMode::Paint:
        {
            const auto *terrain = target.Terrain();
            const auto &layers = terrain ? terrain->GetLayers().GetLayers() : std::vector<scene::TerrainLayer>{};
            const auto label = [&](std::size_t index) {
                return index < layers.size() ? (layers[index].name.empty() ? "Layer " + std::to_string(index + 1) : layers[index].name)
                                             : std::string("(no layer)");
            };
            item();
            ImGui::SetNextItemWidth(compact ? 140.0f : 0.0f);
            if (ImGui::BeginCombo("Layer##TerrainPaint", label(tool.paint.layer).c_str()))
            {
                for (std::size_t index = 0; index < layers.size(); ++index)
                    if (ImGui::Selectable((label(index) + "##" + std::to_string(index)).c_str(), index == tool.paint.layer))
                        tool.paint.layer = index;
                ImGui::EndCombo();
            }
            item();
            ImGui::DragFloat("Radius##TerrainPaint", &tool.paint.radius, 0.1f, kMinRadius, kMaxRadius, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
            item();
            ImGui::SliderFloat("Opacity##TerrainPaint", &tool.paint.dab.strength, 0.01f, 1.0f, "%.2f");
            item();
            ImGui::SliderFloat("Hardness##TerrainPaint", &tool.paint.dab.hardness, 0.0f, 1.0f, "%.2f");
            break;
        }
        case TerrainToolMode::Holes:
            ImGui::SameLine();
            if (ImGui::RadioButton("Cut", tool.holes.cut))
                tool.holes.cut = true;
            ImGui::SameLine();
            if (ImGui::RadioButton("Fill", !tool.holes.cut))
                tool.holes.cut = false;
            item();
            ImGui::DragFloat("Radius##TerrainHoles", &tool.holes.radius, 0.05f, kMinRadius, kMaxRadius, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
            break;
        case TerrainToolMode::Off:
            break;
        }
    }

    bool TerrainViewportTool::IsActive(EditorShell &shell)
    {
        const auto target = SelectedTarget(shell);
        return target && shell.GetTerrainTool().IsActiveFor(target.RootId());
    }

    void TerrainViewportTool::BeginFrame(EditorShell &shell, bool mouseDown)
    {
        auto &tool = shell.GetTerrainTool();
        const auto target = SelectedTarget(shell);
        tool.SyncTarget(target.RootId(), shell.GetSceneRevision(), !shell.GetEngine().IsRuntimeRunning());
        if (m_sceneRevision != shell.GetSceneRevision())
        {
            // Never finish a stroke against a scene that replaced the one it started in.
            m_touched.clear();
            m_active = false;
            m_blockedUntilRelease = mouseDown;
            m_sceneRevision = shell.GetSceneRevision();
        }
        if (m_active && (!mouseDown || !tool.IsActiveFor(m_strokeRoot) || tool.GetMode() != m_strokeMode))
            FinishStroke(shell);
        if (!mouseDown)
        {
            m_blockedUntilRelease = false;
            m_sampler.Reset();
        }
    }

    bool TerrainViewportTool::RenderToolBar(EditorShell &shell, const ImVec2 &viewportMin, const ImVec2 &viewportSize, ImGuiID platformViewport)
    {
        if (!IsActive(shell))
            return false;
        auto &tool = shell.GetTerrainTool();
        const auto target = SelectedTarget(shell);
        ImGui::SetNextWindowPos(ImVec2(viewportMin.x + viewportSize.x * 0.5f, viewportMin.y + viewportSize.y - 12.0f), ImGuiCond_Always,
                                ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.94f);
        ImGui::SetNextWindowViewport(platformViewport);
        ImGui::Begin("Terrain Tool##Viewport", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing);
        const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        ImGui::TextUnformatted(target.Name().c_str());
        ImGui::SameLine();
        DrawTerrainToolModeButtons(tool, target);
        DrawTerrainBrushSettings(tool, target, true);
        ImGui::SameLine();
        if (ImGui::SmallButton("Terrain Editor"))
            shell.OpenTerrainEditor();
        if (target.kind == TerrainEditTarget::Kind::World && target.Surface().Empty())
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "No tiles are loaded around the camera.");
        ImGui::End();
        return hovered;
    }

    TerrainViewportTool::Touched &TerrainViewportTool::Touch(scene::TerrainComponent &terrain, const TerrainEditTarget &target)
    {
        const auto found = std::find_if(m_touched.begin(), m_touched.end(), [&](const Touched &touched) { return touched.terrain == &terrain; });
        if (found != m_touched.end())
            return *found;
        Touched touched;
        touched.terrain = &terrain;
        if (target.kind == TerrainEditTarget::Kind::World && target.world)
        {
            touched.ref.world = target.RootId();
            touched.ref.tile = target.world->FindTile(terrain).value_or(scene::TerrainTileCoord{});
        }
        else
            touched.ref.entity = terrain.GetOwner()->GetID();
        // What this stroke can change, before it does.
        switch (m_strokeMode)
        {
        case TerrainToolMode::Sculpt:
            touched.width = terrain.GetWidth();
            touched.heightsBefore = terrain.GetHeightSamples();
            break;
        case TerrainToolMode::Paint: touched.weightsBefore = terrain.GetLayers().GetWeights(); break;
        case TerrainToolMode::Holes: touched.holesBefore = terrain.GetHoles(); break;
        case TerrainToolMode::Off: break;
        }
        m_touched.push_back(std::move(touched));
        return m_touched.back();
    }

    bool TerrainViewportTool::Update(EditorShell &shell, const TerrainViewportContext &context)
    {
        auto &tool = shell.GetTerrainTool();
        const auto target = SelectedTarget(shell);
        if (!target || !tool.IsActiveFor(target.RootId()))
            return false;
        const auto &io = ImGui::GetIO();
        if (context.hovered && !io.WantTextInput)
        {
            auto &radius = tool.ActiveRadius();
            const float step = std::max(0.25f, radius * 0.1f);
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket))
                radius = std::max(kMinRadius, radius - step);
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket))
                radius = std::min(kMaxRadius, radius + step);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                tool.SetMode(TerrainToolMode::Off, target.RootId());
                return true;
            }
        }

        auto surface = target.Surface();
        const auto hit = context.rayOrigin && context.rayDirection ? surface.Raycast(*context.rayOrigin, *context.rayDirection) : std::nullopt;
        if (!hit)
        {
            m_sampler.Reset();
            return true;
        }
        const auto mode = tool.GetMode();
        const bool invert = io.KeyShift;
        ImU32 color = kSculptColor;
        if (mode == TerrainToolMode::Paint)
            color = kPaintColor;
        else if (mode == TerrainToolMode::Holes)
            color = (tool.holes.cut != invert) ? kHoleColor : kSculptColor;
        else if ((tool.sculpt.mode == scene::TerrainSculptMode::Lower) != (invert && tool.sculpt.mode == scene::TerrainSculptMode::Raise))
            color = kLowerColor;
        const float radius = tool.ActiveRadius();
        context.drawCircle(hit->position, glm::vec3(radius, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, radius), color);

        const bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        // Ctrl+click picks the flatten height instead of sculpting.
        if (mode == TerrainToolMode::Sculpt && tool.sculpt.mode == scene::TerrainSculptMode::Flatten && io.KeyCtrl && context.hovered &&
            !context.controlsHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            tool.sculpt.flattenHeight = hit->position.y;
            m_blockedUntilRelease = true;
            return true;
        }
        const bool brushDown = mouseDown && !m_blockedUntilRelease && context.hovered && !context.controlsHovered && !io.KeyAlt;
        if (!brushDown)
        {
            m_sampler.Reset();
            return true;
        }

        if (!m_active)
        {
            shell.FlushPendingSceneEdits();
            m_active = true;
            m_strokeMode = mode;
            m_strokeRoot = target.RootId();
            m_touched.clear();
            m_sampler.Reset();
            // Heights of a lone terrain move into a height file on its first stroke, so they need no scene snapshot.
            m_sceneEditStroke = false;
            if (mode == TerrainToolMode::Sculpt && target.kind == TerrainEditTarget::Kind::Terrain)
                if (auto *terrain = target.Terrain(); terrain && terrain->GetHeightDataPath().empty())
                    m_sceneEditStroke = !StoreTerrainHeightsInFile(shell, *target.root, *terrain);
            // A lone terrain's holes are scene data, and so are heights without a project to keep a height file in:
            // the stroke is an ordinary scene edit. A world's tiles keep their holes in files, like their weights.
            if (mode == TerrainToolMode::Holes && target.kind == TerrainEditTarget::Kind::Terrain)
                m_sceneEditStroke = true;
            if (m_sceneEditStroke)
                shell.BeginSceneEdit(mode == TerrainToolMode::Holes ? "Paint Terrain Holes" : "Sculpt Terrain");
        }

        const auto record = [&](const std::vector<scene::TerrainEditChange> &changes) {
            for (const auto &change : changes)
            {
                auto &touched = Touch(*change.terrain, target);
                touched.heights.Include(change.heights);
                touched.weights.Include(change.weights);
            }
        };
        const auto touchNear = [&](const glm::vec3 &position, float reach) {
            for (auto *terrain : surface.TerrainsNear(position, reach))
                Touch(*terrain, target);
        };
        switch (mode)
        {
        case TerrainToolMode::Sculpt:
        {
            auto brush = tool.sculpt;
            if (invert && brush.mode == scene::TerrainSculptMode::Raise)
                brush.mode = scene::TerrainSculptMode::Lower;
            else if (invert && brush.mode == scene::TerrainSculptMode::Lower)
                brush.mode = scene::TerrainSculptMode::Raise;
            touchNear(hit->position, brush.radius);
            record(surface.Sculpt(hit->position, brush, std::max(context.deltaTime, 1.0f / 240.0f)));
            break;
        }
        case TerrainToolMode::Paint:
            // Dabs a quarter radius apart along the mouse path, whatever the frame rate.
            m_sampler.Sample({hit->position.x, hit->position.z}, std::max(0.05f, tool.paint.radius * 0.25f), [&](glm::vec2 point) {
                const glm::vec3 position(point.x, hit->position.y, point.y);
                touchNear(position, tool.paint.radius);
                record(surface.PaintLayer(position, tool.paint));
            });
            break;
        case TerrainToolMode::Holes:
        {
            auto brush = tool.holes;
            brush.cut = brush.cut != invert;
            touchNear(hit->position, brush.radius);
            if (!surface.PaintHoles(hit->position, brush).empty() && target.kind == TerrainEditTarget::Kind::Terrain)
            {
                for (auto *terrain : surface.TerrainsNear(hit->position, brush.radius))
                    if (auto *owner = terrain->GetOwner())
                        owner->AddPrefabOverride("Component:TerrainComponent:HoleMask");
                shell.MarkSceneDirty();
            }
            break;
        }
        case TerrainToolMode::Off:
            break;
        }
        return true;
    }

    void TerrainViewportTool::FinishStroke(EditorShell &shell)
    {
        m_active = false;
        m_sampler.Reset();
        auto touched = std::move(m_touched);
        m_touched.clear();
        if (m_sceneEditStroke)
        {
            m_sceneEditStroke = false;
            shell.MarkSceneDirty();
            shell.EndSceneEdit();
            return;
        }

        if (m_strokeMode == TerrainToolMode::Sculpt)
        {
            struct Edit
            {
                TerrainRef ref;
                scene::TerrainHeightRegion before, after;
            };
            auto edits = std::make_shared<std::vector<Edit>>();
            std::size_t bytes = 0;
            std::vector<scene::TerrainTileCoord> tiles;
            scene::EntityID world = 0;
            for (auto &entry : touched)
            {
                if (entry.heights.Empty() || !entry.terrain || entry.terrain->GetWidth() != entry.width)
                    continue;
                Edit edit{entry.ref, {entry.heights, {}}, entry.terrain->CaptureHeights(entry.heights)};
                for (int z = entry.heights.z0; z < entry.heights.z1; ++z)
                    for (int x = entry.heights.x0; x < entry.heights.x1; ++x)
                        edit.before.samples.push_back(entry.heightsBefore[static_cast<std::size_t>(z) * entry.width + x]);
                bytes += (edit.before.samples.size() + edit.after.samples.size()) * sizeof(float);
                if (entry.ref.world)
                {
                    world = entry.ref.world;
                    tiles.push_back(entry.ref.tile);
                }
                SaveHeights(*entry.terrain);
                edits->push_back(std::move(edit));
            }
            if (edits->empty())
                return;
            // Tiles' shared edges and borders follow the stroke, and the neighbours it changed are saved too.
            if (auto *terrainWorld = world ? FindWorld(world) : nullptr)
                for (auto *neighbour : terrainWorld->SyncTileEdges(tiles))
                    SaveHeights(*neighbour);

            const auto apply = [edits](bool undo) {
                std::map<scene::EntityID, std::vector<scene::TerrainTileCoord>> worlds;
                for (const auto &edit : *edits)
                {
                    const auto &region = undo ? edit.before : edit.after;
                    if (edit.ref.world)
                    {
                        auto *terrainWorld = FindWorld(edit.ref.world);
                        scene::TerrainComponent *loaded = nullptr;
                        if (!terrainWorld || !terrainWorld->PatchTileHeights(edit.ref.tile, region, &loaded))
                            return false;
                        if (loaded)
                            SaveHeights(*loaded);
                        worlds[edit.ref.world].push_back(edit.ref.tile);
                    }
                    else if (auto *terrain = FindTerrain(edit.ref.entity))
                    {
                        terrain->RestoreHeights(region);
                        SaveHeights(*terrain);
                    }
                    else
                        return false;
                }
                for (const auto &[id, tiles] : worlds)
                    if (auto *terrainWorld = FindWorld(id))
                        for (auto *neighbour : terrainWorld->SyncTileEdges(tiles))
                            SaveHeights(*neighbour);
                return true;
            };
            shell.PushSceneEditCommand(world ? "Sculpt Terrain World" : "Sculpt Terrain", [apply] { return apply(true); },
                                       [apply] { return apply(false); }, bytes);
            return;
        }

        if (m_strokeMode == TerrainToolMode::Paint)
        {
            struct Edit
            {
                TerrainRef ref;
                scene::TerrainWeightMap::Region before, after;
            };
            auto edits = std::make_shared<std::vector<Edit>>();
            std::size_t bytes = 0;
            for (auto &entry : touched)
            {
                if (entry.weights.Empty() || !entry.terrain || !entry.weightsBefore)
                    continue;
                Edit edit{entry.ref, entry.weightsBefore->Capture(entry.weights), entry.terrain->GetLayers().GetWeights().Capture(entry.weights)};
                bytes += edit.before.weights.size() + edit.after.weights.size();
                SaveWeights(*entry.terrain);
                edits->push_back(std::move(edit));
            }
            if (edits->empty())
                return;
            const auto apply = [edits](bool undo) {
                for (const auto &edit : *edits)
                {
                    scene::TerrainComponent *terrain = nullptr;
                    if (edit.ref.world)
                    {
                        // A tile that has since unloaded keeps what its weight file holds.
                        auto *terrainWorld = FindWorld(edit.ref.world);
                        terrain = terrainWorld ? terrainWorld->GetTileTerrain(edit.ref.tile) : nullptr;
                        if (!terrain)
                            continue;
                    }
                    else if (!(terrain = FindTerrain(edit.ref.entity)))
                        return false;
                    terrain->GetLayers().RestoreWeights(undo ? edit.before : edit.after);
                    SaveWeights(*terrain);
                }
                return true;
            };
            shell.PushSceneEditCommand("Paint Terrain Layer", [apply] { return apply(true); }, [apply] { return apply(false); }, bytes);
            return;
        }

        if (m_strokeMode == TerrainToolMode::Holes)
        {
            // Only world tiles get here (a lone terrain's hole strokes are scene edits): each touched tile's mask before
            // and after, saved to its hole file.
            struct Edit
            {
                TerrainRef ref;
                scene::TerrainHoleMask before, after;
            };
            auto edits = std::make_shared<std::vector<Edit>>();
            std::size_t bytes = 0;
            for (auto &entry : touched)
            {
                if (!entry.terrain || !entry.holesBefore || !entry.ref.world || entry.terrain->GetHoles() == *entry.holesBefore)
                    continue;
                auto *terrainWorld = FindWorld(entry.ref.world);
                if (!terrainWorld)
                    continue;
                if (!terrainWorld->SaveTileHoles(entry.ref.tile))
                    shell.Log(EditorShell::ConsoleSeverity::Warning, "Could not save a terrain tile's holes.");
                Edit edit{entry.ref, std::move(*entry.holesBefore), entry.terrain->GetHoles()};
                bytes += static_cast<std::size_t>(edit.before.GetCellsX()) * edit.before.GetCellsZ() * 2;
                edits->push_back(std::move(edit));
            }
            if (edits->empty())
                return;
            const auto apply = [edits](bool undo) {
                for (const auto &edit : *edits)
                {
                    auto *terrainWorld = FindWorld(edit.ref.world);
                    if (!terrainWorld || !terrainWorld->StoreTileHoles(edit.ref.tile, undo ? edit.before : edit.after))
                        return false;
                }
                return true;
            };
            shell.PushSceneEditCommand("Paint Terrain World Holes", [apply] { return apply(true); }, [apply] { return apply(false); }, bytes);
        }
    }
}
