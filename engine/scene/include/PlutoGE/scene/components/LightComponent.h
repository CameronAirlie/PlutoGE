#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/render/Texture.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>

#include <memory>

namespace PlutoGE::scene
{

    enum class LightType
    {
        Point,
        Directional,
        Spot
    };

    inline constexpr int kMaxDirectionalShadowCascades = 4;
    inline constexpr int kDefaultDirectionalShadowCascades = 4;

    struct DirectionalShadowSettings
    {
        int cascadeCount = kDefaultDirectionalShadowCascades;
        int resolution = 2048;
        // 0.5 reduces a 2048 base map to 256 pixels by cascade four.
        float cascadeResolutionFalloff = 0.75f;
        float maxDistance = 150.0f;
        float nearCascadeDistance = 8.0f;
        float splitLambda = 0.9f;
        float cascadeBlendDistance = 4.0f;
        float softness = 1.5f;
        float minCasterTexelRadius = 0.0f;
        bool screenSpaceFilterEnabled = true;
        float screenSpaceFilterRenderScale = 0.5f;
        int screenSpaceFilterRadius = 2;
        float screenSpaceFilterDepthScale = 0.015f;
        float screenSpaceFilterMinDepthScale = 0.05f;
        float screenSpaceFilterNormalThreshold = 0.72f;
        float screenSpaceFilterNormalSoftness = 0.26f;
    };

    struct Light
    {
        LightType type = LightType::Point; // Type of the light (point, directional, spot)
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 color{1.0f, 1.0f, 1.0f};          // Color of the light (default to white)
        float intensity = 1.0f;                     // Intensity of the light (default to 1.0)
        float range = 10.0f;                        // Range of the light (for point and spot lights)
        glm::vec3 direction{0.0f, -1.0f, 0.0f};     // Direction of the light (for directional and spot lights)
        std::unique_ptr<render::Texture> shadowMap; // Owned shadow map texture (if any)
        glm::mat4 shadowMatrix{1.0f};               // Light-space matrix for projected shadow maps
        std::array<std::unique_ptr<render::Texture>, kMaxDirectionalShadowCascades> shadowCascadeMaps;
        // Static depth is cached separately, then copied into shadowCascadeMaps
        // before dynamic casters are overlaid. Consumers continue sampling the
        // combined maps through the existing public interface.
        std::array<std::unique_ptr<render::Texture>, kMaxDirectionalShadowCascades> staticShadowCascadeMaps;
        // Persistent ping-pong targets let scrolling cascades promote the
        // shifted texture to the static cache without copying the entire map
        // back into its previous allocation.
        std::array<std::array<std::unique_ptr<render::Texture>, 2>, kMaxDirectionalShadowCascades> staticShadowCascadeScratchMaps;
        std::array<std::uint8_t, kMaxDirectionalShadowCascades> nextStaticShadowScratchIndex{};
        std::array<bool, kMaxDirectionalShadowCascades> staticShadowCascadeValid{false, false, false, false};
        std::array<glm::vec3, kMaxDirectionalShadowCascades> shadowCascadeWorldOrigins{
            glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f)};
        std::array<glm::mat4, kMaxDirectionalShadowCascades> shadowCascadeMatrices{
            glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
        std::array<float, kMaxDirectionalShadowCascades> shadowCascadeSplits{0.0f, 0.0f, 0.0f, 0.0f};
        std::array<float, kMaxDirectionalShadowCascades> shadowCascadeWorldTexelSizes{0.0f, 0.0f, 0.0f, 0.0f};
        std::array<float, kMaxDirectionalShadowCascades> shadowCascadeDepthRanges{1.0f, 1.0f, 1.0f, 1.0f};
        DirectionalShadowSettings directionalShadowSettings{};
        int activeShadowCascadeCount = 0;
        float shadowFarPlane = 0.0f; // Far plane used when sampling point-light shadows
        bool castsShadows = false;   // Whether the light casts shadows
        bool isStatic = false;       // Static lights only refresh shadow data when dirty
        bool isDirty = true;         // Dirty lights need their shadow data refreshed
        bool shadowRefreshPending = false;
        std::uint8_t pendingPointShadowFaceMask = 0;
        std::uint8_t pendingShadowCascadeMask = 0;
        int nextShadowCascadeToRefresh = 0;
    };

    class LightComponent : public TypedComponent<LightComponent>
    {
    public:
        LightComponent() = default;
        ~LightComponent() override;

        void SetLightType(LightType type);
        void SetColor(const glm::vec3 &color);
        void SetIntensity(float intensity);
        void SetRange(float range);
        void SetDirection(const glm::vec3 &direction);
        void SetStatic(bool isStatic);
        void SetCastsShadows(bool castsShadows);
        void SetDirectionalShadowSettings(const DirectionalShadowSettings &settings);
        void SetShadowMap(render::Texture *shadowMap);
        void MarkDirty();
        void ClearDirty();
        bool IsDirty() const;

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void Initialize() override;
        void Update(float deltaTime) override;
        Light &GetLight() { return m_config; }
        const Light &GetLight() const { return m_config; }

    private:
        Light m_config;
    };
}
