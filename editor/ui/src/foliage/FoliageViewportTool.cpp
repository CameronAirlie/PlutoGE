#include "PlutoGE/ui/FoliageViewportTool.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/FoliageDiagnostics.h"
#include "PlutoGE/ui/FoliageEditing.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr ImU32 kPaintColor = IM_COL32(100, 180, 255, 230);
        constexpr ImU32 kEraseColor = IM_COL32(255, 110, 90, 230);
        constexpr ImU32 kSelectedColor = IM_COL32(70, 175, 255, 245);
        constexpr ImU32 kPrimaryColor = IM_COL32(255, 210, 70, 255);
        constexpr float kMinBrushRadius = 0.05f;
        constexpr float kMaxBrushRadius = 512.0f;

        void DrawInstanceBox(const FoliageViewportContext &context, const scene::FoliageInstanceBounds &bounds, ImU32 color)
        {
            std::array<glm::vec3, 8> corners;
            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 local((corner & 1) ? bounds.localMax.x : bounds.localMin.x,
                                      (corner & 2) ? bounds.localMax.y : bounds.localMin.y,
                                      (corner & 4) ? bounds.localMax.z : bounds.localMin.z);
                corners[static_cast<std::size_t>(corner)] = glm::vec3(bounds.model * glm::vec4(local, 1.0f));
            }
            static constexpr std::array<std::pair<int, int>, 12> edges{{{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
                                                                        {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
            for (const auto &[from, to] : edges)
                context.drawLine(corners[static_cast<std::size_t>(from)], corners[static_cast<std::size_t>(to)], color);
        }

        scene::FoliageInstanceWorldEdit EditBetween(const glm::mat4 &start, const glm::mat4 &current)
        {
            const auto axisLengths = [](const glm::mat4 &matrix)
            {
                return glm::vec3(glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])), glm::length(glm::vec3(matrix[2])));
            };
            const auto rotationOf = [](const glm::mat4 &matrix, const glm::vec3 &lengths)
            {
                return glm::mat3(glm::vec3(matrix[0]) / lengths.x, glm::vec3(matrix[1]) / lengths.y, glm::vec3(matrix[2]) / lengths.z);
            };
            const glm::vec3 startLengths = glm::max(axisLengths(start), glm::vec3(1e-6f));
            const glm::vec3 currentLengths = glm::max(axisLengths(current), glm::vec3(1e-6f));
            scene::FoliageInstanceWorldEdit edit;
            edit.pivot = glm::vec3(start[3]);
            edit.translation = glm::vec3(current[3]) - glm::vec3(start[3]);
            edit.scale = currentLengths / startLengths;
            const glm::mat3 rotation = rotationOf(current, currentLengths) * glm::transpose(rotationOf(start, startLengths));
            // Translating or scaling leaves the rotation at identity but for rounding; keep the instances' angles exact.
            bool rotated = false;
            for (int column = 0; column < 3; ++column)
                rotated = rotated || glm::length(rotation[column] - glm::mat3(1.0f)[column]) > 1e-5f;
            if (rotated) edit.rotation = rotation;
            return edit;
        }
    }

    void DrawFoliageToolModeButtons(FoliageEditTool &tool, scene::EntityID entity)
    {
        const auto button = [&](const char *label, FoliageToolMode mode, const char *tooltip)
        {
            const bool active = tool.IsActiveFor(entity) && tool.GetMode() == mode;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(label)) tool.SetMode(active ? FoliageToolMode::Off : mode, entity);
            if (active) ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%s", tooltip);
        };
        button("Paint", FoliageToolMode::Paint, "Paint the selected type onto the terrain. Hold Shift to erase, [ and ] change the radius.");
        ImGui::SameLine();
        button("Erase", FoliageToolMode::Erase, "Remove the selected type under the brush. Hold Shift to paint, [ and ] change the radius.");
        ImGui::SameLine();
        button("Select", FoliageToolMode::Select,
               "Click instances to select them. Ctrl toggles, Shift adds. W, E and R pick the gizmo, Delete removes, F frames.");
    }

    bool FoliageViewportTool::IsActiveFor(EditorShell &shell, const scene::Entity &entity)
    {
        const auto *foliage = entity.GetComponent<scene::FoliageComponent>();
        return foliage && foliage->IsEnabled() && shell.GetFoliageTool().IsActiveFor(entity.GetID());
    }

    void FoliageViewportTool::FinishStroke(EditorShell &shell, bool mouseDown)
    {
        scene::FoliageInstanceSnapshot after;
        auto *scene = shell.GetScene();
        auto *entity = scene ? scene->FindEntityByID(m_stroke.entity) : nullptr;
        auto *foliage = entity ? entity->GetComponent<scene::FoliageComponent>() : nullptr;
        if (foliage) after = foliage->CaptureInstanceSnapshot();
        if (foliage && m_stroke.before != after)
        {
            for (std::size_t typeIndex = 0; typeIndex < after.size(); ++typeIndex)
                if (typeIndex >= m_stroke.before.size() || m_stroke.before[typeIndex] != after[typeIndex])
                    RecordFoliageInstanceOverride(*entity, typeIndex);
            PushFoliageSnapshotCommand(shell, m_stroke.entity,
                                       m_stroke.mode == scene::FoliageBrushMode::Remove ? "Erase Foliage" : "Paint Foliage",
                                       std::move(m_stroke.before), std::move(after));
        }
        m_stroke.before.clear();
        m_stroke.entity = 0;
        m_stroke.active = false;
        m_stroke.sampler.Reset();
        m_stroke.blockedUntilRelease = mouseDown;
    }

    void FoliageViewportTool::BeginFrame(EditorShell &shell, bool mouseDown, bool hovered, bool controlsHovered)
    {
        auto &tool = shell.GetFoliageTool();
        const bool editable = !shell.GetEngine().IsRuntimeRunning();
        const auto selected = shell.GetSelectedEntities();
        auto *target = selected.size() == 1 ? selected.front() : nullptr;
        auto *foliage = target ? target->GetComponent<scene::FoliageComponent>() : nullptr;

        if (!mouseDown) m_stroke.blockedUntilRelease = false;

        // Scene replacement and play transitions invalidate authoring snapshots: never resolve a prior scene's entity
        // ID against its replacement.
        if (m_sceneRevision != shell.GetSceneRevision() || !editable)
        {
            const bool interrupted = m_stroke.active;
            const auto revision = shell.GetSceneRevision();
            m_stroke = Stroke{};
            m_stroke.blockedUntilRelease = interrupted && mouseDown;
            m_sceneRevision = revision;
            m_gizmoDragging = false;
        }
        tool.SyncTarget(target ? target->GetID() : 0, shell.GetSceneRevision(), editable);

        const bool targetChanged = !foliage || !foliage->IsEnabled() || target->GetID() != m_stroke.entity ||
                                   foliage->GetSelectedTypeIndex() != m_stroke.type ||
                                   tool.GetMode() == FoliageToolMode::Off || tool.GetMode() == FoliageToolMode::Select;
        if (m_stroke.active && (!mouseDown || targetChanged))
            FinishStroke(shell, mouseDown);
        if (!mouseDown || targetChanged || !hovered || controlsHovered || ImGui::GetIO().KeyAlt)
            m_stroke.sampler.Reset();
    }

    bool FoliageViewportTool::RenderToolBar(EditorShell &shell, const ImVec2 &viewportMin, const ImVec2 &viewportSize, ImGuiID platformViewport)
    {
        auto &tool = shell.GetFoliageTool();
        auto *entity = shell.GetSelectedEntity();
        if (!entity || !IsActiveFor(shell, *entity) || shell.GetSelectedEntities().size() != 1) return false;
        auto &foliage = *entity->GetComponent<scene::FoliageComponent>();

        ImGui::SetNextWindowPos(ImVec2(viewportMin.x + viewportSize.x * 0.5f, viewportMin.y + viewportSize.y - 12.0f),
                                ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.94f);
        ImGui::SetNextWindowViewport(platformViewport);
        ImGui::Begin("Foliage Tool##Viewport", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoFocusOnAppearing);
        const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        ImGui::TextUnformatted(icons::EntityFoliage);
        ImGui::SameLine();
        DrawFoliageToolModeButtons(tool, entity->GetID());
        ImGui::SameLine();

        if (const auto *selected = foliage.GetSelectedType())
        {
            ImGui::SetNextItemWidth(170.0f);
            if (ImGui::BeginCombo("##FoliageToolType", selected->name.c_str()))
            {
                for (std::size_t index = 0; index < foliage.GetTypeCount(); ++index)
                {
                    const auto *type = foliage.GetType(index);
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::Selectable(type->name.c_str(), static_cast<int>(index) == foliage.GetSelectedTypeIndex()))
                        foliage.SetSelectedTypeIndex(static_cast<int>(index));
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("The foliage type the brush paints and erases.");
        }

        if (tool.GetMode() == FoliageToolMode::Select)
        {
            ImGui::SameLine();
            tool.selection.Resolve(foliage);
            if (tool.selection.Empty())
                ImGui::TextDisabled("Click an instance. Esc leaves the tool.");
            else
            {
                ImGui::Text("%zu selected", tool.selection.Size());
                ImGui::SameLine();
                if (ImGui::SmallButton("Frame")) shell.FrameSelectedEntity();
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete")) DeleteSelectedFoliageInstances(shell, tool, *entity, foliage);
            }
        }
        else
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::DragFloat("Radius##FoliageTool", &tool.brush.radius, 0.05f, kMinBrushRadius, kMaxBrushRadius, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            ImGui::DragInt("Density##FoliageTool", &tool.brush.density, 1.0f, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip("Instances attempted per dab. Dabs follow the stroke; holding still adds nothing.");

            const auto *terrain = entity->GetComponent<scene::TerrainComponent>();
            for (const auto &issue : DiagnoseFoliagePainting(terrain && terrain->IsEnabled(), foliage.GetSelectedType()))
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", issue.message.c_str());
        }
        ImGui::End();
        return hovered;
    }

    void FoliageViewportTool::UpdateBrush(EditorShell &shell, const FoliageViewportContext &context)
    {
        auto &tool = shell.GetFoliageTool();
        const auto &io = ImGui::GetIO();
        if (!io.WantTextInput && context.hovered)
        {
            const float step = (std::max)(0.25f, tool.brush.radius * 0.1f);
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) tool.brush.radius = (std::max)(kMinBrushRadius, tool.brush.radius - step);
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) tool.brush.radius = (std::min)(kMaxBrushRadius, tool.brush.radius + step);
        }

        auto *terrain = context.terrain;
        if (!terrain || !terrain->IsEnabled() || !context.rayOrigin || !context.rayDirection) return;
        glm::vec3 hitPoint(0.0f);
        if (!terrain->Raycast(*context.rayOrigin, *context.rayDirection, hitPoint))
        {
            m_stroke.sampler.Reset();
            return;
        }

        const auto brush = tool.BrushFor(io.KeyShift);
        const glm::mat4 worldTransform = context.entity.GetWorldTransform();
        // Radii are applied in terrain-local units: keep the owner's scale so the preview covers the same footprint.
        context.drawCircle(hitPoint, glm::vec3(worldTransform[0]) * brush.radius, glm::vec3(worldTransform[2]) * brush.radius,
                           brush.mode == scene::FoliageBrushMode::Remove ? kEraseColor : kPaintColor);

        const bool canApply = context.foliage.GetSelectedType() && context.foliage.GetSelectedType()->mesh;
        const bool brushDown = canApply && !m_stroke.blockedUntilRelease && context.hovered && !context.controlsHovered &&
                               !io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (!brushDown)
        {
            m_stroke.sampler.Reset();
            return;
        }

        if (!m_stroke.active)
        {
            shell.FlushPendingSceneEdits();
            m_stroke.entity = context.entity.GetID();
            m_stroke.before = context.foliage.CaptureInstanceSnapshot();
            m_stroke.type = context.foliage.GetSelectedTypeIndex();
            m_stroke.mode = brush.mode;
            m_stroke.sampler.Reset();
            m_stroke.active = true;
        }

        const glm::vec3 localHit = glm::vec3(glm::inverse(worldTransform) * glm::vec4(hitPoint, 1.0f));
        const auto sampleSurface = [terrain](float x, float z) -> std::optional<scene::FoliageSurfaceSample>
        {
            scene::FoliageSurfaceSample surface;
            if (!terrain->TrySampleSurface(x, z, surface.height, surface.normal)) return std::nullopt;
            return surface;
        };
        m_stroke.sampler.Sample({localHit.x, localHit.z}, (std::max)(0.05f, brush.radius * 0.25f), [&](glm::vec2 dab)
        {
            float height = 0.0f;
            glm::vec3 normal(0.0f);
            if (!terrain->TrySampleSurface(dab.x, dab.y, height, normal)) return;
            const glm::vec3 worldDab = glm::vec3(worldTransform * glm::vec4(dab.x, height, dab.y, 1.0f));
            if (context.foliage.ApplyBrush(brush, worldDab, sampleSurface))
                shell.MarkSceneDirty();
        });
    }

    bool FoliageViewportTool::UpdateSelection(EditorShell &shell, const FoliageViewportContext &context)
    {
        auto &tool = shell.GetFoliageTool();
        auto &foliage = context.foliage;
        const auto &io = ImGui::GetIO();
        const auto locations = tool.selection.Resolve(foliage);

        for (std::size_t index = 0; index < locations.size(); ++index)
            if (const auto bounds = foliage.GetInstanceBounds(locations[index]))
                DrawInstanceBox(context, *bounds, index + 1 == locations.size() ? kPrimaryColor : kSelectedColor);

        bool gizmoUsing = false;
        if (!locations.empty())
        {
            if (!m_gizmoDragging)
            {
                // One instance turns on its own axes. A group turns on the world's, about its middle.
                if (locations.size() == 1)
                {
                    m_gizmoMatrix = foliage.GetInstancePivotTransform(locations.front()).value_or(glm::mat4(1.0f));
                }
                else
                {
                    glm::vec3 center(0.0f);
                    for (const auto &location : locations)
                        center += glm::vec3(foliage.GetInstancePivotTransform(location).value_or(glm::mat4(1.0f))[3]);
                    m_gizmoMatrix = glm::mat4(1.0f);
                    m_gizmoMatrix[3] = glm::vec4(center / static_cast<float>(locations.size()), 1.0f);
                }
            }
            ImGuizmo::Manipulate(glm::value_ptr(context.camera.view), glm::value_ptr(context.gizmoProjection),
                                 context.gizmoOperation, context.gizmoMode, glm::value_ptr(m_gizmoMatrix), nullptr, context.gizmoSnap);
            gizmoUsing = ImGuizmo::IsUsing();

            if (gizmoUsing && !m_gizmoDragging)
            {
                m_gizmoDragging = true;
                BeginFoliageTransform(shell, tool, foliage);
                // Manipulate already applied this frame's movement; the matrix it started from is rebuilt from the instance.
                m_gizmoStart = glm::mat4(1.0f);
                if (locations.size() == 1)
                {
                    m_gizmoStart = foliage.GetInstancePivotTransform(locations.front()).value_or(glm::mat4(1.0f));
                }
                else
                {
                    glm::vec3 center(0.0f);
                    for (const auto &location : locations)
                        center += glm::vec3(foliage.GetInstancePivotTransform(location).value_or(glm::mat4(1.0f))[3]);
                    m_gizmoStart[3] = glm::vec4(center / static_cast<float>(locations.size()), 1.0f);
                }
            }
            if (gizmoUsing)
            {
                const auto edit = EditBetween(m_gizmoStart, m_gizmoMatrix);
                const glm::mat4 owner = context.entity.GetWorldTransform();
                std::vector<scene::FoliageInstance> moved;
                moved.reserve(tool.transformBefore.size());
                for (const auto &instance : tool.transformBefore)
                    moved.push_back(scene::ApplyFoliageInstanceWorldEdit(owner, instance, edit));
                UpdateFoliageTransform(tool, foliage, moved);
            }
            else if (m_gizmoDragging)
            {
                m_gizmoDragging = false;
                CommitFoliageTransform(shell, tool, context.entity, foliage, "Transform Foliage Instances");
            }
        }
        else if (m_gizmoDragging)
        {
            // The selection was cleared mid-drag. Esc keeps what the drag did, so it still needs its undo step; after
            // an undo the instances are back where they started and this records nothing.
            m_gizmoDragging = false;
            CommitFoliageTransform(shell, tool, context.entity, foliage, "Transform Foliage Instances");
        }

        const bool overGizmo = !locations.empty() && (ImGuizmo::IsOver() || gizmoUsing);
        if (context.clicked && context.hovered && !context.controlsHovered && !overGizmo &&
            context.rayOrigin && context.rayDirection)
        {
            const bool toggle = io.KeyCtrl || io.KeySuper;
            if (const auto hit = foliage.PickInstance(*context.rayOrigin, *context.rayDirection))
            {
                if (toggle) tool.selection.Toggle(hit->instanceId);
                else if (io.KeyShift) tool.selection.Add(hit->instanceId);
                else tool.selection.Set(hit->instanceId);
                foliage.SetSelectedTypeIndex(static_cast<int>(hit->location.typeIndex));
            }
            else if (!toggle && !io.KeyShift)
            {
                tool.selection.Clear();
            }
        }
        return gizmoUsing;
    }

    bool FoliageViewportTool::Update(EditorShell &shell, const FoliageViewportContext &context)
    {
        return shell.GetFoliageTool().GetMode() == FoliageToolMode::Select
                   ? UpdateSelection(shell, context)
                   : (UpdateBrush(shell, context), false);
    }
}
