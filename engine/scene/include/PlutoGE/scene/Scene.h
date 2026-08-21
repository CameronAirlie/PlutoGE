#pragma once

#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace PlutoGE::render
{
    class Material;
    class Texture;
}

namespace PlutoGE::scene
{
    class UISystem;
    class NavigationSystem;
    class Entity;
    class FoliageComponent;
    class DecalComponent;
    class MeshComponent;
    class CanvasComponent;
    class RmlWidgetComponent;
    class ParticleSystemComponent;
    class TerrainComponent;
    struct Light;

    using EntityID = uint32_t;
    constexpr int kMaxIblCaptureVolumes = 4;

    struct BakedProbeVolume
    {
        glm::vec3 origin{0.0f};
        glm::vec3 size{1.0f};
        glm::ivec3 resolution{0};
        std::vector<glm::vec3> irradiance;

        [[nodiscard]] bool IsValid() const
        {
            return resolution.x > 0 && resolution.y > 0 && resolution.z > 0 &&
                   irradiance.size() == static_cast<std::size_t>(resolution.x * resolution.y * resolution.z);
        }
    };

    struct IblCaptureVolume
    {
        glm::vec3 origin{0.0f};
        glm::vec3 size{1.0f};
        std::string environmentMapPath;
        render::Texture *environmentMapTexture = nullptr;
        float intensity = 1.0f;
        float blendDistance = 1.0f;

        [[nodiscard]] bool IsValid() const
        {
            return environmentMapTexture != nullptr && size.x > 0.0f && size.y > 0.0f && size.z > 0.0f;
        }
    };

    struct PhysicsRaycastHit
    {
        EntityID entityId = 0;
        glm::vec3 point{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        float distance = 0.0f;
        std::uint64_t foliageInstanceId = 0;
    };

    struct PhysicsRaycastRequest
    {
        glm::vec3 origin{0.0f};
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        float maxDistance = 0.0f;
    };

    struct SceneUpdateTimingStats
    {
        struct ComponentTiming
        {
            std::string name;
            float totalMs = 0.0f;
            float maxInstanceMs = 0.0f;
            uint32_t callCount = 0;
            EntityID slowestEntityId = 0;
            std::string slowestEntityName;
        };

        float preparationMs = 0.0f;
        float runtimeUiMs = 0.0f;
        float componentsMs = 0.0f;
        float lateScriptsMs = 0.0f;
        float renderSubmissionMs = 0.0f;
        float meshSubmissionMs = 0.0f;
        float terrainSubmissionMs = 0.0f;
        float foliageSubmissionMs = 0.0f;
        float physicsMs = 0.0f;
        std::vector<ComponentTiming> componentTimings;
        std::vector<ComponentTiming> animationTimings;
        std::vector<ComponentTiming> scriptUpdateTimings;
        std::vector<ComponentTiming> scriptLateUpdateTimings;
    };

    class Scene
    {
    public:
        Scene();
        ~Scene();

        Entity *AddEntity(std::unique_ptr<Entity> entity, Entity *parent = nullptr);
        void RemoveEntity(Entity *entity);
        bool DestroyEntity(EntityID entityId);
        std::size_t RefreshMaterialAsset(const std::string &materialAssetReference, render::Material *material);
        std::size_t RemapMaterialAsset(const std::string &oldMaterialAssetReference,
                                       const std::string &newMaterialAssetReference,
                                       render::Material *material);
        const std::vector<Entity *> &GetRootEntities() const { return m_rootEntities; }

        void StartRuntime();
        void StopRuntime();
        void Update(float deltaTime);
        void SetRuntimeUIInputOverride(const glm::vec2 &canvasSize, const glm::vec2 &mousePosition, bool pointerInside);
        void ClearRuntimeUIInputOverride();
        [[nodiscard]] bool GetRuntimeUIInputOverride(glm::vec2 &canvasSize,
                                                     glm::vec2 &mousePosition,
                                                     bool &pointerInside) const;
        [[nodiscard]] bool IsRuntimeStarted() const { return m_runtimeStarted; }
        void SetTimeScale(float timeScale);
        [[nodiscard]] float GetTimeScale() const { return m_timeScale; }
        [[nodiscard]] const SceneUpdateTimingStats &GetUpdateTimingStats() const { return m_updateTimingStats; }
        void RecordComponentTiming(const char *name, float elapsedMs, const Entity &entity);
        void RecordAnimationTiming(const char *phase, float elapsedMs, const Entity &entity);
        void RecordScriptTiming(const std::string &scriptClass, float elapsedMs, const Entity &entity, bool lateUpdate);
        [[nodiscard]] uint64_t GetUpdateSequence() const { return m_updateSequence; }
        UISystem &GetUISystem();
        const UISystem &GetUISystem() const;
        [[nodiscard]] bool HasNativeRuntimeUI() const;
        [[nodiscard]] bool HasRmlRuntimeUI() const;

        Entity *FindEntityByName(const std::string &name) const; // Utility function to find an entity by name (can be useful for scripting and editor)
        Entity *FindEntityByID(EntityID id) const;               // Utility function to find an entity by its unique ID (useful for serialization and referencing)
        bool ContainsEntity(const Entity *entity) const;
        std::vector<Entity *> FindEntitiesByTag(const std::string &tag) const; // Utility function to find entities by tag (can be useful for scripting and editor)
        bool Raycast(const glm::vec3 &origin,
                     const glm::vec3 &direction,
                     float maxDistance,
                     PhysicsRaycastHit &hit,
                     EntityID ignoredEntityId = 0) const;
        void RaycastBatch(const std::vector<PhysicsRaycastRequest> &requests,
                          EntityID ignoredEntityId,
                          std::vector<PhysicsRaycastHit> &hits,
                          std::vector<uint8_t> &hitResults) const;
        bool RaycastByTag(const glm::vec3 &origin,
                          const glm::vec3 &direction,
                          float maxDistance,
                          const std::string &tag,
                          PhysicsRaycastHit &hit,
                          EntityID ignoredEntityId = 0) const;
        glm::vec3 MoveKinematic(Entity &entity, const glm::vec3 &displacement, float skinWidth = 0.02f) const;
        bool AddRigidbodyForce(EntityID entityId,
                               const glm::vec3 &value,
                               bool impulse = false,
                               std::optional<glm::vec3> worldPosition = std::nullopt);
        bool GetRigidbodyVelocityAtPoint(EntityID entityId,
                                         const glm::vec3 &worldPosition,
                                         glm::vec3 &velocity) const;
        Entity *SpawnDecal(const PhysicsRaycastHit &hit,
                           const std::string &materialAssetReference,
                           const glm::vec2 &size,
                           float depth = 0.1f,
                           float lifetime = 0.0f,
                           float fadeDuration = 0.0f);

        std::vector<Light *> GetLights() const; // Get active lights in the scene (for rendering)
        void MarkShadowLightsDirty();
        void InvalidateFoliagePhysics();
        void SubmitRenderCommands();
        const std::string &GetFilePath() const { return m_filePath; }
        void SetFilePath(const std::string &filePath) { m_filePath = filePath; }
        const std::string &GetEnvironmentMapPath() const { return m_environmentMapPath; }
        render::Texture *GetEnvironmentMapTexture() const { return m_environmentMapTexture; }
        bool HasEnvironmentMap() const { return m_environmentMapTexture != nullptr; }
        float GetEnvironmentIntensity() const { return m_environmentIntensity; }
        void SetEnvironmentMap(render::Texture *texture, const std::string &filePath);
        void ClearEnvironmentMap();
        void SetEnvironmentIntensity(float intensity);
        const BakedProbeVolume &GetBakedProbeVolume() const { return m_bakedProbeVolume; }
        bool HasBakedProbeVolume() const { return m_bakedProbeVolume.IsValid() && m_bakedProbeTexture != nullptr; }
        render::Texture *GetBakedProbeTexture() const { return m_bakedProbeTexture.get(); }
        void SetBakedProbeVolume(BakedProbeVolume bakedProbeVolume);
        void ClearBakedProbeVolume();
        // Clears every baked-lighting reference owned by the scene, including
        // mesh/submesh overrides, terrain and foliage materials, and probes.
        // Returns the number of material lightmaps that were removed.
        std::size_t ClearBakedLighting();
        const std::vector<IblCaptureVolume> &GetIblCaptureVolumes() const { return m_iblCaptureVolumes; }
        const std::vector<ParticleSystemComponent *> &GetParticleSystemComponents() const { return m_particleSystemComponents; }
        const std::vector<CanvasComponent *> &GetCanvasComponents() const { return m_canvasComponents; }
        const std::vector<RmlWidgetComponent *> &GetRmlWidgetComponents() const { return m_rmlWidgetComponents; }
        int AddIblCaptureVolume(IblCaptureVolume captureVolume);
        void SetIblCaptureVolume(std::size_t index, IblCaptureVolume captureVolume);
        void RemoveIblCaptureVolume(std::size_t index);
        void ClearIblCaptureVolumes();

    protected:
        friend class Entity;
        void AddLight(Light *light) { m_lights.push_back(light); }
        void RemoveLight(Light *light)
        {
            auto it = std::find(m_lights.begin(), m_lights.end(), light);
            if (it != m_lights.end())
            {
                m_lights.erase(it);
            }
        }
        void RegisterMeshComponent(MeshComponent *meshComponent);
        void UnregisterMeshComponent(MeshComponent *meshComponent);
        void RegisterTerrainComponent(TerrainComponent *terrainComponent);
        void UnregisterTerrainComponent(TerrainComponent *terrainComponent);
        void RegisterFoliageComponent(FoliageComponent *foliageComponent);
        void UnregisterFoliageComponent(FoliageComponent *foliageComponent);
        void RegisterParticleSystemComponent(ParticleSystemComponent *particleSystemComponent);
        void UnregisterParticleSystemComponent(ParticleSystemComponent *particleSystemComponent);
        void RegisterDecalComponent(DecalComponent *decalComponent);
        void UnregisterDecalComponent(DecalComponent *decalComponent);
        void RegisterCanvasComponent(CanvasComponent *canvasComponent);
        void UnregisterCanvasComponent(CanvasComponent *canvasComponent);
        void RegisterRmlWidgetComponent(RmlWidgetComponent *widgetComponent);
        void UnregisterRmlWidgetComponent(RmlWidgetComponent *widgetComponent);

    private:
        struct PhysicsQueryCache;
        struct RuntimePhysicsState;
        struct PendingRigidbodyForce
        {
            EntityID entityId = 0;
            glm::vec3 value{0.0f};
            std::optional<glm::vec3> worldPosition;
            bool impulse = false;
        };

        std::string m_name;
        std::vector<std::unique_ptr<Entity>> m_entityStorage;
        std::unordered_map<EntityID, Entity *> m_entitiesById;
        std::vector<Entity *> m_rootEntities;
        std::vector<MeshComponent *> m_meshComponents;
        std::vector<TerrainComponent *> m_terrainComponents;
        std::vector<FoliageComponent *> m_foliageComponents;
        std::vector<ParticleSystemComponent *> m_particleSystemComponents;
        std::vector<DecalComponent *> m_decalComponents;
        std::vector<CanvasComponent *> m_canvasComponents;
        std::vector<RmlWidgetComponent *> m_rmlWidgetComponents;
        std::vector<Light *> m_lights;
        std::string m_filePath;
        std::string m_environmentMapPath;
        render::Texture *m_environmentMapTexture = nullptr;
        float m_environmentIntensity = 1.0f;
        bool m_runtimeStarted = false;
        float m_timeScale = 1.0f;
        float m_physicsTimeAccumulator = 0.0f;
        SceneUpdateTimingStats m_updateTimingStats;
        BakedProbeVolume m_bakedProbeVolume;
        std::unique_ptr<render::Texture> m_bakedProbeTexture;
        std::vector<IblCaptureVolume> m_iblCaptureVolumes;
        std::unordered_set<uint64_t> m_activeCollisionPairs;
        std::vector<EntityID> m_pendingDestroyEntities;
        std::unordered_set<EntityID> m_pendingDestroyEntityIds;
        mutable std::unique_ptr<PhysicsQueryCache> m_physicsQueryCache;
        uint64_t m_updateSequence = 0;
        std::unique_ptr<RuntimePhysicsState> m_runtimePhysicsState;
        std::vector<PendingRigidbodyForce> m_pendingRigidbodyForces;
        bool m_inFixedScriptUpdate = false;
        struct RuntimeUIInputOverride
        {
            glm::vec2 canvasSize{0.0f};
            glm::vec2 mousePosition{0.0f};
            bool pointerInside = false;
        };
        std::optional<RuntimeUIInputOverride> m_runtimeUIInputOverride;
        std::unique_ptr<UISystem> m_uiSystem;
        void CollectEntitySubtree(Entity *entity, std::vector<Entity *> &entities) const;
        bool RemoveEntityRecursive(Entity *current, Entity *target);
        void FlushPendingDestroyEntities();
        void RebuildBakedProbeTexture();
        PhysicsQueryCache &GetPhysicsQueryCache() const;
        void RefreshPhysicsQueryCache() const;
        void InvalidatePhysicsQueryCache() const;
        void ResetRuntimePhysicsState();
        void RestoreRuntimePhysicsTransforms();
        void ApplyRuntimePhysicsRenderExtrapolation(float remainderTime);
        void RebuildRuntimePhysicsState(const std::vector<Entity *> &physicsEntities,
                                        const std::vector<Entity *> &activeEntities);
        void SyncPhysicsQueryTransform(const Entity &entity) const;
        void StepPhysics(float deltaTime);
        void DispatchCollisionEvents(std::unordered_set<uint64_t> currentCollisionPairs);
    };
}
