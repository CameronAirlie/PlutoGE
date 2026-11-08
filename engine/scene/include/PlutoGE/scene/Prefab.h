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

        // Resolves property variants, constructing only mesh/collider geometry.
        // The detached scene is never updated or inserted in the active scene.
        static std::unique_ptr<Scene> LoadGeometryPreview(std::string_view prefabReference,
                                                        std::string *errorMessage = nullptr);

        static PrefabPreloadResult Preload(std::string_view prefabReference);
        static bool IsReady(std::string_view prefabReference);
        static PrefabInstantiationProfile GetLatestInstantiationProfile();
        static PrefabInstantiationProfile GetMaximumInstantiationProfile();
        static void ResetInstantiationProfiles();

        static bool SaveVariant(const Entity &instanceRoot, const std::filesystem::path &filePath,
                                std::string *errorMessage = nullptr);
        static std::string GetVariantBase(std::string_view reference);
        static bool RevertInstance(Entity &instanceRoot, std::string *errorMessage = nullptr);
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
        static bool UpdateInstance(Entity &instanceRoot, std::string *errorMessage = nullptr,
                                   bool preserveAddedChildren = true);
        static int UpdateInstances(Scene &scene,
                                   std::string_view prefabReference = {},
                                   std::string *errorMessage = nullptr);
        static bool ApplyInstanceToPrefab(const Entity &instanceRoot, std::string *errorMessage = nullptr);

        // Content fingerprint of a prefab and its variant bases; 0 when it cannot be read.
        [[nodiscard]] static uint64_t GetFingerprint(std::string_view prefabReference, std::string *errorMessage = nullptr);
        struct InstanceSyncResult
        {
            int updated = 0;   // Instances rebuilt from a changed prefab (overrides and added children kept).
            int adopted = 0;   // Instances without a fingerprint that now record the current one.
            std::vector<std::string> errors; // One line per instance that could not be updated.
        };
        // Brings every prefab instance root in `scene` up to date with its prefab: instances whose prefab changed
        // since they were built are updated; instances from scenes saved before fingerprints adopt the current one
        // without changing (use UpdateInstance for those). Never runs while the runtime is started.
        static InstanceSyncResult SyncInstances(Scene &scene);
        // The steps of SyncInstances, for hosts that record the update as an undoable edit.
        static int AdoptFingerprints(Scene &scene);
        [[nodiscard]] static std::vector<EntityID> FindOutdatedInstances(Scene &scene);
        static InstanceSyncResult UpdateOutdatedInstances(Scene &scene, const std::vector<EntityID> &instanceRoots);
    };
}
