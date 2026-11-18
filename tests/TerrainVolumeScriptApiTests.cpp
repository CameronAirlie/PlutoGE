// The native side of the C# TerrainVolumes API (TerrainVolumeScriptApi, docs/dev/TERRAIN_VOLUMES_PLAN.md phase 5):
// requests as the managed bridge sends them, against a scene with a dug-able terrain. The managed half is covered by
// PlutoGETerrainVolumeManagedTests.
#include "TerrainVolumeScriptApi.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/TerrainVolumeComponent.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE;
using namespace PlutoGE::scripting;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    NativeTerrainVolumeRequest Sphere(TerrainVolumeScriptOperation operation, float x, float y, float z, float radius, std::int32_t mode = 0)
    {
        NativeTerrainVolumeRequest request;
        request.operation = static_cast<std::int32_t>(operation);
        request.shape = static_cast<std::int32_t>(TerrainVolumeScriptShape::Sphere);
        request.mode = mode;
        request.a[0] = x;
        request.a[1] = y;
        request.a[2] = z;
        request.radius = radius;
        return request;
    }

    NativeTerrainVolumeRequest Operation(TerrainVolumeScriptOperation operation, std::uint32_t entity = 0)
    {
        NativeTerrainVolumeRequest request;
        request.operation = static_cast<std::int32_t>(operation);
        request.entity = entity;
        return request;
    }
}

int main()
try
{
    auto &engine = core::Engine::GetInstance();
    struct Reset
    {
        ~Reset() { core::Engine::GetInstance().SetScene(nullptr); }
    } reset;

    scene::Scene scene;
    auto *entity = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Terrain"}));
    auto *terrain = entity->CreateComponent<scene::TerrainComponent>(scene::TerrainComponentConfig{.width = 33, .depth = 33, .cellSize = 1.0f});
    std::string heights;
    for (int i = 0; i < 33 * 33; ++i)
        heights += (i ? ",5" : "5");
    terrain->Deserialize({{"HeightSamples", scene::PropertyType::String, heights}});
    render::Material groundMaterial;
    terrain->SetMaterial(&groundMaterial);
    auto *volume = entity->CreateComponent<scene::TerrainVolumeComponent>();
    volume->SetChunkVoxels(8);
    auto *bare = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Bare"}));

    Require(ControlTerrainVolume(nullptr) == 0, "A null request is refused");
    auto noScene = Sphere(TerrainVolumeScriptOperation::Edit, 16, 5, 16, 2);
    Require(ControlTerrainVolume(&noScene) == 0, "Without a scene nothing happens");
    engine.SetScene(&scene);

    // Scene-wide digging.
    auto dig = Sphere(TerrainVolumeScriptOperation::Edit, 16, 5, 16, 2);
    Require(ControlTerrainVolume(&dig) == 1 && dig.result == 1 && dig.volumesChanged == 1 && dig.rockRemoved > 5.0f &&
                dig.mainMaterial == 0 && dig.rockAdded == 0.0f,
            "A scene-wide dig reports the rock it removed");
    // Painted, so it moves no rock for the edits below.
    auto broken = Sphere(TerrainVolumeScriptOperation::Edit, 6, 5, 26, 1.5f);
    broken.debrisPieces = 3;
    Require(ControlTerrainVolume(&broken) == 1 && broken.result == 1 && broken.debrisSpawned > 0 && broken.debrisSpawned <= 3,
            "A dig asking for debris breaks its rock into pieces");
    auto big = Sphere(TerrainVolumeScriptOperation::Edit, 16, 5, 16, 20, 2);
    big.material = 1;
    Require(ControlTerrainVolume(&big) == 1 && big.result == -1, "The default budget refuses a huge shape");
    big.maxExtent = 0.0f;
    Require(ControlTerrainVolume(&big) == 1 && big.result == 1, "A budget of 0 accepts it");

    // One terrain, a box and a capsule.
    auto box = Sphere(TerrainVolumeScriptOperation::Edit, 4, 5, 4, 0, 1);
    box.entity = entity->GetID();
    box.shape = static_cast<std::int32_t>(TerrainVolumeScriptShape::Box);
    box.halfExtents[0] = box.halfExtents[1] = box.halfExtents[2] = 1.0f;
    box.rotation[3] = 2.0f; // not normalised: the API normalises it
    box.material = 3;
    Require(ControlTerrainVolume(&box) == 1 && box.result == 1 && box.rockAdded > 1.0f && box.mainMaterial == -1, "Filling a box on one terrain");
    auto capsule = Sphere(TerrainVolumeScriptOperation::Edit, 28, 5, 4, 1);
    capsule.shape = static_cast<std::int32_t>(TerrainVolumeScriptShape::Capsule);
    capsule.b[0] = 28;
    capsule.b[1] = 5;
    capsule.b[2] = 10;
    Require(ControlTerrainVolume(&capsule) == 1 && capsule.result == 1 && capsule.rockRemoved > 1.0f, "A tunnel");

    auto badMode = Sphere(TerrainVolumeScriptOperation::Edit, 16, 5, 16, 1, 9);
    Require(ControlTerrainVolume(&badMode) == 0, "An unknown mode is malformed");
    auto badShape = Sphere(TerrainVolumeScriptOperation::Edit, 16, 5, 16, 1);
    badShape.shape = 9;
    Require(ControlTerrainVolume(&badShape) == 0, "An unknown shape is malformed");
    auto bareEdit = Sphere(TerrainVolumeScriptOperation::Edit, 16, 5, 16, 1);
    bareEdit.entity = bare->GetID();
    Require(ControlTerrainVolume(&bareEdit) == 0, "An entity without a volume is refused");
    auto unknown = Operation(static_cast<TerrainVolumeScriptOperation>(99));
    Require(ControlTerrainVolume(&unknown) == 0, "An unknown operation is refused");

    // State.
    auto upToDate = Operation(TerrainVolumeScriptOperation::IsUpToDate, entity->GetID());
    Require(ControlTerrainVolume(&upToDate) == 1 && upToDate.result == 0, "Edits are pending");
    auto flush = Operation(TerrainVolumeScriptOperation::Flush);
    Require(ControlTerrainVolume(&flush) == 1 && ControlTerrainVolume(&upToDate) == 1 && upToDate.result == 1, "Flushing every volume lands them");
    auto chunks = Operation(TerrainVolumeScriptOperation::ChunkCount, entity->GetID());
    Require(ControlTerrainVolume(&chunks) == 1 && chunks.result == static_cast<std::int32_t>(volume->GetChunkEntityCount()) && chunks.result > 0,
            "Chunk count");
    auto save = Operation(TerrainVolumeScriptOperation::Save, entity->GetID());
    Require(ControlTerrainVolume(&save) == 1 && save.result == 0, "Saving without a volume file fails");

    // Save games: the size, then the copy; restore and clear.
    const auto stored = volume->GetVolume()->GetStoredChunks();
    auto size = Operation(TerrainVolumeScriptOperation::CaptureEdits);
    Require(ControlTerrainVolume(&size) == 1 && size.result > 12, "Capturing reports the size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.result));
    auto copy = Operation(TerrainVolumeScriptOperation::CaptureEdits);
    copy.buffer = reinterpret_cast<std::uintptr_t>(bytes.data());
    copy.bufferSize = static_cast<std::int64_t>(bytes.size());
    Require(ControlTerrainVolume(&copy) == 1 && copy.result == size.result && bytes[0] == 'P' && bytes[3] == 'A', "Capturing copies the archive");

    auto clear = Operation(TerrainVolumeScriptOperation::ClearEdits);
    Require(ControlTerrainVolume(&clear) == 1 && clear.result == 1 && volume->GetVolume()->GetStoredChunkCount() == 0, "Clearing goes back to the file");
    auto restore = Operation(TerrainVolumeScriptOperation::RestoreEdits);
    restore.buffer = copy.buffer;
    restore.bufferSize = copy.bufferSize;
    Require(ControlTerrainVolume(&restore) == 1 && restore.result == 1 && volume->GetVolume()->GetStoredChunks() == stored, "Restoring");
    bytes.resize(bytes.size() / 2);
    restore.bufferSize = static_cast<std::int64_t>(bytes.size());
    Require(ControlTerrainVolume(&restore) == 1 && restore.result == 0 && volume->GetVolume()->GetStoredChunks() == stored,
            "A damaged save is refused and changes nothing");

    std::cout << "Terrain volume script API tests passed.\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "FAILED: " << error.what() << "\n";
    return 1;
}
