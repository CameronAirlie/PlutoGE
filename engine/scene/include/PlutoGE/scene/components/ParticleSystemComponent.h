#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/assets/ParticleSystemAsset.h"

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

    struct ParticleCpuData
    {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec4 color{1.0f};
        float size = 0.25f;
        float age = 999999.0f;
        float lifetime = 1.0f;
        float seed = 0.0f;
        bool active = false;
        bool deathSubEmitterFired = false;
    };

    struct ParticleTrailPoint
    {
        glm::vec3 position{0.0f};
        glm::vec4 color{1.0f};
        float age = 0.0f;
    };

    struct ParticleTrailRenderSegment
    {
        glm::vec3 start{0.0f};
        glm::vec3 end{0.0f};
        glm::vec4 color{1.0f};
        float width = 0.05f;
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
        void EmitAt(const glm::vec3 &worldPosition, int count);

        bool IsPlaying() const { return m_playing; }
        int GetParticleCount() const { return m_particleCountEstimate; }
        float ConsumePendingDeltaTime();
        int ConsumePendingEmitCount();
        bool ConsumeClearRequested();
        int GetNextEmitIndex() const { return m_nextEmitIndex; }
        int GetNextEmitSequence() const { return m_nextEmitSequence; }
        void AdvanceEmitCursor(int count);

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
        float GetLifetimeVariation() const { return m_lifetimeVariation; }
        void SetStartLifetime(float lifetime);
        float GetStartSpeed() const { return m_startSpeed; }
        float GetSpeedVariation() const { return m_speedVariation; }
        void SetStartSpeed(float speed);
        float GetStartSize() const { return m_startSize; }
        float GetSizeVariation() const { return m_sizeVariation; }
        void SetStartSize(float size);
        const glm::vec4 &GetStartColor() const { return m_startColor; }
        void SetStartColor(const glm::vec4 &color) { m_startColor = glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f)); }
        bool GetColorOverLifetimeEnabled() const { return m_colorOverLifetimeEnabled; }
        const glm::vec4 &GetEndColor() const { return m_endColor; }
        bool GetSizeOverLifetimeEnabled() const { return m_sizeOverLifetimeEnabled; }
        float GetEndSize() const { return m_endSize; }
        float GetGravityModifier() const { return m_gravityModifier; }
        float GetDrag() const { return m_drag; }
        float GetBuoyancy() const { return m_buoyancy; }
        const glm::vec3 &GetWindVelocity() const { return m_windVelocity; }
        float GetTurbulenceStrength() const { return m_turbulenceStrength; }
        float GetTurbulenceFrequency() const { return m_turbulenceFrequency; }
        float GetRotationSpeed() const { return m_rotationSpeed; }
        float GetRotationSpeedVariation() const { return m_rotationSpeedVariation; }
        float GetStartRotation() const { return m_startRotation; }
        float GetStartRotationVariation() const { return m_startRotationVariation; }
        float GetFadeInFraction() const { return m_fadeInFraction; }
        float GetFadeOutFraction() const { return m_fadeOutFraction; }
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
        void SetShape(ParticleShape shape);
        const glm::vec3 &GetShapeSize() const { return m_shapeSize; }
        void SetShapeSize(const glm::vec3 &size);
        float GetShapeRadius() const { return m_shapeRadius; }
        void SetShapeRadius(float radius);
        float GetConeAngle() const { return m_coneAngle; }
        void SetConeAngle(float angle);
        assets::ParticleRenderShape GetRenderShape() const { return m_renderShape; }
        void SetRenderShape(assets::ParticleRenderShape renderShape) { m_renderShape = renderShape; }
        const std::string &GetMaterialAssetReference() const { return m_materialAssetReference; }
        void SetMaterialAssetReference(const std::string &materialAssetReference) { m_materialAssetReference = materialAssetReference; }
        int GetFlipbookColumns() const { return m_flipbookColumns; }
        int GetFlipbookRows() const { return m_flipbookRows; }
        float GetFlipbookFramesPerSecond() const { return m_flipbookFramesPerSecond; }
        bool GetFlipbookLooping() const { return m_flipbookLooping; }
        bool GetFlipbookRandomStart() const { return m_flipbookRandomStart; }
        bool GetSoftParticlesEnabled() const { return m_softParticlesEnabled; }
        float GetSoftParticleDistance() const { return m_softParticleDistance; }
        bool GetSmokeLightingEnabled() const { return m_smokeLightingEnabled; }
        float GetSmokeLightingStrength() const { return m_smokeLightingStrength; }
        float GetSmokeAmbient() const { return m_smokeAmbient; }
        bool UsesCpuSimulation() const;
        const std::vector<ParticleCpuData> &GetCpuParticles() const { return m_cpuParticles; }
        void BuildTrailRenderSegments(std::vector<ParticleTrailRenderSegment> &segments) const;
        bool GetTrailsEnabled() const { return m_trailsEnabled; }
        const std::string &GetTrailMaterialAssetReference() const { return m_trailMaterialAssetReference; }
        const std::string &GetParticleSystemAssetReference() const { return m_particleSystemAssetReference; }
        bool SetParticleSystemAssetReference(std::string particleSystemAssetReference);
        void ApplyParticleSystemAsset(const assets::ParticleSystemAsset &asset);

    private:
        struct EmitAtRequest
        {
            glm::vec3 worldPosition{0.0f};
            int count = 0;
        };

        void ResetPlaybackState(bool clearParticles);
        void ConfigureParticleAttributes(GLuint vao, GLuint buffer) const;
        void UpdateCpuSimulation(float deltaTime);
        void SpawnCpuParticles(int count);
        void SpawnCpuParticlesAt(const glm::vec3 &worldPosition, int count);
        void SpawnCpuParticlesFromAsset(const assets::ParticleSystemAsset &asset, const glm::vec3 &worldPosition, int count);
        void SpawnSubEmitter(const std::string &assetReference, int count, const glm::vec3 &worldPosition);
        void ResizeCpuStorage();

        bool m_playing = false;
        bool m_paused = false;
        bool m_playOnAwake = true;
        bool m_looping = true;
        bool m_burstFiredThisCycle = false;
        float m_duration = 5.0f;
        float m_time = 0.0f;
        float m_pendingDeltaTime = 0.0f;
        float m_gpuSimulationTimeRemaining = 0.0f;
        float m_emissionAccumulator = 0.0f;
        int m_pendingEmitCount = 0;
        int m_nextEmitIndex = 0;
        int m_nextEmitSequence = 0;
        int m_particleCountEstimate = 0;
        bool m_clearRequested = true;

        int m_maxParticles = 1000;
        float m_startLifetime = 5.0f;
        float m_lifetimeVariation = 0.0f;
        float m_startSpeed = 2.0f;
        float m_speedVariation = 0.0f;
        float m_startSize = 0.25f;
        float m_sizeVariation = 0.0f;
        glm::vec4 m_startColor{1.0f};
        bool m_colorOverLifetimeEnabled = false;
        glm::vec4 m_endColor{1.0f, 1.0f, 1.0f, 0.0f};
        bool m_sizeOverLifetimeEnabled = false;
        float m_endSize = 0.0f;
        float m_gravityModifier = 0.0f;
        float m_drag = 0.0f;
        float m_buoyancy = 0.0f;
        glm::vec3 m_windVelocity{0.0f};
        float m_turbulenceStrength = 0.0f;
        float m_turbulenceFrequency = 0.5f;
        float m_rotationSpeed = 0.0f;
        float m_rotationSpeedVariation = 0.0f;
        float m_startRotation = 0.0f;
        float m_startRotationVariation = 180.0f;
        float m_fadeInFraction = 0.0f;
        float m_fadeOutFraction = 0.25f;
        float m_emissionRateOverTime = 10.0f;
        float m_burstTime = 0.0f;
        int m_burstCount = 0;
        ParticleSimulationSpace m_simulationSpace = ParticleSimulationSpace::Local;
        ParticleShape m_shape = ParticleShape::Point;
        glm::vec3 m_shapeSize{1.0f};
        float m_shapeRadius = 1.0f;
        float m_coneAngle = 25.0f;
        assets::ParticleRenderShape m_renderShape = assets::ParticleRenderShape::Circle;
        std::string m_materialAssetReference;
        int m_flipbookColumns = 1;
        int m_flipbookRows = 1;
        float m_flipbookFramesPerSecond = 0.0f;
        bool m_flipbookLooping = true;
        bool m_flipbookRandomStart = true;
        bool m_softParticlesEnabled = false;
        float m_softParticleDistance = 0.5f;
        bool m_smokeLightingEnabled = false;
        float m_smokeLightingStrength = 0.65f;
        float m_smokeAmbient = 0.35f;
        bool m_collisionEnabled = false;
        assets::ParticleCollisionMode m_collisionMode = assets::ParticleCollisionMode::Kill;
        float m_collisionDampening = 0.0f;
        float m_collisionBounce = 0.5f;
        float m_collisionLifetimeLoss = 0.0f;
        float m_collisionRadius = 0.05f;
        int m_collisionMaxChecksPerFrame = 256;
        bool m_trailsEnabled = false;
        float m_trailLifetime = 0.5f;
        float m_trailWidth = 0.08f;
        bool m_trailInheritParticleColor = true;
        std::string m_trailMaterialAssetReference;
        std::string m_collisionSubEmitterAssetReference;
        int m_collisionSubEmitterCount = 0;
        std::string m_deathSubEmitterAssetReference;
        int m_deathSubEmitterCount = 0;
        std::string m_particleSystemAssetReference;
        bool m_emitAtRequested = false;
        std::vector<EmitAtRequest> m_pendingEmitAtRequests;
        std::vector<ParticleCpuData> m_cpuParticles;
        std::vector<std::vector<ParticleTrailPoint>> m_trails;
        int m_nextCpuEmitIndex = 0;

        bool m_gpuInitialized = false;
        bool m_gpuStateDirty = true;
        int m_gpuCapacity = 0;
        int m_readBufferIndex = 0;
        std::array<GLuint, 2> m_buffers{0, 0};
        std::array<GLuint, 2> m_vaos{0, 0};
    };
}
