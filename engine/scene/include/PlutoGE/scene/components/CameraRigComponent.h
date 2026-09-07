#pragma once
#include "PlutoGE/scene/components/Component.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace PlutoGE::scene
{
    enum class CameraRigMode { Follow, Orbit };
    struct CameraRigSettings
    {
        std::uint32_t target = 0;
        CameraRigMode mode = CameraRigMode::Follow;
        glm::vec3 offset{0, 2, 6};
        glm::vec3 focusOffset{0, 1, 0};
        bool followHeading = true;
        float yaw = 0, pitch = 15, distance = 6;
        float smoothing = 0.15f;
        float collisionRadius = 0.25f; // Zero disables avoidance.
        float collisionPadding = 0.05f;
    };

    class CameraRigComponent : public TypedComponent<CameraRigComponent>
    {
    public:
        void Update(float) override {} // Driven by Scene's late camera phase.
        const CameraRigSettings &GetSettings() const { return m_settings; }
        bool SetSettings(CameraRigSettings settings);
        bool BlendTo(std::uint32_t target, float seconds);
        bool Shake(float amplitude, float seconds, float frequency = 20);
        void ResetRuntime();
        // Scene invokes this after physics presentation and script LateUpdate,
        // only during runtime. Explicit entry point also permits headless tests.
        void UpdateRig(float deltaTime);
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;
    private:
        CameraRigSettings m_settings;
        bool m_initialized = false;
        glm::vec3 m_position{0}, m_focus{0}, m_blendPosition{0}, m_blendFocus{0};
        double m_blendTime = 0, m_shakeTime = 0;
        float m_blendDuration = 0, m_shakeDuration = 0, m_shakeAmplitude = 0, m_shakeFrequency = 20;
    };
}
