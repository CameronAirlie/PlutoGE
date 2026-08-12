#pragma once

#include "PlutoGE/scene/Entity.h"

#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace PlutoGE::scene
{
    class Scene;

    struct PrefabPreloadResult
    {
        bool ready = false;
        bool cacheHit = false;
        double durationMs = 0.0;
        std::string resolvedPath;
        std::string error;
    };

    struct PrefabInstantiationProfile
    {
        std::string prefabPath;
        std::string rootEntityName;
        EntityID rootEntityId = 0;
        double totalMs = 0.0;
        double fileResolutionMs = 0.0;
        double parsingMs = 0.0;
        double hierarchyAllocationMs = 0.0;
        double componentConstructionMs = 0.0;
        double componentDeserializationMs = 0.0;
        double referenceRemappingMs = 0.0;
        bool parsedPrefabCacheMiss = false;
        std::uint32_t synchronousLoadCount = 0;
        std::uint32_t deferredWorkCount = 0;
        std::unordered_map<std::string, double> componentTypeTotalsMs;
        std::string slowestComponentType;
        double slowestComponentMs = 0.0;
    };

    class Prefab
    {
    public:
        static constexpr std::string_view kFileExtension = ".plutoprefab";

        static PrefabPreloadResult Preload(std::string_view prefabReference);
        static bool IsReady(std::string_view prefabReference);
        static PrefabInstantiationProfile GetLatestInstantiationProfile();
        static PrefabInstantiationProfile GetMaximumInstantiationProfile();
        static void ResetInstantiationProfiles();

        static bool SaveFromEntity(const Entity &entity,
                                   const std::filesystem::path &filePath,
                                   std::string *errorMessage = nullptr);
        static Entity *Instantiate(Scene &scene,
                                   std::string prefabReference,
                                   Entity *parent = nullptr,
                                   std::string *errorMessage = nullptr);
        static Entity *DuplicateEntity(Scene &scene,
                                       const Entity &source,
                                       Entity *parent = nullptr,
                                       bool preservePrefabLink = true);
        static bool UpdateInstance(Entity &instanceRoot, std::string *errorMessage = nullptr);
        static int UpdateInstances(Scene &scene,
                                   std::string_view prefabReference = {},
                                   std::string *errorMessage = nullptr);
        static bool ApplyInstanceToPrefab(const Entity &instanceRoot, std::string *errorMessage = nullptr);
    };
}
