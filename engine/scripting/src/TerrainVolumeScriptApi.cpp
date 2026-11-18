#include "TerrainVolumeScriptApi.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/TerrainVolumeEditing.h"
#include "PlutoGE/scene/components/TerrainVolumeComponent.h"
#include "PlutoGE/scene/volume/VolumeEditArchive.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <span>

namespace PlutoGE::scripting
{
    namespace
    {
        glm::vec3 Vector(const float (&value)[3]) { return {value[0], value[1], value[2]}; }

        std::unique_ptr<scene::volume::VolumeShape> MakeShape(const NativeTerrainVolumeRequest &request)
        {
            const float radius = std::max(request.radius, 0.0f);
            switch (static_cast<TerrainVolumeScriptShape>(request.shape))
            {
            case TerrainVolumeScriptShape::Sphere:
                return std::make_unique<scene::volume::SphereShape>(Vector(request.a), radius);
            case TerrainVolumeScriptShape::Capsule:
                return std::make_unique<scene::volume::CapsuleShape>(Vector(request.a), Vector(request.b), radius);
            case TerrainVolumeScriptShape::Box:
            {
                const glm::quat rotation(request.rotation[3], request.rotation[0], request.rotation[1], request.rotation[2]);
                const float length = glm::length(rotation);
                return std::make_unique<scene::volume::BoxShape>(Vector(request.a), glm::max(Vector(request.halfExtents), glm::vec3(0.0f)),
                                                                length > 1.0e-6f ? rotation / length : glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            }
            }
            return nullptr;
        }

        bool ValidMode(std::int32_t mode)
        {
            return mode >= static_cast<std::int32_t>(scene::volume::VolumeEditMode::Subtract) &&
                   mode <= static_cast<std::int32_t>(scene::volume::VolumeEditMode::Paint);
        }

        void Report(NativeTerrainVolumeRequest &request, const scene::volume::VolumeEditResult &result)
        {
            request.rockRemoved = result.RockRemoved();
            request.rockAdded = result.rockAdded;
            request.mainMaterial = -1;
            float most = 0.0f;
            for (const auto &[material, amount] : result.rockRemoved)
                if (amount > most)
                {
                    most = amount;
                    request.mainMaterial = material;
                }
        }

        scene::TerrainVolumeComponent *FindVolume(scene::Scene &scene, std::uint32_t entityId)
        {
            auto *entity = scene.FindEntityByID(entityId);
            return entity ? entity->GetComponent<scene::TerrainVolumeComponent>() : nullptr;
        }

        // The last capture, so a size query followed by the copy does not capture twice (scripts run on one thread).
        std::vector<std::uint8_t> &LastCapture()
        {
            static std::vector<std::uint8_t> capture;
            return capture;
        }
    }

    std::int32_t PLUTO_TERRAIN_VOLUME_API_CALL ControlTerrainVolume(NativeTerrainVolumeRequest *request)
    {
        if (!request)
            return 0;
        try
        {
            auto *scene = core::Engine::GetInstance().GetScene();
            request->result = 0;
            if (!scene)
                return 0;
            auto *volume = request->entity != 0 ? FindVolume(*scene, request->entity) : nullptr;
            if (request->entity != 0 && !volume)
                return 0;
            const std::span<std::uint8_t> buffer(reinterpret_cast<std::uint8_t *>(static_cast<std::uintptr_t>(request->buffer)),
                                                 request->buffer && request->bufferSize > 0 ? static_cast<std::size_t>(request->bufferSize) : 0);

            switch (static_cast<TerrainVolumeScriptOperation>(request->operation))
            {
            case TerrainVolumeScriptOperation::Edit:
            {
                const auto shape = MakeShape(*request);
                if (!shape || !ValidMode(request->mode) || request->material < 0 || request->material > 255)
                    return 0;
                const scene::volume::VolumeEdit edit{static_cast<scene::volume::VolumeEditMode>(request->mode),
                                                     static_cast<scene::volume::VolumeMaterial>(request->material)};
                if (volume)
                {
                    const auto result = volume->Apply(*shape, edit);
                    Report(*request, result);
                    request->volumesChanged = result.Changed() ? 1 : 0;
                    request->foliageRemoved = 0;
                    request->result = result.Changed() ? 1 : 0;
                    return 1;
                }
                scene::TerrainVolumeEditOptions options;
                if (request->maxExtent >= 0.0f)
                    options.maxExtent = request->maxExtent;
                const auto report = scene::EditTerrainVolumes(*scene, *shape, edit, options);
                Report(*request, report.result);
                request->volumesChanged = report.volumesChanged;
                request->foliageRemoved = static_cast<std::int32_t>(report.foliageRemoved);
                request->result = report.refused ? -1 : report.Changed() ? 1 : 0;
                return 1;
            }
            case TerrainVolumeScriptOperation::Flush:
                if (volume)
                    volume->Flush();
                else
                    scene::FlushTerrainVolumes(*scene);
                request->result = 1;
                return 1;
            case TerrainVolumeScriptOperation::IsUpToDate:
                request->result = !volume || volume->IsUpToDate() ? 1 : 0;
                return 1;
            case TerrainVolumeScriptOperation::Save:
                request->result = volume && volume->SaveFile() ? 1 : 0;
                return 1;
            case TerrainVolumeScriptOperation::CaptureEdits:
            {
                auto &capture = LastCapture();
                // A buffer that fits the last capture is the copy that follows the size query.
                if (buffer.empty() || buffer.size() != capture.size())
                    capture = scene::CaptureTerrainVolumeEdits(*scene);
                if (!buffer.empty() && buffer.size() >= capture.size())
                {
                    std::memcpy(buffer.data(), capture.data(), capture.size());
                    request->result = static_cast<std::int32_t>(capture.size());
                    capture.clear();
                    capture.shrink_to_fit();
                    return 1;
                }
                request->result = static_cast<std::int32_t>(capture.size());
                return 1;
            }
            case TerrainVolumeScriptOperation::RestoreEdits:
                request->result = scene::RestoreTerrainVolumeEdits(*scene, buffer) ? 1 : 0;
                return 1;
            case TerrainVolumeScriptOperation::ChunkCount:
                request->result = volume ? static_cast<std::int32_t>(volume->GetChunkEntityCount()) : 0;
                return 1;
            case TerrainVolumeScriptOperation::ClearEdits:
                request->result = scene::RestoreTerrainVolumeEdits(*scene, scene::volume::VolumeEditArchive::Encode({})) ? 1 : 0;
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
