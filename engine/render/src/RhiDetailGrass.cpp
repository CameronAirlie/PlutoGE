#include "RhiDetailGrass.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/CameraTagFilter.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/DetailGrassComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <algorithm>

namespace PlutoGE::render
{
    std::vector<BasicDetailGrassField> CollectRhiDetailGrass(const scene::Scene &scene, const scene::CameraTagFilter *filter,
                                                             const RhiDetailGrassResolvers &resolvers)
    {
        std::vector<BasicDetailGrassField> fields;
        for (const auto *component : scene.GetDetailGrassComponents())
        {
            auto *owner = component ? component->GetOwner() : nullptr;
            if (!owner || !component->IsEnabled() || !owner->IsActiveInHierarchy() || (filter && !filter->Accepts(owner)))
                continue;
            auto *terrain = component->GetTerrain();
            if (!terrain || !terrain->IsEnabled())
                continue;
            auto heights = component->GetHeightField();
            if (!heights)
                continue;
            auto &stack = terrain->GetLayers();
            const auto set = stack.Empty() ? nullptr : stack.GetRenderSet();
            const auto *macro = stack.Empty() ? nullptr : stack.GetMacroAlbedo();
            const auto groundAlbedo = macro ? resolvers.texture(macro, true) : rhi::TextureHandle{};
            const auto &terrainLayers = stack.GetLayers();
            const auto &layers = component->GetLayers();
            for (std::size_t index = 0; index < layers.size(); ++index)
            {
                const auto &layer = layers[index];
                if (!layer.settings.enabled)
                    continue;
                BasicDetailGrassField field;
                field.id = (static_cast<std::uint64_t>(owner->GetID()) << 8u) | index;
                field.terrainToWorld = owner->GetWorldTransform();
                field.heights = heights;
                field.groundAlbedo = groundAlbedo;
                field.layer = layer.settings;
                if (!layer.densityLayer.empty())
                {
                    const auto found = std::find_if(terrainLayers.begin(), terrainLayers.end(),
                                                    [&](const auto &terrainLayer) { return terrainLayer.name == layer.densityLayer; });
                    if (found == terrainLayers.end() || !set)
                        continue;
                    const auto terrainLayer = static_cast<std::size_t>(found - terrainLayers.begin());
                    const auto *weights = set->weightMaps[terrainLayer / 4];
                    field.density = weights ? resolvers.texture(weights, false) : rhi::TextureHandle{};
                    if (!field.density)
                        continue;
                    field.densityChannel = glm::vec4(0.0f);
                    field.densityChannel[static_cast<int>(terrainLayer % 4)] = 1.0f;
                }
                if (!layer.materialAsset.empty())
                    if (const auto *material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(layer.materialAsset))
                        field.material = resolvers.material(material);
                fields.push_back(std::move(field));
            }
        }
        return fields;
    }
}
