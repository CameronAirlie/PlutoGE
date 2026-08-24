#pragma once

#include "PlutoGE/scene/components/Component.h"
#include <glm/glm.hpp>

namespace PlutoGE::scene
{
    enum class AudioEnvironmentPreset { Custom, Cave, Forest, City, Field, Room, Hall, Underwater };
    enum class AudioEnvironmentShape { Box, Sphere };

    class AudioEnvironmentVolumeComponent final : public TypedComponent<AudioEnvironmentVolumeComponent>
    {
    public:
        void Update(float) override {}
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void SetPreset(AudioEnvironmentPreset preset);
        [[nodiscard]] float InfluenceAt(const glm::vec3 &worldPosition) const;
        [[nodiscard]] AudioEnvironmentPreset GetPreset() const { return m_preset; }
        [[nodiscard]] AudioEnvironmentShape GetShape() const { return m_shape; }
        [[nodiscard]] const glm::vec3 &GetSize() const { return m_size; }
        [[nodiscard]] float GetRadius() const { return m_radius; }
        void SetSize(const glm::vec3 &size);
        void SetRadius(float radius);
        [[nodiscard]] int GetPriority() const { return m_priority; }
        [[nodiscard]] float GetReverbWet() const { return m_reverbWet; }
        [[nodiscard]] float GetReverbDecay() const { return m_reverbDecay; }
        [[nodiscard]] float GetReverbDensity() const { return m_reverbDensity; }
        [[nodiscard]] float GetReverbDiffusion() const { return m_reverbDiffusion; }
        [[nodiscard]] float GetEchoWet() const { return m_echoWet; }
        [[nodiscard]] float GetEchoDelay() const { return m_echoDelay; }
        [[nodiscard]] float GetEchoFeedback() const { return m_echoFeedback; }
        [[nodiscard]] float GetLowPass() const { return m_lowPass; }
        [[nodiscard]] float GetGain() const { return m_gain; }

    private:
        AudioEnvironmentPreset m_preset = AudioEnvironmentPreset::Custom;
        AudioEnvironmentShape m_shape = AudioEnvironmentShape::Box;
        glm::vec3 m_size{10.0f};
        float m_radius = 5.0f;
        float m_blendDistance = 2.0f;
        int m_priority = 0;
        float m_reverbWet = 0.0f, m_reverbDecay = 1.0f, m_reverbDensity = 1.0f, m_reverbDiffusion = 1.0f;
        float m_echoWet = 0.0f, m_echoDelay = 0.15f, m_echoFeedback = 0.25f;
        float m_lowPass = 0.0f, m_gain = 1.0f;
    };
}
