#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <array>
#include <cstdint>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    enum class ParticleSimulationSpace
    {
        Local = 0,
        World = 1,
    };

    enum class ParticleShape
    {
        Point = 0,
        Sphere = 1,
        Box = 2,
        Cone = 3,
    };

    struct ParticleGpuData
    {
        glm::vec4 positionAge{0.0f, 0.0f, 0.0f, 999999.0f};
        glm::vec4 velocityLifetime{0.0f, 0.0f, 0.0f, 1.0f};
        glm::vec4 colorSize{1.0f};
        glm::vec4 seed{0.0f};
    };

    class ParticleSystemComponent : public TypedComponent<ParticleSystemComponent>
    {
    public:
        ParticleSystemComponent() = default;
        ~ParticleSystemComponent() override;

        void Update(float deltaTime) override;
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void Play();
        void Pause();
        void Stop(bool clear = true);
        void Clear();
        void Emit(int count);

        bool IsPlaying() const { return m_playing; }
        int GetParticleCount() const { return m_particleCountEstimate; }
        float ConsumePendingDeltaTime();
        int ConsumePendingEmitCount();
        bool ConsumeClearRequested();

        void EnsureGpuResources();
        void ReleaseGpuResources();
        bool HasGpuResources() const { return m_gpuInitialized; }
        GLuint GetReadVao() const { return m_vaos[static_cast<std::size_t>(m_readBufferIndex)]; }
        GLuint GetReadBuffer() const { return m_buffers[static_cast<std::size_t>(m_readBufferIndex)]; }
        GLuint GetWriteBuffer() const { return m_buffers[static_cast<std::size_t>(1 - m_readBufferIndex)]; }
        int GetGpuCapacity() const { return m_gpuCapacity; }
        void SwapGpuBuffers() { m_readBufferIndex = 1 - m_readBufferIndex; }
        void MarkGpuStateDirty() { m_gpuStateDirty = true; }
        bool ConsumeGpuStateDirty();

        bool GetLooping() const { return m_looping; }
        void SetLooping(bool looping) { m_looping = looping; }
        bool GetPlayOnAwake() const { return m_playOnAwake; }
        void SetPlayOnAwake(bool playOnAwake) { m_playOnAwake = playOnAwake; }
        float GetDuration() const { return m_duration; }
        void SetDuration(float duration);
        int GetMaxParticles() const { return m_maxParticles; }
        void SetMaxParticles(int maxParticles);
        float GetStartLifetime() const { return m_startLifetime; }
        void SetStartLifetime(float lifetime);
        float GetStartSpeed() const { return m_startSpeed; }
        void SetStartSpeed(float speed);
        float GetStartSize() const { return m_startSize; }
        void SetStartSize(float size);
        const glm::vec4 &GetStartColor() const { return m_startColor; }
        void SetStartColor(const glm::vec4 &color) { m_startColor = glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f)); }
        float GetGravityModifier() const { return m_gravityModifier; }
        void SetGravityModifier(float gravityModifier) { m_gravityModifier = gravityModifier; }
        float GetEmissionRateOverTime() const { return m_emissionRateOverTime; }
        void SetEmissionRateOverTime(float rate);
        float GetBurstTime() const { return m_burstTime; }
        void SetBurstTime(float time);
        int GetBurstCount() const { return m_burstCount; }
        void SetBurstCount(int count);
        ParticleSimulationSpace GetSimulationSpace() const { return m_simulationSpace; }
        void SetSimulationSpace(ParticleSimulationSpace simulationSpace) { m_simulationSpace = simulationSpace; }
        ParticleShape GetShape() const { return m_shape; }
        void SetShape(ParticleShape shape) { m_shape = shape; }
        const glm::vec3 &GetShapeSize() const { return m_shapeSize; }
        void SetShapeSize(const glm::vec3 &size) { m_shapeSize = glm::max(size, glm::vec3(0.0f)); }
        float GetShapeRadius() const { return m_shapeRadius; }
        void SetShapeRadius(float radius);
        float GetConeAngle() const { return m_coneAngle; }
        void SetConeAngle(float angle);
        const std::string &GetMaterialAssetReference() const { return m_materialAssetReference; }
        void SetMaterialAssetReference(const std::string &materialAssetReference) { m_materialAssetReference = materialAssetReference; }

    private:
        void ResetPlaybackState(bool clearParticles);
        void ConfigureParticleAttributes(GLuint vao, GLuint buffer) const;

        bool m_playing = false;
        bool m_paused = false;
        bool m_playOnAwake = true;
        bool m_looping = true;
        bool m_burstFiredThisCycle = false;
        float m_duration = 5.0f;
        float m_time = 0.0f;
        float m_pendingDeltaTime = 0.0f;
        float m_emissionAccumulator = 0.0f;
        int m_pendingEmitCount = 0;
        int m_particleCountEstimate = 0;
        bool m_clearRequested = true;

        int m_maxParticles = 1000;
        float m_startLifetime = 5.0f;
        float m_startSpeed = 2.0f;
        float m_startSize = 0.25f;
        glm::vec4 m_startColor{1.0f};
        float m_gravityModifier = 0.0f;
        float m_emissionRateOverTime = 10.0f;
        float m_burstTime = 0.0f;
        int m_burstCount = 0;
        ParticleSimulationSpace m_simulationSpace = ParticleSimulationSpace::Local;
        ParticleShape m_shape = ParticleShape::Point;
        glm::vec3 m_shapeSize{1.0f};
        float m_shapeRadius = 1.0f;
        float m_coneAngle = 25.0f;
        std::string m_materialAssetReference;

        bool m_gpuInitialized = false;
        bool m_gpuStateDirty = true;
        int m_gpuCapacity = 0;
        int m_readBufferIndex = 0;
        std::array<GLuint, 2> m_buffers{0, 0};
        std::array<GLuint, 2> m_vaos{0, 0};
    };
}
