// The compact Terrain inspector: what the terrain is, the tools to edit it, and the rendering settings. Sculpting,

#include "PlutoGE/render/Material.h"
// painting, biomes and heights live in the Terrain Editor panel, which this points to.
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/TerrainWorldComponent.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/ComponentInspectors.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/TerrainEditorSections.h"
#include "PlutoGE/ui/TerrainViewportTool.h"

#include <imgui.h>

#include <algorithm>
#include <string_view>

namespace PlutoGE::ui
{
    namespace
    {
        // Shown elsewhere (the Terrain Editor), written by tools, or owned by a world's tiles.
        bool DrawnElsewhere(std::string_view name)
        {
            return name == "Width" || name == "Depth" || name == "HeightMap" || name == "HeightData" || name == "MaterialAsset" ||
                   name == "PaintedAlbedo" || name == "LayerCount" || name.starts_with("Layer.") || name.starts_with("WeightMap") ||
                   name == "WeightResolution" || name == "HeightBlend" || name == "DetailDistance" || name == "LodDistanceScale";
        }

        void DrawTerrainInspector(scene::Component &component, ComponentInspectorContext &context, std::vector<scene::Property> &properties)
        {
            auto &terrain = static_cast<scene::TerrainComponent &>(component);
            auto &shell = context.shell;
            auto &entity = context.entity;
            std::erase_if(properties, [&](const scene::Property &property) {
                return DrawnElsewhere(property.name) || terrain.IsRetiredProperty(property.name);
            });

            const auto target = TerrainEditTarget::Resolve(&entity);
            if (target.kind == TerrainEditTarget::Kind::World)
            {
                const bool isTemplate = scene::TerrainWorldComponent::IsTileTemplate(entity);
                ImGui::TextWrapped(isTemplate ? "Tile Template of %s: every tile copies these settings, layers and components."
                                              : "A streamed tile of %s. Its heights and paint save to the tile's files; settings "
                                                "come from the world's Tile Template.",
                                   target.Name().c_str());
            }
            else
                ImGui::Text("%d x %d samples, %.0f x %.0f m, %zu layers", terrain.GetWidth(), terrain.GetDepth(),
                            (terrain.GetWidth() - 1) * terrain.GetCellSize(), (terrain.GetDepth() - 1) * terrain.GetCellSize(),
                            terrain.GetLayers().GetLayers().size());
            DrawTerrainToolModeButtons(shell.GetTerrainTool(), target);
            ImGui::SameLine();
            if (ImGui::Button("Terrain Editor"))
                shell.OpenTerrainEditor();
            DrawTerrainMaterialPicker(shell, entity, terrain);
        }
    }

    void DrawTerrainMaterialPicker(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain)
    {
        auto selection = terrain.GetMaterialAssetReference();
        if (!RenderProjectAssetPicker("Material", shell.GetProject(), assets::ProjectAssetType::Material, selection,
                                      {.noneLabel = nullptr, .emptyPreview = "Default Shaded", .includeEngineAssets = true}))
            return;
        auto &assets = core::Engine::GetInstance().GetAssetManager();
        if (assets::Project::GetAssetTypeForReference(ResolveAssetPickerReference(assets.GetAssetCatalog().get(), selection)) !=
            assets::ProjectAssetType::Material)
            return;
        auto *material = assets.LoadMaterialAsset(selection);
        if (!material)
            return;
        terrain.SetMaterial(material);
        terrain.SetMaterialAssetReference(selection);
        entity.AddPrefabOverride("Component:TerrainComponent:MaterialAsset");
        shell.MarkSceneDirty();
    }

    void RegisterTerrainInspectors()
    {
        RegisterComponentInspector(scene::GetComponentTypeID<scene::TerrainComponent>(), &DrawTerrainInspector);
    }
}
