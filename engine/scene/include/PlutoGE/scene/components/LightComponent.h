#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/render/LocalLight.h"
#include "PlutoGE/render/ShadowMethod.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace PlutoGE::scene
{

    // Serialized by value: append new types only.
    enum class LightType
    {
        Point,
        Directional,
        Spot,
        // A one-sided rectangle (sourceWidth x sourceHeight) facing `direction`, shaded with linearly transformed
        // cosines; it casts no shadow maps, only contact shadows.
        Rect
    };

    inline constexpr int kMaxDirectionalShadowCascades = 4;
    inline constexpr int kDefaultDirectionalShadowCascades = 4;

    struct DirectionalShadowSettings
    {
        render::ShadowMethod method = render::ShadowMethod::Virtual;
        int virtualPoolPages = 256;
        int virtualSpotResolution = 2048;
        int virtualMaxPageAge = 8;
        bool virtualAllowOversizedPages = true;
        float virtualCoarseMinCasterTexels = 0.5f;
        bool virtualClusterCulling = true;
        int virtualPageBudget = 64;
        int virtualTriangleBudget = 1000000;
        bool virtualAdaptiveBudget = false;
        int cascadeCount = kDefaultDirectionalShadowCascades;
        int resolution = 2048;
        // 0.5 reduces a 2048 base map to 256 pixels by cascade four.
        float cascadeResolutionFalloff = 0.75f;
        float maxDistance = 150.0f;
        // Maximum distance searched toward the light for occluders. Zero keeps
        // legacy behavior by matching the receiver shadow distance.
        float casterDistance = 0.0f;
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
        // World length of screen-space contact shadows, which catch occluders too close for the shadow map to
        // resolve (feet, props, creases); 0 disables them.
        float contactShadowLength = 0.25f;
    };

    struct Light
    {
        LightType type = LightType::Point; // Type of the light (point, directional, spot)
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 color{1.0f, 1.0f, 1.0f};          // Color of the light (default to white)
        render::SpotCone spotCone;
        float intensity = 1.0f; // Candela for point/spot, illuminance for directional.
        [[nodiscard]] float GetRange() const { return render::LocalLightRange(intensity, color); }
        glm::vec3 direction{0.0f, -1.0f, 0.0f};     // Direction of the light (for directional and spot lights)
        // Emitter shape for point and spot lights, in metres: a sphere of this radius, which softens and widens
        // highlights; for point lights optionally stretched into a tube of `sourceLength` along `direction`.
        // Zero is an ideal point. Shapes change specular only; falloff and shadows stay those of the centre.
        float sourceRadius = 0.0f;
        float sourceLength = 0.0f;
        // Rect lights: the emitter's size in metres along `right` (the transform's x axis) and up (its y axis).
        float sourceWidth = 1.0f;
        float sourceHeight = 1.0f;
        glm::vec3 right{1.0f, 0.0f, 0.0f};
        // Point, spot and rect lights: world length of screen-space contact shadows towards the light; 0 disables
        // them. Directional lights use directionalShadowSettings.contactShadowLength.
        float contactShadowLength = 0.0f;
        DirectionalShadowSettings directionalShadowSettings{};
        bool castsShadows = false;   // Whether the light casts shadows
        bool isStatic = false;       // Static lights only refresh shadow data when dirty
        bool isDirty = true;         // Dirty lights need their shadow data refreshed
    };

    class LightComponent : public TypedComponent<LightComponent>
    {
    public:
        LightComponent() = default;
        ~LightComponent() override;

        void SetLightType(LightType type);
        void SetColor(const glm::vec3 &color);
        void SetIntensity(float intensity);
        void SetSpotCone(float innerAngle, float outerAngle);
        void SetDirection(const glm::vec3 &direction);
        void SetStatic(bool isStatic);
        void SetCastsShadows(bool castsShadows);
        void SetSourceShape(float radius, float length);
        void SetRectSize(float width, float height);
        void SetContactShadowLength(float length);
        void SetDirectionalShadowSettings(const DirectionalShadowSettings &settings);
        void MarkDirty();
        void ClearDirty();
        bool IsDirty() const;

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void Update(float deltaTime) override;
        Light &GetLight() { return m_config; }
        const Light &GetLight() const { return m_config; }

    private:
        Light m_config;
    };
}
