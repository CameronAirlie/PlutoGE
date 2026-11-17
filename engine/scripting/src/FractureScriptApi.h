#pragma once

#include <cstdint>

#ifdef _WIN32
#define PLUTO_FRACTURE_API_CALL __cdecl
#else
#define PLUTO_FRACTURE_API_CALL
#endif

namespace PlutoGE::scripting
{
    // The native side of the managed Fracture API (ScriptBridge.Fracture.cs, RegisterFractureApi): scene::Shatter with
    // the settings of PlutoGE.ScriptCore.ShatterSettings. Optional: hosts without it leave Fracture.IsSupported false.
    struct NativeShatterRequest
    {
        std::uint32_t root = 0;
        std::int32_t pieces = 8;
        std::uint32_t seed = 0;
        std::int32_t hasImpact = 0;
        float impactPoint[3]{};
        float impactDirection[3]{};
        float impactBias = 0.5f;
        float density = 2600.0f;
        float burstSpeed = 2.5f;
        float impactSpeed = 1.5f;
        float spin = 4.0f;
        float minPieceSize = 0.04f;
    };
    static_assert(sizeof(NativeShatterRequest) == 64, "Keep in step with ScriptBridge.Fracture.cs");

    // Shatters the root's subtree in the active scene and writes the pieces' entity IDs. `capacity` must hold
    // scene::kMaxShatterPieces (one shatter never spawns more), so nothing is ever spawned that the caller cannot see;
    // with less, or without a scene or root, nothing happens. Returns how many pieces were spawned.
    std::int32_t PLUTO_FRACTURE_API_CALL ShatterEntity(const NativeShatterRequest *request, const char *tag,
                                                       std::uint32_t *pieces, std::int32_t capacity);
}
