#include "TerrainScriptApi.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <algorithm>
#include <cstring>

namespace PlutoGE::scripting
{
    namespace
    {
        scene::TerrainComponent *FindTerrain(std::uint32_t entityId)
        {
            auto *scene = core::Engine::GetInstance().GetScene();
            auto *entity = scene ? scene->FindEntityByID(entityId) : nullptr;
            return entity ? entity->GetComponent<scene::TerrainComponent>() : nullptr;
        }

        // The terrain UV under a world position, or false off the terrain.
        bool OnTerrain(const scene::TerrainComponent &terrain, const glm::vec3 &position, glm::vec2 &uv)
        {
            uv = terrain.WorldToTerrainUv(position);
            return uv.x >= 0.0f && uv.y >= 0.0f && uv.x <= 1.0f && uv.y <= 1.0f;
        }
    }

    std::int32_t PLUTO_TERRAIN_API_CALL ControlTerrain(NativeTerrainRequest *request)
    {
        if (!request)
            return 0;
        try
        {
            auto *terrain = FindTerrain(request->entity);
            request->result = 0;
            if (!terrain)
                return 0;
            const glm::vec3 position(request->position[0], request->position[1], request->position[2]);
            auto &layers = terrain->GetLayers();
            const auto layerCount = static_cast<std::int32_t>(layers.GetLayers().size());
            glm::vec2 uv;
            switch (static_cast<TerrainScriptOperation>(request->operation))
            {
            case TerrainScriptOperation::SampleSurface:
            {
                glm::vec3 normal;
                if (terrain->TrySampleWorldSurface(position, request->height, normal))
                {
                    std::copy_n(&normal.x, 3, request->normal);
                    request->result = 1;
                }
                return 1;
            }
            case TerrainScriptOperation::LayerCount:
                request->result = layerCount;
                return 1;
            case TerrainScriptOperation::LayerName:
            {
                if (request->layer < 0 || request->layer >= layerCount)
                    return 1;
                const auto &name = layers.GetLayers()[static_cast<std::size_t>(request->layer)].name;
                const auto length = std::min(name.size(), sizeof(request->name) - 1);
                std::memcpy(request->name, name.data(), length);
                request->name[length] = '\0';
                request->result = 1;
                return 1;
            }
            case TerrainScriptOperation::LayerWeights:
                if (OnTerrain(*terrain, position, uv))
                {
                    for (std::int32_t layer = 0; layer < layerCount; ++layer)
                        request->weights[layer] = layers.GetWeights().SampleWeight(uv, static_cast<std::size_t>(layer));
                    request->result = 1;
                }
                return 1;
            case TerrainScriptOperation::DominantLayer:
                request->result = layerCount > 0 && OnTerrain(*terrain, position, uv)
                    ? static_cast<std::int32_t>(layers.GetWeights().DominantLayer(uv)) : -1;
                return 1;
            case TerrainScriptOperation::PaintLayer:
                if (request->layer < 0 || request->layer >= layerCount)
                    return 1;
                request->result = terrain->PaintLayerAtWorldPosition(position, static_cast<std::size_t>(request->layer), request->radius,
                    scene::TerrainLayerBrush{std::clamp(request->opacity, 0.0f, 1.0f), std::clamp(request->hardness, 0.0f, 1.0f)}).Empty() ? 0 : 1;
                return 1;
            default:
                return 0;
            }
        }
        catch (...)
        {
            return 0;
        }
    }
}
