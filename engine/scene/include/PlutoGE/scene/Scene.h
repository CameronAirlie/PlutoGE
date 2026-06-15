#pragma once

#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_set>
#include <vector>
#include <string>

namespace PlutoGE::render
{
    class Texture;
}

namespace PlutoGE::scene
{
    class Entity;
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

    class Scene
    {
    public:
        Scene() = default;
        ~Scene();

        Entity *AddEntity(std::unique_ptr<Entity> entity, Entity *parent = nullptr);
        void RemoveEntity(Entity *entity);
        std::vector<Entity *> GetRootEntities() const { return m_rootEntities; }

        void StartRuntime();
        void StopRuntime();
        void Update(float deltaTime);
        [[nodiscard]] bool IsRuntimeStarted() const { return m_runtimeStarted; }

        Entity *FindEntityByName(const std::string &name) const;               // Utility function to find an entity by name (can be useful for scripting and editor)
        Entity *FindEntityByID(EntityID id) const;                             // Utility function to find an entity by its unique ID (useful for serialization and referencing)
        std::vector<Entity *> FindEntitiesByTag(const std::string &tag) const; // Utility function to find entities by tag (can be useful for scripting and editor)

        std::vector<Light *> GetLights() const; // Get active lights in the scene (for rendering)
        void MarkShadowLightsDirty();
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
        const std::vector<IblCaptureVolume> &GetIblCaptureVolumes() const { return m_iblCaptureVolumes; }
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

    private:
        std::string m_name;
        std::vector<std::unique_ptr<Entity>> m_entityStorage;
        std::vector<Entity *> m_rootEntities;
        std::vector<Light *> m_lights;
        std::string m_filePath;
        std::string m_environmentMapPath;
        render::Texture *m_environmentMapTexture = nullptr;
        float m_environmentIntensity = 1.0f;
        bool m_runtimeStarted = false;
        BakedProbeVolume m_bakedProbeVolume;
        std::unique_ptr<render::Texture> m_bakedProbeTexture;
        std::vector<IblCaptureVolume> m_iblCaptureVolumes;
        std::unordered_set<uint64_t> m_activeCollisionPairs;
        void CollectEntitySubtree(Entity *entity, std::vector<Entity *> &entities) const;
        bool RemoveEntityRecursive(Entity *current, Entity *target);
        void RebuildBakedProbeTexture();
        void StepPhysics(float deltaTime);
        void DispatchCollisionEvents(std::unordered_set<uint64_t> currentCollisionPairs);
    };
}
