#pragma once

#include <cstdint>

#ifdef _WIN32
#define PLUTO_TERRAIN_VOLUME_API_CALL __cdecl
#else
#define PLUTO_TERRAIN_VOLUME_API_CALL
#endif

namespace PlutoGE::scripting
{
    // The native side of the managed TerrainVolumes API and TerrainVolumeComponent (ScriptBridge.TerrainVolume.cs,
    // RegisterTerrainVolumeApi; optional, so hosts and ScriptCore builds from before it still load). One entry point
    // taking a request whose layout both sides check by size. docs/dev/TERRAIN_VOLUMES_PLAN.md, phase 5.
    enum class TerrainVolumeScriptOperation : std::int32_t
    {
        // A shape edit. Entity 0: every terrain volume under the shape (scene::EditTerrainVolumes: budget, foliage);
        // otherwise that entity's volume only. result 1 changed, 0 not, -1 refused (over the budget); the rock and
        // count outputs are filled.
        Edit = 0,
        Flush = 1,        // entity 0: every volume; result 1
        IsUpToDate = 2,   // the entity's volume; result 1 when every mesh and hole has landed
        Save = 3,         // the entity's volume file; result 1 when written
        CaptureEdits = 4, // the scene's edits (scene::CaptureTerrainVolumeEdits); result = bytes, copied when they fit
        RestoreEdits = 5, // from buffer; result 1 when restored
        ChunkCount = 6,   // result = the entity's chunk entities
        ClearEdits = 7,   // every terrain back to its files; result 1
    };

    enum class TerrainVolumeScriptShape : std::int32_t
    {
        Sphere = 0,  // a (centre), radius
        Capsule = 1, // a to b, radius
        Box = 2,     // a (centre), halfExtents, rotation
    };

    struct NativeTerrainVolumeRequest
    {
        std::int32_t operation = 0;
        std::uint32_t entity = 0;
        std::int32_t shape = 0;
        std::int32_t mode = 0;     // scene::volume::VolumeEditMode
        std::int32_t material = 0; // terrain layer, Add and Paint
        std::int32_t result = 0;
        float radius = 0.0f;
        float maxExtent = -1.0f;   // < 0: the default budget, 0: none
        float a[3]{};
        float b[3]{};
        float halfExtents[3]{};
        float rotation[4]{0.0f, 0.0f, 0.0f, 1.0f}; // x, y, z, w
        float rockRemoved = 0.0f;
        float rockAdded = 0.0f;
        std::int32_t mainMaterial = -1; // the material most of the removed rock had, -1 none
        std::int32_t volumesChanged = 0;
        std::int32_t foliageRemoved = 0;
        std::uint64_t buffer = 0; // address of a byte buffer (CaptureEdits, RestoreEdits)
        std::int64_t bufferSize = 0;
    };
    static_assert(sizeof(NativeTerrainVolumeRequest) == 120, "Keep in step with ScriptBridge.TerrainVolume.cs");

    // Returns 0 when the request is malformed, the scene is missing, or an entity has no terrain volume.
    std::int32_t PLUTO_TERRAIN_VOLUME_API_CALL ControlTerrainVolume(NativeTerrainVolumeRequest *request);
}
