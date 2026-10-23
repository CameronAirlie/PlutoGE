#pragma once

#include <cstdint>

#ifdef _WIN32
#define PLUTO_TERRAIN_API_CALL __cdecl
#else
#define PLUTO_TERRAIN_API_CALL
#endif

namespace PlutoGE::scripting
{
    // The native side of the managed TerrainComponent (ScriptBridge.Terrain.cs, RegisterTerrainApi): one entry point
    // taking a request whose layout both sides check by size. Inputs are entity, operation, layer, position and the
    // brush; outputs are result, height, normal, weights and name.
    enum class TerrainScriptOperation : std::int32_t
    {
        SampleSurface = 0, // result 1 on the terrain; height, normal (world)
        LayerCount = 1,    // result = layers
        LayerName = 2,     // name of `layer`; result 1 when it exists
        LayerWeights = 3,  // weights[0..7] at position; result 1 on the terrain
        DominantLayer = 4, // result = the strongest layer at position, -1 off the terrain or without layers
        PaintLayer = 5,    // one dab of `layer` (radius, opacity, hardness) at position; result 1 when it changed
    };

    struct NativeTerrainRequest
    {
        std::int32_t operation = 0;
        std::uint32_t entity = 0;
        std::int32_t layer = 0;
        float radius = 0.0f, opacity = 0.0f, hardness = 0.0f;
        float position[3]{};
        std::int32_t result = 0;
        float height = 0.0f;
        float normal[3]{};
        float weights[8]{};
        char name[128]{};
    };
    static_assert(sizeof(NativeTerrainRequest) == 216, "Keep in step with ScriptBridge.Terrain.cs");

    // Returns 0 when the entity has no terrain or the request is malformed.
    std::int32_t PLUTO_TERRAIN_API_CALL ControlTerrain(NativeTerrainRequest *request);
}
