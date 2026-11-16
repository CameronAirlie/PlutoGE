// Water tools: the River inspector (water ribbon, conforming to the terrain, carving the bed), the lake tools of the
// Ocean inspector (the lake preset, fitting the area to a terrain basin) and the Water Effects inspector's checks and
// play-mode test button. The effect settings themselves are ordinary properties drawn by the generic editor.
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/WaterBodies.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/RiverComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/WaterEffectsComponent.h"
#include "PlutoGE/scene/water/WaterResponders.h"
#include "PlutoGE/scene/water/WaterSystem.h"
#include "PlutoGE/ui/ComponentInspectors.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/TerrainHeightEditing.h"
#include "PlutoGE/ui/WaterEditing.h"

#include <imgui.h>

#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    namespace
    {
        void OverrideChanged(scene::Entity &entity, const char *type, const std::vector<scene::Property> &before,
                             const std::vector<scene::Property> &after)
        {
            for (const auto &property : after)
            {
                const auto previous = std::find_if(before.begin(), before.end(), [&](const auto &other) { return other.name == property.name; });
                if (previous == before.end() || previous->value != property.value)
                    entity.AddPrefabOverride(std::string("Component:") + type + ":" + property.name);
            }
        }

        // The terrain whose surface is under a world position.
        scene::TerrainComponent *TerrainUnder(const scene::Scene &scene, glm::vec3 world)
        {
            for (auto *terrain : scene.GetTerrainComponents())
            {
                float height = 0.0f;
                glm::vec3 normal{};
                if (terrain && terrain->IsEnabled() && terrain->GetOwner() && terrain->GetOwner()->IsActive() &&
                    terrain->TrySampleWorldSurface({world.x, 1.0e6f, world.z}, height, normal))
                    return terrain;
            }
            return nullptr;
        }

        // Carves the bed into every terrain the river crosses as one undo step (the heights may live in files).
        void CarveWithUndo(EditorShell &shell, scene::RiverComponent &river)
        {
            shell.FlushPendingSceneEdits();
            auto carved = river.CarveTerrain();
            if (carved.empty())
            {
                shell.Log(EditorShell::ConsoleSeverity::Warning, "The river crosses no terrain to carve.");
                return;
            }
            struct Edit
            {
                scene::EntityID terrain;
                scene::TerrainHeightRegion before, after;
            };
            auto edits = std::make_shared<std::vector<Edit>>();
            std::size_t bytes = 0;
            for (auto &result : carved)
            {
                auto *owner = result.terrain->GetOwner();
                bytes += (result.before.samples.size() + result.after.samples.size()) * sizeof(float);
                edits->push_back({owner->GetID(), std::move(result.before), std::move(result.after)});
                if (!result.terrain->GetHeightDataPath().empty() || shell.GetProject())
                    SaveTerrainHeights(shell, *owner, *result.terrain);
            }
            const auto apply = [edits](bool undo) {
                auto *scene = EditorShell::GetInstance().GetScene();
                if (!scene)
                    return false;
                for (const auto &edit : *edits)
                {
                    auto *owner = scene->FindEntityByID(edit.terrain);
                    auto *terrain = owner ? owner->GetComponent<scene::TerrainComponent>() : nullptr;
                    if (!terrain)
                        return false;
                    terrain->RestoreHeights(undo ? edit.before : edit.after);
                    SaveTerrainHeights(EditorShell::GetInstance(), *owner, *terrain);
                }
                return true;
            };
            shell.PushSceneEditCommand("Carve River Bed", [apply] { return apply(true); }, [apply] { return apply(false); }, bytes);
            shell.MarkSceneDirty();
        }

        void DrawRiverInspector(scene::Component &component, ComponentInspectorContext &context, std::vector<scene::Property> &)
        {
            auto &river = static_cast<scene::RiverComponent &>(component);
            auto &shell = context.shell;
            auto &entity = context.entity;
            auto *spline = river.GetSpline();
            if (!spline)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "A river flows along a Spline Track Component on its entity.");
                return;
            }
            ImGui::TextWrapped("Flows from the spline's first point to its last; the spline's width is the water's width. "
                               "Tag the entity (e.g. River) to use it in procedural Distance rules.");
            if (ImGui::Button("Make Water Ribbon"))
                shell.ExecuteSceneEdit("Make River Ribbon", [&] {
                    const auto before = spline->Serialize();
                    river.ConfigureSpline();
                    OverrideChanged(entity, "SplineComponent", before, spline->Serialize());
                });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Open, thin and without collision; drawn with the river's water look unless the spline has a material");
            ImGui::SameLine();
            if (ImGui::Button("Conform To Terrain"))
                shell.ExecuteSceneEdit("Conform River To Terrain", [&] {
                    const auto before = spline->Serialize();
                    if (!river.ConformToTerrain())
                        shell.Log(EditorShell::ConsoleSeverity::Warning, "No terrain under the river's points.");
                    OverrideChanged(entity, "SplineComponent", before, spline->Serialize());
                });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Puts the points on the ground (minus Surface Drop), never flowing uphill");
            ImGui::SameLine();
            if (ImGui::Button("Carve River Bed"))
                CarveWithUndo(shell, river);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Lowers the terrain under the river into its bed (Depth) and banks (Bank Width)");
        }

        void DrawWaterEffectsInspector(scene::Component &component, ComponentInspectorContext &context, std::vector<scene::Property> &)
        {
            auto &effects = static_cast<scene::WaterEffectsComponent &>(component);
            auto &entity = context.entity;
            const ImVec4 warning(1.0f, 0.75f, 0.3f, 1.0f);
            const bool ocean = entity.HasComponent<scene::OceanComponent>();
            const bool river = entity.HasComponent<scene::RiverComponent>();
            if (!ocean && !river)
            {
                ImGui::TextColored(warning, "Water effects need an Ocean or River component on this entity.");
                return;
            }
            ImGui::TextWrapped("Dynamic rigidbodies, and entities with a Water Interactor, make rings, splashes and sounds "
                               "here during play. Splashes start as the engine's built-in droplets (engine://builtin/particles/water-splash); "
                               "pick your own or clear one to skip it. Sounds left empty are skipped. Heavy variants play at or above "
                               "Heavy Intensity.");
            if (river && !ocean)
                ImGui::TextColored(warning, "Rivers splash and sound, but only Ocean surfaces draw ripples.");

            auto *scene = entity.GetScene();
            if (!scene || !scene->IsRuntimeStarted())
            {
                ImGui::TextDisabled("Enter play mode to test splashes.");
                return;
            }
            auto &water = scene->GetWaterSystem();
            ImGui::Text("Live rings: %zu", effects.GetRipples().GetRipples().size());
            if (water.IsListenerUnderwater())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(listener under water)");
            }
            static float testIntensity = 1.0f;
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderFloat("##TestIntensity", &testIntensity, 0.05f, 4.0f, "Intensity %.2f");
            ImGui::SameLine();
            if (ImGui::Button("Test Splash"))
            {
                const auto point = water.GetSnapshot().NearestSurfacePoint(entity.GetID(), entity.GetWorldPosition());
                if (!point || !water.Disturb(*scene, *point, testIntensity))
                    context.shell.Log(EditorShell::ConsoleSeverity::Warning, "No water surface found for a test splash.");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Splashes at the water nearest this entity, with every response of the profile");
            for (const auto &failed : water.GetSplashResponder().GetFailedAssets())
                ImGui::TextColored(warning, "Particles did not load: %s", failed.c_str());
        }
    }

    bool FitOceanAreaToTerrain(EditorShell &shell, scene::Entity &entity, scene::OceanComponent &ocean)
    {
        auto *scene = entity.GetScene();
        const glm::vec3 position = entity.GetWorldPosition();
        auto *terrain = scene ? TerrainUnder(*scene, position) : nullptr;
        if (!terrain)
        {
            shell.Log(EditorShell::ConsoleSeverity::Warning, "Move the ocean over a terrain basin first: there is no terrain under it.");
            return false;
        }
        const glm::mat4 terrainWorld = terrain->GetOwner()->GetWorldTransform();
        const glm::vec3 local = glm::vec3(glm::inverse(terrainWorld) * glm::vec4(position, 1.0f));
        const auto outline = scene::TraceWaterOutline(terrain->GetHeightData().field, terrain->GetCellSize(), local.y, {local.x, local.z},
                                                      {.maxPoints = 32, .shoreMargin = 2.0f});
        if (outline.empty())
        {
            shell.Log(EditorShell::ConsoleSeverity::Warning, "The ocean's entity is above the ground there: lower it into the basin.");
            return false;
        }
        const glm::mat4 toOcean = glm::inverse(entity.GetWorldTransform());
        scene::OceanAreaPolygon area;
        for (const auto &point : outline)
        {
            const glm::vec3 world = glm::vec3(terrainWorld * glm::vec4(point.x, local.y, point.y, 1.0f));
            const glm::vec3 inOcean = glm::vec3(toOcean * glm::vec4(world, 1.0f));
            area.points.push_back({inOcean.x, inOcean.z});
        }
        shell.ExecuteSceneEdit("Fit Lake To Terrain", [&] {
            const auto before = ocean.Serialize();
            ocean.SetAreas({std::move(area)});
            ocean.SetInvertAreaMask(true);
            OverrideChanged(entity, "OceanComponent", before, ocean.Serialize());
        });
        shell.Log(EditorShell::ConsoleSeverity::Info, "Lake shore: " + std::to_string(outline.size()) + " points.");
        return true;
    }

    void RenderLakeTools(EditorShell &shell, scene::Entity &entity, scene::OceanComponent &ocean)
    {
        if (ImGui::Button("Apply Lake Preset"))
            shell.ExecuteSceneEdit("Apply Lake Preset", [&] {
                const auto before = ocean.Serialize();
                ocean.ApplyLakePreset();
                OverrideChanged(entity, "OceanComponent", before, ocean.Serialize());
            });
        ImGui::SameLine();
        if (ImGui::Button("Fit Area To Terrain"))
            FitOceanAreaToTerrain(shell, entity, ocean);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Floods the terrain basin under this entity to the entity's height and makes the shore\n"
                              "the ocean's only area (Invert Area Mask on): a lake.");
    }

    void RegisterWaterInspectors()
    {
        RegisterComponentInspector(scene::GetComponentTypeID<scene::RiverComponent>(), &DrawRiverInspector);
        RegisterComponentInspector(scene::GetComponentTypeID<scene::WaterEffectsComponent>(), &DrawWaterEffectsInspector);
    }
}
