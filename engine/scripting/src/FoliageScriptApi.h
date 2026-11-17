#pragma once

#include <cstdint>

#ifdef _WIN32
#define PLUTO_FOLIAGE_API_CALL __cdecl
#else
#define PLUTO_FOLIAGE_API_CALL
#endif

namespace PlutoGE::scripting
{
    // The native side of the managed FoliageComponent, FoliageInstance and FoliageProxyComponent
    // (ScriptBridge.Foliage.cs, RegisterFoliageApi): one entry point taking a request whose layout both sides check by
    // size. Inputs are entity, operation, instance ID, position, radius and the ID or match buffer (plus a tag in name
    // and a proxy filter in hasProxyPrefab for FindInScene); the rest are outputs.
    enum class FoliageScriptOperation : std::int32_t
    {
        // entity (foliage), instanceId -> result 1 when it exists: typeIndex, world position, rotation and scale,
        // hasProxyPrefab, name (the type's name) and boundsMin/boundsMax (the world box round what it draws).
        GetInstance = 0,
        // entity (foliage), instanceId -> result 1 with linkedEntity = the proxy spawned in its place, otherwise result 0
        // with the reason in name.
        Promote = 1,
        // entity (proxy) -> result 1 when its instance went back into the foliage. The proxy is destroyed either way.
        Release = 2,
        // entity (foliage), position, radius, ids/capacity -> result = how many instances have their pivot within
        // radius, nearest first; the first `capacity` of their IDs are written to ids.
        FindInRadius = 3,
        // entity (proxy) -> result 1 when it stands for a foliage instance: linkedEntity (the foliage), instanceId.
        GetProxyLink = 4,
        // No entity: the whole scene (scene/FoliageQueries.h). position, radius, name (interaction tag; empty for any),
        // hasProxyPrefab (1: only types with a proxy), capacity, matches -> result = how many instances match, nearest
        // first; the first `capacity` are written to matches.
        FindInScene = 5,
    };

    struct NativeFoliageMatch
    {
        std::uint32_t foliageEntity = 0;
        std::int32_t typeIndex = -1;
        std::uint64_t instanceId = 0;
        float distance = 0.0f;
        std::uint32_t reserved = 0;
    };
    static_assert(sizeof(NativeFoliageMatch) == 24, "Keep in step with ScriptBridge.Foliage.cs");

    struct NativeFoliageRequest
    {
        std::int32_t operation = 0;
        std::uint32_t entity = 0;
        std::uint64_t instanceId = 0;
        float position[3]{};
        float radius = 0.0f;
        float rotation[4]{0.0f, 0.0f, 0.0f, 1.0f}; // x, y, z, w
        float scale[3]{};
        std::int32_t result = 0;
        std::int32_t typeIndex = -1;
        std::uint32_t linkedEntity = 0;
        std::int32_t capacity = 0;
        std::int32_t hasProxyPrefab = 0;
        std::uint64_t *ids = nullptr;
        char name[256]{};
        NativeFoliageMatch *matches = nullptr;
        float boundsMin[3]{};
        float boundsMax[3]{};
    };
    static_assert(sizeof(NativeFoliageRequest) == 376, "Keep in step with ScriptBridge.Foliage.cs");

    // Returns 0 when the entity lacks the component the operation needs or the request is malformed.
    std::int32_t PLUTO_FOLIAGE_API_CALL ControlFoliage(NativeFoliageRequest *request);
}
