#include "FractureScriptApi.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Shatter.h"

#include <cmath>

namespace PlutoGE::scripting
{
    std::int32_t PLUTO_FRACTURE_API_CALL ShatterEntity(const NativeShatterRequest *request, const char *tag,
                                                       std::uint32_t *pieces, std::int32_t capacity)
    {
        if (!request || !pieces || capacity < static_cast<std::int32_t>(scene::kMaxShatterPieces))
            return 0;
        auto *activeScene = core::Engine::GetInstance().GetScene();
        auto *root = activeScene ? activeScene->FindEntityByID(request->root) : nullptr;
        if (!root || !activeScene->IsEntityAlive(request->root))
            return 0;

        scene::ShatterSettings settings{
            .pieces = request->pieces,
            .seed = request->seed,
            .impactDirection = {request->impactDirection[0], request->impactDirection[1], request->impactDirection[2]},
            .impactBias = request->impactBias,
            .density = request->density,
            .burstSpeed = request->burstSpeed,
            .impactSpeed = request->impactSpeed,
            .spin = request->spin,
            .minPieceSize = request->minPieceSize,
            .tag = tag ? tag : "",
        };
        const glm::vec3 impact(request->impactPoint[0], request->impactPoint[1], request->impactPoint[2]);
        if (request->hasImpact != 0 && std::isfinite(impact.x) && std::isfinite(impact.y) && std::isfinite(impact.z))
            settings.impactPoint = impact;

        const auto spawned = scene::Shatter(*activeScene, *root, settings);
        std::int32_t count = 0;
        for (const auto *piece : spawned)
            if (piece && count < capacity)
                pieces[count++] = piece->GetID();
        return count;
    }
}
