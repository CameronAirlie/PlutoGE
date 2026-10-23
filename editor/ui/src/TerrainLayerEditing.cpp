#include "PlutoGE/ui/TerrainLayerEditing.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/EditorShell.h"

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr std::array kWeightResolutions{256, 512, 1024, 2048};
        constexpr const char *kLayerFields[] = {"Name", "AlbedoMap", "NormalMap", "RoughnessMap", "HeightMap", "Tint",
                                               "TileSize", "NormalStrength", "Roughness", "Metallic", "AntiTiling"};

        std::size_t WeightBytes(const scene::TerrainLayerStack::Snapshot &snapshot)
        {
            const auto size = static_cast<std::size_t>(snapshot.weights.GetResolution());
            return size * size * scene::TerrainWeightMap::kLayers;
        }

        scene::TerrainComponent *FindTerrain(std::uint64_t entityId, scene::Entity **owner = nullptr)
        {
            auto *scene = EditorShell::GetInstance().GetScene();
            auto *entity = scene ? scene->FindEntityByID(static_cast<scene::EntityID>(entityId)) : nullptr;
            if (owner)
                *owner = entity;
            return entity ? entity->GetComponent<scene::TerrainComponent>() : nullptr;
        }

        // Prefab overrides for everything the layer list serializes (a layer edit can renumber layers).
        void OverrideLayerProperties(scene::Entity &entity, const scene::TerrainComponent &terrain)
        {
            for (const char *name : {"LayerCount", "HeightBlend", "DetailDistance", "WeightResolution", "WeightMap0", "WeightMap1", "PaintLayer"})
                entity.AddPrefabOverride(std::string("Component:TerrainComponent:") + name);
            for (std::size_t index = 0; index < terrain.GetLayers().GetLayers().size(); ++index)
                for (const char *field : kLayerFields)
                    entity.AddPrefabOverride("Component:TerrainComponent:Layer." + std::to_string(index) + "." + field);
        }

        // Gives the terrain weight files for every plane its layers use (before any state is captured for undo, so
        // assigning them is part of the edit that needs them).
        bool EnsureWeightMapPaths(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain)
        {
            auto *project = shell.GetProject();
            if (!project)
                return false;
            auto &layers = terrain.GetLayers();
            auto paths = layers.GetWeightMapPaths();
            const std::size_t planes = std::max<std::size_t>(1, (layers.GetLayers().size() + 3) / 4);
            bool assigned = false;
            const auto *scene = entity.GetScene();
            const std::string stem = scene && !scene->GetFilePath().empty()
                ? std::filesystem::path(scene->GetFilePath()).stem().string() : std::string("Scene");
            for (std::size_t plane = 0; plane < planes; ++plane)
            {
                if (!paths[plane].empty())
                    continue;
                const auto file = project->GetAssetDirectoryPath() / "TerrainLayers" /
                    (stem + "_" + std::to_string(entity.GetID()) + "_weights" + std::to_string(plane) + ".png");
                paths[plane] = project->MakeAssetReference(file);
                assigned = true;
            }
            if (assigned)
            {
                layers.SetWeightMapPaths(paths);
                entity.AddPrefabOverride("Component:TerrainComponent:WeightMap0");
                entity.AddPrefabOverride("Component:TerrainComponent:WeightMap1");
            }
            return true;
        }

        // An edit that moves weights (and possibly the layer list): one undo step restoring both.
        template <typename Edit>
        void EditWeights(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain, std::string label, Edit &&edit)
        {
            shell.FlushPendingSceneEdits();
            EnsureWeightMapPaths(shell, entity, terrain);
            auto before = terrain.GetLayers().Capture();
            edit(terrain.GetLayers());
            auto after = terrain.GetLayers().Capture();
            OverrideLayerProperties(entity, terrain);
            SaveTerrainWeightMaps(shell, entity, terrain);
            const auto bytes = WeightBytes(before) + WeightBytes(after);
            const std::uint64_t id = entity.GetID();
            const auto restore = [id](const scene::TerrainLayerStack::Snapshot &snapshot) {
                scene::Entity *owner = nullptr;
                auto *target = FindTerrain(id, &owner);
                if (!target)
                    return false;
                target->GetLayers().Restore(snapshot);
                SaveTerrainWeightMaps(EditorShell::GetInstance(), *owner, *target);
                return true;
            };
            shell.PushSceneEditCommand(std::move(label), [restore, before = std::move(before)] { return restore(before); },
                                       [restore, after = std::move(after)] { return restore(after); }, bytes);
        }

        // One texture slot of a layer; true when it changed.
        bool TextureSlot(EditorShell &shell, const char *label, std::string &reference)
        {
            return RenderProjectAssetPicker(label, shell.GetProject(), assets::ProjectAssetType::Texture, reference);
        }
    }

    bool SaveTerrainWeightMaps(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain)
    {
        auto &layers = terrain.GetLayers();
        if (layers.Empty())
            return true;
        auto *project = shell.GetProject();
        if (!project || !EnsureWeightMapPaths(shell, entity, terrain))
            return false;
        for (const auto &reference : layers.GetWeightMapPaths())
            if (!reference.empty())
                std::filesystem::create_directories(project->ResolveAssetReference(reference).parent_path());
        if (!layers.SaveWeightMaps())
        {
            shell.Log(EditorShell::ConsoleSeverity::Error, "Could not write the terrain's weight maps.");
            return false;
        }
        for (const auto &reference : layers.GetWeightMapPaths())
            if (!reference.empty())
                project->RefreshAssetEntry(project->ResolveAssetReference(reference));
        return true;
    }

    void RenderTerrainLayerInspector(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain)
    {
        auto &stack = terrain.GetLayers();
        ImGui::SeparatorText("Material Layers");
        if (stack.Empty())
            ImGui::TextWrapped("Add layers to paint materials onto the terrain. Each layer has its own maps and tiling; "
                               "paint them with the viewport's Paint > Layer brush.");

        const auto &layers = stack.GetLayers();
        std::optional<std::size_t> removeLayer;
        std::optional<std::pair<std::size_t, std::size_t>> moveLayer;
        for (std::size_t index = 0; index < layers.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(index));
            auto layer = layers[index];
            const bool painting = terrain.IsPaintEnabled() && terrain.GetPaintMode() == scene::TerrainPaintMode::Layer &&
                                  terrain.GetPaintLayer() == static_cast<int>(index);
            if (ImGui::RadioButton("##Paint", painting))
            {
                // One click from the inspector to painting this layer.
                terrain.SetPaintEnabled(true);
                terrain.SetPaintMode(scene::TerrainPaintMode::Layer);
                terrain.SetPaintLayer(static_cast<int>(index));
                shell.MarkSceneDirty();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Paint this layer (Paint > Layer in the viewport)");
            ImGui::SameLine();
            const std::string title = (layer.name.empty() ? "Layer " + std::to_string(index + 1) : layer.name) + "###Layer";
            const bool open = ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - 78.0f);
            ImGui::BeginDisabled(index == 0);
            if (ImGui::ArrowButton("##Up", ImGuiDir_Up))
                moveLayer = std::pair{index, index - 1};
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(index + 1 == layers.size());
            if (ImGui::ArrowButton("##Down", ImGuiDir_Down))
                moveLayer = std::pair{index, index + 1};
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                removeLayer = index;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Remove the layer; its paint goes to the others");
            if (open)
            {
                bool changed = false;
                char name[128]{};
                layer.name.copy(name, sizeof(name) - 1);
                if (ImGui::InputText("Name", name, sizeof(name)))
                {
                    layer.name = name;
                    changed = true;
                }
                changed |= TextureSlot(shell, "Albedo", layer.albedoMap);
                changed |= TextureSlot(shell, "Normal", layer.normalMap);
                changed |= TextureSlot(shell, "Roughness", layer.roughnessMap);
                changed |= TextureSlot(shell, "Height", layer.heightMap);
                changed |= ImGui::ColorEdit3("Tint", &layer.tint.x);
                changed |= ImGui::DragFloat("Tile Size", &layer.tileSize, 0.05f, 0.05f, 1000.0f, "%.2f m");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("World units covered by one repeat of the layer's maps");
                changed |= ImGui::SliderFloat("Normal Strength", &layer.normalStrength, 0.0f, 2.0f);
                changed |= ImGui::SliderFloat("Roughness", &layer.roughness, 0.0f, 2.0f);
                changed |= ImGui::SliderFloat("Metallic", &layer.metallic, 0.0f, 1.0f);
                changed |= ImGui::Checkbox("Anti-Tiling", &layer.antiTiling);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Hide the maps' repeats (costs a second texture sample per map)");
                if (changed)
                {
                    stack.SetLayer(index, std::move(layer));
                    OverrideLayerProperties(entity, terrain);
                    shell.MarkSceneDirty();
                }
                if (ImGui::Button("Fill Terrain"))
                    EditWeights(shell, entity, terrain, "Fill Terrain Layer", [index](auto &target) { target.Fill(index); });
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Cover the whole terrain with this layer");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removeLayer)
            EditWeights(shell, entity, terrain, "Remove Terrain Layer", [index = *removeLayer](auto &target) { target.RemoveLayer(index); });
        else if (moveLayer)
            EditWeights(shell, entity, terrain, "Reorder Terrain Layers",
                        [move = *moveLayer](auto &target) { target.MoveLayer(move.first, move.second); });

        ImGui::BeginDisabled(layers.size() >= scene::TerrainLayerStack::kMaxLayers);
        if (ImGui::Button("Add Layer"))
        {
            scene::TerrainLayer layer;
            layer.name = "Layer " + std::to_string(layers.size() + 1);
            const bool first = stack.Empty();
            stack.AddLayer(std::move(layer));
            if (first)
            {
                // The first layer is ready to paint over.
                terrain.SetPaintLayer(0);
            }
            OverrideLayerProperties(entity, terrain);
            shell.MarkSceneDirty();
        }
        ImGui::EndDisabled();
        if (stack.Empty())
            return;

        float heightBlend = stack.GetHeightBlend();
        if (ImGui::SliderFloat("Height Blend", &heightBlend, 0.0f, 1.0f))
        {
            stack.SetHeightBlend(heightBlend);
            entity.AddPrefabOverride("Component:TerrainComponent:HeightBlend");
            shell.MarkSceneDirty();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0 blends layers by their painted weights. Higher values let the taller surface (by the\n"
                              "layers' height maps) show through transitions, e.g. stones through sand.");

        float detailDistance = stack.GetDetailDistance();
        if (ImGui::DragFloat("Detail Distance", &detailDistance, 1.0f, 0.0f, 5000.0f, detailDistance > 0.0f ? "%.0f" : "Unlimited"))
        {
            stack.SetDetailDistance(detailDistance);
            entity.AddPrefabOverride("Component:TerrainComponent:DetailDistance");
            shell.MarkSceneDirty();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Beyond this distance from the camera the terrain shows its macro colour instead of the\n"
                              "layers, which is much cheaper to shade. 0 shows the layers at every distance.");

        const int resolution = stack.GetWeights().GetResolution();
        const std::string preview = std::to_string(resolution) + " x " + std::to_string(resolution);
        if (ImGui::BeginCombo("Paint Resolution", preview.c_str()))
        {
            for (const int option : kWeightResolutions)
                if (ImGui::Selectable((std::to_string(option) + " x " + std::to_string(option)).c_str(), option == resolution) &&
                    option != resolution)
                    EditWeights(shell, entity, terrain, "Terrain Paint Resolution",
                                [option](auto &target) { target.SetWeightResolution(option); });
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Texels of the layer weight maps across the terrain (existing paint is resampled)");
        for (std::size_t plane = 0; plane < stack.GetWeightMapPaths().size(); ++plane)
            if (const auto &path = stack.GetWeightMapPaths()[plane]; !path.empty())
                ImGui::TextDisabled("Weights %zu: %s", plane, path.c_str());
    }

    void RenderTerrainLayerPaintControls(scene::TerrainComponent &terrain)
    {
        const auto &layers = terrain.GetLayers().GetLayers();
        if (layers.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Add material layers in the inspector");
            return;
        }
        const int selected = std::clamp(terrain.GetPaintLayer(), 0, static_cast<int>(layers.size()) - 1);
        const auto label = [&](std::size_t index) {
            return layers[index].name.empty() ? "Layer " + std::to_string(index + 1) : layers[index].name;
        };
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::BeginCombo("Layer##TerrainPaint", label(static_cast<std::size_t>(selected)).c_str()))
        {
            for (std::size_t index = 0; index < layers.size(); ++index)
                if (ImGui::Selectable((label(index) + "##" + std::to_string(index)).c_str(), static_cast<int>(index) == selected))
                    terrain.SetPaintLayer(static_cast<int>(index));
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        float opacity = terrain.GetLayerOpacity();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::SliderFloat("Opacity##TerrainPaint", &opacity, 0.01f, 1.0f, "%.2f"))
            terrain.SetLayerOpacity(opacity);
        ImGui::SameLine();
        float hardness = terrain.GetBrushHardness();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::SliderFloat("Hardness##TerrainPaint", &hardness, 0.0f, 1.0f, "%.2f"))
            terrain.SetBrushHardness(hardness);
    }

    void TerrainLayerStroke::Dab(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain, const glm::vec3 &worldHit)
    {
        if (!m_before || m_entity != entity.GetID())
        {
            if (m_before)
                End(shell);
            shell.FlushPendingSceneEdits();
            EnsureWeightMapPaths(shell, entity, terrain);
            m_entity = entity.GetID();
            m_before = terrain.GetLayers().GetWeights();
            m_changed = {};
            m_sampler.Reset();
        }
        const glm::mat4 world = entity.GetWorldTransform();
        const glm::vec3 local = glm::vec3(glm::inverse(world) * glm::vec4(worldHit, 1.0f));
        // Dabs a quarter radius apart, so a stroke's coverage does not depend on frame rate or mouse speed.
        m_sampler.Sample({local.x, local.z}, std::max(0.05f, terrain.GetBrushRadius() * 0.25f), [&](glm::vec2 dab) {
            m_changed.Include(terrain.PaintLayerAtWorldPosition(glm::vec3(world * glm::vec4(dab.x, local.y, dab.y, 1.0f))));
        });
    }

    void TerrainLayerStroke::End(EditorShell &shell)
    {
        if (!m_before)
            return;
        scene::Entity *entity = nullptr;
        auto *terrain = FindTerrain(m_entity, &entity);
        if (terrain && !m_changed.Empty())
        {
            auto before = m_before->Capture(m_changed);
            auto after = terrain->GetLayers().GetWeights().Capture(m_changed);
            SaveTerrainWeightMaps(shell, *entity, *terrain);
            const auto bytes = before.weights.size() + after.weights.size();
            const auto restore = [id = m_entity](const scene::TerrainWeightMap::Region &region) {
                scene::Entity *owner = nullptr;
                auto *target = FindTerrain(id, &owner);
                if (!target)
                    return false;
                target->GetLayers().RestoreWeights(region);
                SaveTerrainWeightMaps(EditorShell::GetInstance(), *owner, *target);
                return true;
            };
            shell.PushSceneEditCommand("Paint Terrain Layer", [restore, before = std::move(before)] { return restore(before); },
                                       [restore, after = std::move(after)] { return restore(after); }, bytes);
        }
        m_before.reset();
        m_changed = {};
        m_sampler.Reset();
    }
}
