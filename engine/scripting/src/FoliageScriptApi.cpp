#include "FoliageScriptApi.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/FoliageProxies.h"
#include "PlutoGE/scene/FoliageQueries.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/FoliageProxyComponent.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>

namespace PlutoGE::scripting
{
    namespace
    {
        scene::Entity *FindEntity(std::uint32_t entityId)
        {
            auto *scene = core::Engine::GetInstance().GetScene();
            return scene ? scene->FindEntityByID(entityId) : nullptr;
        }

        void CopyName(NativeFoliageRequest &request, std::string_view text)
        {
            const auto length = (std::min)(text.size(), sizeof(request.name) - 1);
            std::memcpy(request.name, text.data(), length);
            request.name[length] = '\0';
        }

        // Splits an affine world transform into position, rotation and per-axis scale.
        void WriteTransform(NativeFoliageRequest &request, const glm::mat4 &transform)
        {
            glm::vec3 axes[3]{glm::vec3(transform[0]), glm::vec3(transform[1]), glm::vec3(transform[2])};
            for (int axis = 0; axis < 3; ++axis)
            {
                request.scale[axis] = glm::length(axes[axis]);
                if (request.scale[axis] > 0.0f) axes[axis] /= request.scale[axis];
            }
            const glm::quat rotation = glm::normalize(glm::quat_cast(glm::mat3(axes[0], axes[1], axes[2])));
            request.rotation[0] = rotation.x;
            request.rotation[1] = rotation.y;
            request.rotation[2] = rotation.z;
            request.rotation[3] = rotation.w;
            request.position[0] = transform[3].x;
            request.position[1] = transform[3].y;
            request.position[2] = transform[3].z;
        }

        // The world-space box round what the instance draws (its mesh's box carried through the instance's transform);
        // just the pivot when that is unknown.
        void WriteBounds(NativeFoliageRequest &request, const std::optional<scene::FoliageInstanceBounds> &bounds,
                         const glm::mat4 &transform)
        {
            glm::vec3 low(transform[3]), high(transform[3]);
            if (bounds)
            {
                low = glm::vec3(std::numeric_limits<float>::max());
                high = glm::vec3(-std::numeric_limits<float>::max());
                for (int corner = 0; corner < 8; ++corner)
                {
                    const glm::vec3 local((corner & 1) ? bounds->localMax.x : bounds->localMin.x,
                                          (corner & 2) ? bounds->localMax.y : bounds->localMin.y,
                                          (corner & 4) ? bounds->localMax.z : bounds->localMin.z);
                    const glm::vec3 world(bounds->model * glm::vec4(local, 1.0f));
                    low = glm::min(low, world);
                    high = glm::max(high, world);
                }
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                request.boundsMin[axis] = low[axis];
                request.boundsMax[axis] = high[axis];
            }
        }

        std::int32_t GetInstance(scene::FoliageComponent &foliage, NativeFoliageRequest &request)
        {
            const auto location = foliage.FindInstance(request.instanceId);
            const auto transform = location ? foliage.GetInstanceWorldTransform(*location) : std::nullopt;
            if (!location || !transform) return 1;
            const auto &type = *foliage.GetType(location->typeIndex);
            request.typeIndex = static_cast<std::int32_t>(location->typeIndex);
            request.hasProxyPrefab = type.asset.proxyPrefabReference.empty() ? 0 : 1;
            WriteTransform(request, *transform);
            WriteBounds(request, foliage.GetInstanceBounds(*location), *transform);
            CopyName(request, type.name);
            request.result = 1;
            return 1;
        }

        std::int32_t FindInRadius(scene::FoliageComponent &foliage, NativeFoliageRequest &request)
        {
            if (request.capacity < 0 || (request.capacity > 0 && !request.ids)) return 0;
            const glm::vec3 center(request.position[0], request.position[1], request.position[2]);
            const auto locations = foliage.FindInstancesInRadius(center, request.radius);
            const auto count = static_cast<std::int32_t>(locations.size());
            for (std::int32_t index = 0; index < (std::min)(count, request.capacity); ++index)
                request.ids[index] = foliage.GetInstance(locations[static_cast<std::size_t>(index)])->id;
            request.result = count;
            return 1;
        }

        std::int32_t FindInScene(NativeFoliageRequest &request)
        {
            if (request.capacity < 0 || (request.capacity > 0 && !request.matches)) return 0;
            const auto *scene = core::Engine::GetInstance().GetScene();
            if (!scene) return 0;
            request.name[sizeof(request.name) - 1] = '\0';
            const auto matches = scene::FindFoliageInstances(*scene, {
                .center = {request.position[0], request.position[1], request.position[2]},
                .radius = request.radius,
                .interactionTag = request.name,
                .requireProxy = request.hasProxyPrefab != 0,
            });
            const auto count = static_cast<std::int32_t>(matches.size());
            for (std::int32_t index = 0; index < (std::min)(count, request.capacity); ++index)
            {
                const auto &match = matches[static_cast<std::size_t>(index)];
                request.matches[index] = {match.foliageEntity, static_cast<std::int32_t>(match.typeIndex), match.instanceId, match.distance};
            }
            request.result = count;
            return 1;
        }
    }

    std::int32_t PLUTO_FOLIAGE_API_CALL ControlFoliage(NativeFoliageRequest *request)
    {
        if (!request)
            return 0;
        try
        {
            request->result = 0;
            if (static_cast<FoliageScriptOperation>(request->operation) == FoliageScriptOperation::FindInScene)
                return FindInScene(*request);
            auto *entity = FindEntity(request->entity);
            if (!entity)
                return 0;
            auto *foliage = entity->GetComponent<scene::FoliageComponent>();
            auto *link = entity->GetComponent<scene::FoliageProxyComponent>();
            switch (static_cast<FoliageScriptOperation>(request->operation))
            {
            case FoliageScriptOperation::GetInstance:
                return foliage ? GetInstance(*foliage, *request) : 0;
            case FoliageScriptOperation::FindInRadius:
                return foliage ? FindInRadius(*foliage, *request) : 0;
            case FoliageScriptOperation::Promote:
            {
                if (!foliage)
                    return 0;
                std::string error;
                if (auto *proxy = scene::PromoteFoliageInstance(*foliage, request->instanceId, &error))
                {
                    request->linkedEntity = proxy->GetID();
                    request->result = 1;
                }
                else
                {
                    CopyName(*request, error);
                }
                return 1;
            }
            case FoliageScriptOperation::Release:
                if (!link)
                    return 0;
                request->result = scene::ReleaseFoliageProxy(*entity) ? 1 : 0;
                return 1;
            case FoliageScriptOperation::FindInScene:
                return 0; // handled above, without an entity
            case FoliageScriptOperation::GetProxyLink:
                if (link)
                {
                    request->linkedEntity = link->GetFoliageEntityId();
                    request->instanceId = link->GetInstanceId();
                    request->result = 1;
                }
                return 1;
            }
            return 0;
        }
        catch (...)
        {
            return 0;
        }
    }
}
