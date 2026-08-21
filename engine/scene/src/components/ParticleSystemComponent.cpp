#include "PlutoGE/scene/components/ParticleSystemComponent.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <sstream>

#include <glm/gtc/constants.hpp>

namespace PlutoGE::scene
{
    namespace
    {
        std::string SerializeVec3(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        std::string SerializeVec4(const glm::vec4 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z) + "," + std::to_string(value.w);
        }

        glm::vec3 ParseVec3(const std::string &value, const glm::vec3 &fallback)
        {
            glm::vec3 result = fallback;
            std::sscanf(value.c_str(), "%f,%f,%f", &result.x, &result.y, &result.z);
            return result;
        }

        glm::vec4 ParseVec4(const std::string &value, const glm::vec4 &fallback)
        {
            glm::vec4 result = fallback;
            std::sscanf(value.c_str(), "%f,%f,%f,%f", &result.x, &result.y, &result.z, &result.w);
            return result;
        }

        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "1";
        }

        ParticleSimulationSpace ToSceneSimulationSpace(assets::ParticleSimulationSpace simulationSpace)
        {
            return simulationSpace == assets::ParticleSimulationSpace::World ? ParticleSimulationSpace::World : ParticleSimulationSpace::Local;
        }

        ParticleShape ToSceneShape(assets::ParticleShape shape)
        {
            switch (shape)
            {
            case assets::ParticleShape::Sphere:
                return ParticleShape::Sphere;
            case assets::ParticleShape::Box:
                return ParticleShape::Box;
            case assets::ParticleShape::Cone:
                return ParticleShape::Cone;
            case assets::ParticleShape::Point:
            default:
                return ParticleShape::Point;
            }
        }

        float Hash(float value)
        {
            const float hashed = std::sin(value) * 43758.5453123f;
            return hashed - std::floor(hashed);
        }

        glm::vec3 RandomDirection(float seed)
        {
            const float z = Hash(seed + 1.0f) * 2.0f - 1.0f;
            const float angle = Hash(seed + 2.0f) * glm::two_pi<float>();
            const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
            return glm::normalize(glm::vec3(radius * std::cos(angle), z, radius * std::sin(angle)));
        }

        glm::vec3 RandomBox(float seed, const glm::vec3 &size)
        {
            return (glm::vec3(Hash(seed + 3.0f), Hash(seed + 4.0f), Hash(seed + 5.0f)) - glm::vec3(0.5f)) * size;
        }

        glm::vec2 RandomDisc(float seed, float radius)
        {
            const float angle = Hash(seed + 9.0f) * glm::two_pi<float>();
            const float distance = std::sqrt(Hash(seed + 10.0f)) * radius;
            return glm::vec2(std::cos(angle), std::sin(angle)) * distance;
        }

        glm::vec3 EmitOffset(ParticleShape shape, const glm::vec3 &shapeSize, float shapeRadius, float seed)
        {
            switch (shape)
            {
            case ParticleShape::Sphere:
                return RandomDirection(seed) * shapeRadius * std::cbrt(Hash(seed + 6.0f));
            case ParticleShape::Box:
                return RandomBox(seed, shapeSize);
            case ParticleShape::Cone:
            {
                const glm::vec2 disc = RandomDisc(seed, shapeRadius);
                return glm::vec3(disc.x, 0.0f, disc.y);
            }
            case ParticleShape::Point:
            default:
                return glm::vec3(0.0f);
            }
        }

        glm::vec3 EmitDirection(ParticleShape shape, float coneAngle, float seed, const glm::vec3 &offset)
        {
            if (shape == ParticleShape::Cone)
            {
                const float angle = Hash(seed + 7.0f) * glm::two_pi<float>();
                const float radial = std::tan(glm::radians(coneAngle)) * std::sqrt(Hash(seed + 8.0f));
                return glm::normalize(glm::vec3(std::cos(angle) * radial, 1.0f, std::sin(angle) * radial));
            }

            if (glm::length(offset) > 0.0001f)
            {
                return glm::normalize(offset);
            }

            return RandomDirection(seed);
        }

        glm::vec3 TurbulenceField(const glm::vec3 &position, float frequency, float seed)
        {
            const glm::vec3 p = position * frequency + glm::vec3(seed * 0.013f);
            // Divergence-free-ish analytic field. Sampling world position makes
            // neighbouring particles roll together instead of jittering independently.
            return glm::vec3(std::sin(p.y + p.z) - std::cos(p.z - p.y),
                             std::sin(p.z + p.x) - std::cos(p.x - p.z),
                             std::sin(p.x + p.y) - std::cos(p.y - p.x)) * 0.5f;
        }
    }

    ParticleSystemComponent::~ParticleSystemComponent()
    {
        ReleaseGpuResources();
    }

    void ParticleSystemComponent::Update(float deltaTime)
    {
        if (m_playOnAwake && !m_playing && !m_paused && m_time == 0.0f)
        {
            Play();
        }

        if (m_paused)
        {
            return;
        }

        const float step = std::max(deltaTime, 0.0f);
        if (step <= 0.0f)
        {
            return;
        }

        const bool cpuSimulation = UsesCpuSimulation();

        // GPU particles live independently of the emitter timeline. Advance the
        // tail before this frame can emit new particles, so new particles receive
        // their full configured lifetime.
        if (!cpuSimulation && m_gpuSimulationTimeRemaining > 0.0f)
        {
            m_pendingDeltaTime += step;
            m_gpuSimulationTimeRemaining = std::max(m_gpuSimulationTimeRemaining - step, 0.0f);
            if (m_gpuSimulationTimeRemaining <= 0.0f)
            {
                m_particleCountEstimate = 0;
                m_clearRequested = true;
            }
        }

        if (m_playing)
        {
            const float previousTime = m_time;
            m_time += step;

            if (m_emissionRateOverTime > 0.0f)
            {
                m_emissionAccumulator += m_emissionRateOverTime * step;
                const int emitCount = static_cast<int>(std::floor(m_emissionAccumulator));
                if (emitCount > 0)
                {
                    Emit(emitCount);
                    m_emissionAccumulator -= static_cast<float>(emitCount);
                }
            }

            if (m_burstCount > 0 && !m_burstFiredThisCycle && previousTime <= m_burstTime && m_time >= m_burstTime)
            {
                Emit(m_burstCount);
                m_burstFiredThisCycle = true;
            }

            if (m_duration > 0.0f && m_time >= m_duration)
            {
                if (m_looping)
                {
                    m_time = std::fmod(m_time, m_duration);
                    m_burstFiredThisCycle = false;
                }
                else
                {
                    m_playing = false;
                }
            }
        }

        if (cpuSimulation && (m_particleCountEstimate > 0 || !m_pendingEmitAtRequests.empty()))
        {
            UpdateCpuSimulation(step);
        }
    }

    void ParticleSystemComponent::Play()
    {
        if (!m_paused && !m_playing && (m_time > 0.0f || m_burstFiredThisCycle))
        {
            ResetPlaybackState(false);
        }

        m_playing = true;
        m_paused = false;
    }

    void ParticleSystemComponent::Pause()
    {
        m_paused = true;
        m_playing = false;
    }

    void ParticleSystemComponent::Stop(bool clear)
    {
        m_playing = false;
        m_paused = false;
        ResetPlaybackState(clear);
    }

    void ParticleSystemComponent::Clear()
    {
        m_particleCountEstimate = 0;
        m_gpuSimulationTimeRemaining = 0.0f;
        m_clearRequested = true;
        m_pendingEmitCount = 0;
        m_nextEmitIndex = 0;
        m_nextEmitSequence = 0;
        m_nextCpuEmitIndex = 0;
        for (auto &particle : m_cpuParticles)
        {
            particle = {};
        }
        for (auto &trail : m_trails)
        {
            trail.clear();
        }
    }

    void ParticleSystemComponent::Emit(int count)
    {
        if (count <= 0)
        {
            return;
        }

        if (!m_playing && !m_paused)
        {
            Play();
        }

        if (UsesCpuSimulation())
        {
            SpawnCpuParticles(count);
            return;
        }

        const int accepted = std::min(count, m_maxParticles);
        m_pendingEmitCount = std::min(m_maxParticles, m_pendingEmitCount + accepted);
        m_particleCountEstimate = std::min(m_maxParticles, m_particleCountEstimate + accepted);
        m_gpuSimulationTimeRemaining = std::max(m_gpuSimulationTimeRemaining, m_startLifetime * (1.0f + m_lifetimeVariation));
    }

    void ParticleSystemComponent::EmitAt(const glm::vec3 &worldPosition, int count)
    {
        if (count <= 0)
        {
            return;
        }

        if (!m_playing && !m_paused)
        {
            Play();
        }

        m_emitAtRequested = true;
        if (UsesCpuSimulation())
        {
            SpawnCpuParticlesAt(worldPosition, count);
            return;
        }

        m_pendingEmitAtRequests.push_back({worldPosition, count});
    }

    float ParticleSystemComponent::ConsumePendingDeltaTime()
    {
        const float deltaTime = m_pendingDeltaTime;
        m_pendingDeltaTime = 0.0f;
        return deltaTime;
    }

    int ParticleSystemComponent::ConsumePendingEmitCount()
    {
        const int count = m_pendingEmitCount;
        m_pendingEmitCount = 0;
        return count;
    }

    bool ParticleSystemComponent::ConsumeClearRequested()
    {
        const bool requested = m_clearRequested;
        m_clearRequested = false;
        return requested;
    }

    void ParticleSystemComponent::AdvanceEmitCursor(int count)
    {
        if (count <= 0 || m_maxParticles <= 0)
        {
            return;
        }

        m_nextEmitIndex = (m_nextEmitIndex + count) % m_maxParticles;
        m_nextEmitSequence = (m_nextEmitSequence + count) & 0x3fffffff;
    }

    bool ParticleSystemComponent::ConsumeGpuStateDirty()
    {
        const bool dirty = m_gpuStateDirty;
        m_gpuStateDirty = false;
        return dirty;
    }

    void ParticleSystemComponent::EnsureGpuResources()
    {
        if (m_gpuInitialized && m_gpuCapacity == m_maxParticles)
        {
            return;
        }

        ReleaseGpuResources();

        std::vector<ParticleGpuData> particles(static_cast<std::size_t>(m_maxParticles));
        for (int index = 0; index < m_maxParticles; ++index)
        {
            particles[static_cast<std::size_t>(index)].positionAge.w = 999999.0f;
            particles[static_cast<std::size_t>(index)].velocityLifetime.w = m_startLifetime;
            particles[static_cast<std::size_t>(index)].seed = glm::vec4(
                static_cast<float>(index) * 12.9898f,
                static_cast<float>(index) * 78.233f,
                static_cast<float>(index) * 37.719f,
                1.0f);
        }

        glGenBuffers(2, m_buffers.data());
        glGenVertexArrays(2, m_vaos.data());
        for (int index = 0; index < 2; ++index)
        {
            glBindBuffer(GL_ARRAY_BUFFER, m_buffers[static_cast<std::size_t>(index)]);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(particles.size() * sizeof(ParticleGpuData)),
                         particles.data(),
                         GL_DYNAMIC_COPY);
            ConfigureParticleAttributes(m_vaos[static_cast<std::size_t>(index)], m_buffers[static_cast<std::size_t>(index)]);
        }
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        m_gpuCapacity = m_maxParticles;
        m_readBufferIndex = 0;
        m_gpuInitialized = true;
        m_gpuStateDirty = false;
        m_clearRequested = true;
    }

    void ParticleSystemComponent::ReleaseGpuResources()
    {
        if (m_vaos[0] != 0 || m_vaos[1] != 0)
        {
            glDeleteVertexArrays(2, m_vaos.data());
        }
        if (m_buffers[0] != 0 || m_buffers[1] != 0)
        {
            glDeleteBuffers(2, m_buffers.data());
        }
        m_vaos = {0, 0};
        m_buffers = {0, 0};
        m_gpuInitialized = false;
        m_gpuCapacity = 0;
        m_readBufferIndex = 0;
        m_nextEmitIndex = 0;
        m_nextEmitSequence = 0;
    }

    void ParticleSystemComponent::ConfigureParticleAttributes(GLuint vao, GLuint buffer) const
    {
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleGpuData), reinterpret_cast<const void *>(offsetof(ParticleGpuData, positionAge)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleGpuData), reinterpret_cast<const void *>(offsetof(ParticleGpuData, velocityLifetime)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleGpuData), reinterpret_cast<const void *>(offsetof(ParticleGpuData, colorSize)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleGpuData), reinterpret_cast<const void *>(offsetof(ParticleGpuData, seed)));
    }

    void ParticleSystemComponent::ResetPlaybackState(bool clearParticles)
    {
        m_time = 0.0f;
        m_emissionAccumulator = 0.0f;
        m_pendingDeltaTime = 0.0f;
        m_burstFiredThisCycle = false;
        if (clearParticles)
        {
            Clear();
        }
    }

    void ParticleSystemComponent::SetDuration(float duration)
    {
        m_duration = std::max(duration, 0.0001f);
    }

    void ParticleSystemComponent::SetMaxParticles(int maxParticles)
    {
        m_maxParticles = std::clamp(maxParticles, 1, 200000);
        m_particleCountEstimate = std::min(m_particleCountEstimate, m_maxParticles);
        ResizeCpuStorage();
        MarkGpuStateDirty();
    }

    void ParticleSystemComponent::SetStartLifetime(float lifetime)
    {
        m_startLifetime = std::max(lifetime, 0.0001f);
    }

    void ParticleSystemComponent::SetStartSpeed(float speed)
    {
        m_startSpeed = std::max(speed, 0.0f);
    }

    void ParticleSystemComponent::SetStartSize(float size)
    {
        m_startSize = std::max(size, 0.0f);
    }

    void ParticleSystemComponent::SetEmissionRateOverTime(float rate)
    {
        m_emissionRateOverTime = std::max(rate, 0.0f);
    }

    void ParticleSystemComponent::SetBurstTime(float time)
    {
        m_burstTime = std::max(time, 0.0f);
    }

    void ParticleSystemComponent::SetBurstCount(int count)
    {
        m_burstCount = std::max(count, 0);
    }

    void ParticleSystemComponent::SetShape(ParticleShape shape)
    {
        if (m_shape == shape)
        {
            return;
        }

        m_shape = shape;
        Clear();
    }

    void ParticleSystemComponent::SetShapeSize(const glm::vec3 &size)
    {
        const glm::vec3 clampedSize = glm::max(size, glm::vec3(0.0f));
        if (m_shapeSize == clampedSize)
        {
            return;
        }

        m_shapeSize = clampedSize;
        Clear();
    }

    void ParticleSystemComponent::SetShapeRadius(float radius)
    {
        const float clampedRadius = std::max(radius, 0.0f);
        if (m_shapeRadius == clampedRadius)
        {
            return;
        }

        m_shapeRadius = clampedRadius;
        Clear();
    }

    void ParticleSystemComponent::SetConeAngle(float angle)
    {
        const float clampedAngle = std::clamp(angle, 0.0f, 89.0f);
        if (m_coneAngle == clampedAngle)
        {
            return;
        }

        m_coneAngle = clampedAngle;
        Clear();
    }

    bool ParticleSystemComponent::UsesCpuSimulation() const
    {
        return m_collisionEnabled || m_trailsEnabled ||
               !m_collisionSubEmitterAssetReference.empty() || !m_deathSubEmitterAssetReference.empty() ||
               m_emitAtRequested;
    }

    void ParticleSystemComponent::ResizeCpuStorage()
    {
        m_cpuParticles.resize(static_cast<std::size_t>(m_maxParticles));
        m_trails.resize(static_cast<std::size_t>(m_maxParticles));
        if (m_nextCpuEmitIndex >= m_maxParticles)
        {
            m_nextCpuEmitIndex = 0;
        }
    }

    void ParticleSystemComponent::SpawnCpuParticles(int count)
    {
        auto *owner = GetOwner();
        if (!owner)
        {
            return;
        }

        const glm::vec3 origin = owner->GetWorldPosition();
        if (m_simulationSpace == ParticleSimulationSpace::World)
        {
            SpawnCpuParticlesAt(origin, count);
            return;
        }

        SpawnCpuParticlesAt(origin, count);
    }

    void ParticleSystemComponent::SpawnCpuParticlesAt(const glm::vec3 &worldPosition, int count)
    {
        if (count <= 0 || m_maxParticles <= 0)
        {
            return;
        }

        ResizeCpuStorage();
        auto *owner = GetOwner();
        const glm::mat4 emitterTransform = owner ? owner->GetWorldTransform() : glm::mat4(1.0f);
        const glm::mat3 emitterBasis(emitterTransform);

        const int emitCount = std::min(count, m_maxParticles);
        for (int emitIndex = 0; emitIndex < emitCount; ++emitIndex)
        {
            const int particleIndex = m_nextCpuEmitIndex;
            const float seed = static_cast<float>(m_nextEmitSequence + emitIndex) * 17.0f + static_cast<float>(emitIndex) * 12.9898f;
            const glm::vec3 localOffset = EmitOffset(m_shape, m_shapeSize, m_shapeRadius, seed);
            const glm::vec3 localDirection = EmitDirection(m_shape, m_coneAngle, seed, localOffset);
            const glm::vec3 spawnDirection = glm::normalize(emitterBasis * localDirection);

            auto &particle = m_cpuParticles[static_cast<std::size_t>(particleIndex)];
            particle.position = worldPosition + emitterBasis * localOffset;
            particle.velocity = spawnDirection * m_startSpeed * (1.0f + (Hash(seed + 20.0f) * 2.0f - 1.0f) * m_speedVariation);
            particle.color = m_startColor;
            particle.size = m_startSize * (1.0f + (Hash(seed + 21.0f) * 2.0f - 1.0f) * m_sizeVariation);
            particle.age = 0.0f;
            particle.lifetime = std::max(m_startLifetime * (1.0f + (Hash(seed + 22.0f) * 2.0f - 1.0f) * m_lifetimeVariation), 0.0001f);
            particle.seed = seed;
            particle.active = true;
            particle.deathSubEmitterFired = false;

            if (particleIndex < static_cast<int>(m_trails.size()))
            {
                auto &trail = m_trails[static_cast<std::size_t>(particleIndex)];
                trail.clear();
                trail.push_back({particle.position, particle.color, 0.0f});
            }

            m_nextCpuEmitIndex = (m_nextCpuEmitIndex + 1) % m_maxParticles;
            m_nextEmitSequence = (m_nextEmitSequence + 1) & 0x3fffffff;
        }

        m_particleCountEstimate = std::min(m_maxParticles, m_particleCountEstimate + emitCount);
    }

    void ParticleSystemComponent::SpawnCpuParticlesFromAsset(const assets::ParticleSystemAsset &asset, const glm::vec3 &worldPosition, int count)
    {
        if (count <= 0 || m_maxParticles <= 0)
        {
            return;
        }

        ResizeCpuStorage();
        const int emitCount = std::min(count, m_maxParticles);
        for (int emitIndex = 0; emitIndex < emitCount; ++emitIndex)
        {
            const int particleIndex = m_nextCpuEmitIndex;
            const float seed = static_cast<float>(m_nextEmitSequence + emitIndex) * 19.0f + static_cast<float>(emitIndex) * 7.77f;
            const auto shape = ToSceneShape(asset.shape);
            const glm::vec3 offset = EmitOffset(shape, asset.shapeSize, asset.shapeRadius, seed);
            const glm::vec3 direction = EmitDirection(shape, asset.coneAngle, seed, offset);

            auto &particle = m_cpuParticles[static_cast<std::size_t>(particleIndex)];
            particle.position = worldPosition + offset;
            particle.velocity = direction * std::max(asset.startSpeed, 0.0f);
            particle.color = glm::clamp(asset.startColor, glm::vec4(0.0f), glm::vec4(1.0f));
            particle.size = std::max(asset.startSize, 0.0f);
            particle.age = 0.0f;
            particle.lifetime = std::max(asset.startLifetime, 0.0001f);
            particle.seed = seed;
            particle.active = true;
            particle.deathSubEmitterFired = true;

            if (particleIndex < static_cast<int>(m_trails.size()))
            {
                auto &trail = m_trails[static_cast<std::size_t>(particleIndex)];
                trail.clear();
                trail.push_back({particle.position, particle.color, 0.0f});
            }

            m_nextCpuEmitIndex = (m_nextCpuEmitIndex + 1) % m_maxParticles;
            m_nextEmitSequence = (m_nextEmitSequence + 1) & 0x3fffffff;
        }

        m_particleCountEstimate = std::min(m_maxParticles, m_particleCountEstimate + emitCount);
    }

    void ParticleSystemComponent::SpawnSubEmitter(const std::string &assetReference, int count, const glm::vec3 &worldPosition)
    {
        if (assetReference.empty() || count <= 0)
        {
            return;
        }

        bool loaded = false;
        const auto asset = core::Engine::GetInstance().GetAssetManager().LoadParticleSystemAsset(assetReference, &loaded);
        if (loaded)
        {
            SpawnCpuParticlesFromAsset(asset, worldPosition, count);
        }
    }

    void ParticleSystemComponent::UpdateCpuSimulation(float deltaTime)
    {
        ResizeCpuStorage();

        for (const auto &request : m_pendingEmitAtRequests)
        {
            SpawnCpuParticlesAt(request.worldPosition, request.count);
        }
        m_pendingEmitAtRequests.clear();

        auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        const auto ignoredEntityId = owner ? owner->GetID() : 0;
        int collisionChecks = 0;

        struct CollisionCandidate
        {
            std::size_t particleIndex = 0;
        };

        struct PendingSubEmitter
        {
            std::string assetReference;
            int count = 0;
            glm::vec3 position{0.0f};
        };

        std::vector<glm::vec3> nextPositions(m_cpuParticles.size(), glm::vec3(0.0f));
        std::vector<CollisionCandidate> collisionCandidates;
        std::vector<PhysicsRaycastRequest> collisionRequests;
        std::vector<PendingSubEmitter> pendingSubEmitters;
        if (m_collisionEnabled && scene && m_collisionMaxChecksPerFrame > 0)
        {
            collisionCandidates.reserve(static_cast<std::size_t>(std::min(m_collisionMaxChecksPerFrame, m_maxParticles)));
            collisionRequests.reserve(collisionCandidates.capacity());
        }

        for (std::size_t index = 0; index < m_cpuParticles.size(); ++index)
        {
            auto &particle = m_cpuParticles[index];
            if (!particle.active)
            {
                continue;
            }

            for (auto &point : m_trails[index])
            {
                point.age += deltaTime;
            }
            m_trails[index].erase(std::remove_if(m_trails[index].begin(),
                                                 m_trails[index].end(),
                                                 [this](const ParticleTrailPoint &point)
                                                 { return point.age > m_trailLifetime; }),
                                  m_trails[index].end());

            const glm::vec3 previousPosition = particle.position;
            const glm::vec3 acceleration = glm::vec3(0.0f, m_buoyancy - 9.81f * m_gravityModifier, 0.0f) +
                                           m_windVelocity + TurbulenceField(particle.position, m_turbulenceFrequency, particle.seed) * m_turbulenceStrength;
            particle.velocity += acceleration * deltaTime;
            particle.velocity *= std::exp(-m_drag * deltaTime);
            glm::vec3 nextPosition = particle.position + particle.velocity * deltaTime;
            particle.age += deltaTime;
            nextPositions[index] = nextPosition;

            if (m_collisionEnabled && scene && collisionChecks < m_collisionMaxChecksPerFrame)
            {
                const glm::vec3 displacement = nextPosition - previousPosition;
                const float distance = glm::length(displacement);
                if (distance > 0.00001f)
                {
                    ++collisionChecks;
                    collisionCandidates.push_back({index});
                    collisionRequests.push_back({previousPosition, displacement / distance, distance + m_collisionRadius});
                }
            }
        }

        std::vector<PhysicsRaycastHit> collisionHits;
        std::vector<uint8_t> collisionHitResults;
        if (!collisionRequests.empty() && scene)
        {
            scene->RaycastBatch(collisionRequests, ignoredEntityId, collisionHits, collisionHitResults);
        }

        for (std::size_t requestIndex = 0; requestIndex < collisionCandidates.size(); ++requestIndex)
        {
            if (requestIndex >= collisionHitResults.size() || collisionHitResults[requestIndex] == 0)
            {
                continue;
            }

            const std::size_t particleIndex = collisionCandidates[requestIndex].particleIndex;
            if (particleIndex >= m_cpuParticles.size())
            {
                continue;
            }

            auto &particle = m_cpuParticles[particleIndex];
            if (!particle.active)
            {
                continue;
            }

            const auto &hit = collisionHits[requestIndex];
            nextPositions[particleIndex] = hit.point + hit.normal * m_collisionRadius;
            if (!m_collisionSubEmitterAssetReference.empty() && m_collisionSubEmitterCount > 0)
            {
                pendingSubEmitters.push_back({m_collisionSubEmitterAssetReference, m_collisionSubEmitterCount, hit.point});
            }

            switch (m_collisionMode)
            {
            case assets::ParticleCollisionMode::Bounce:
                particle.velocity = glm::reflect(particle.velocity, hit.normal) * m_collisionBounce * (1.0f - m_collisionDampening);
                particle.age += particle.lifetime * m_collisionLifetimeLoss;
                break;
            case assets::ParticleCollisionMode::Stop:
                particle.velocity = glm::vec3(0.0f);
                particle.age += particle.lifetime * m_collisionLifetimeLoss;
                break;
            case assets::ParticleCollisionMode::Kill:
            default:
                particle.age = particle.lifetime;
                break;
            }
        }

        int liveCount = 0;
        for (std::size_t index = 0; index < m_cpuParticles.size(); ++index)
        {
            auto &particle = m_cpuParticles[index];
            if (!particle.active)
            {
                continue;
            }

            particle.position = nextPositions[index];

            if (particle.age >= particle.lifetime)
            {
                if (!particle.deathSubEmitterFired)
                {
                    if (!m_deathSubEmitterAssetReference.empty() && m_deathSubEmitterCount > 0)
                    {
                        pendingSubEmitters.push_back({m_deathSubEmitterAssetReference, m_deathSubEmitterCount, particle.position});
                    }
                    particle.deathSubEmitterFired = true;
                }
                particle.active = false;
                m_trails[index].clear();
                continue;
            }

            if (m_trailsEnabled)
            {
                auto &trail = m_trails[index];
                if (trail.empty() || glm::length(trail.back().position - particle.position) > std::max(m_trailWidth * 0.25f, 0.005f))
                {
                    trail.push_back({particle.position, m_trailInheritParticleColor ? particle.color : glm::vec4(1.0f), 0.0f});
                    if (trail.size() > 32)
                    {
                        trail.erase(trail.begin());
                    }
                }
            }

            ++liveCount;
        }

        m_particleCountEstimate = liveCount;
        for (const auto &request : pendingSubEmitters)
        {
            SpawnSubEmitter(request.assetReference, request.count, request.position);
        }
    }

    void ParticleSystemComponent::BuildTrailRenderSegments(std::vector<ParticleTrailRenderSegment> &segments) const
    {
        if (!m_trailsEnabled || m_trailLifetime <= 0.0f || m_trailWidth <= 0.0f)
        {
            return;
        }

        for (const auto &trail : m_trails)
        {
            if (trail.size() < 2)
            {
                continue;
            }

            for (std::size_t index = 1; index < trail.size(); ++index)
            {
                const auto &a = trail[index - 1];
                const auto &b = trail[index];
                const float normalizedAge = std::clamp((a.age + b.age) * 0.5f / std::max(m_trailLifetime, 0.0001f), 0.0f, 1.0f);
                glm::vec4 color = m_trailInheritParticleColor ? (a.color + b.color) * 0.5f : glm::vec4(1.0f);
                color.a *= 1.0f - normalizedAge;
                segments.push_back({a.position, b.position, color, m_trailWidth});
            }
        }
    }

    std::vector<Property> ParticleSystemComponent::Serialize() const
    {
        return {
            {"ParticleSystemAsset", PropertyType::String, m_particleSystemAssetReference},
        };
    }

    void ParticleSystemComponent::Deserialize(const std::vector<Property> &properties)
    {
        bool hasAssetProperty = false;
        for (const auto &property : properties)
        {
            if (property.name == "ParticleSystemAsset")
            {
                hasAssetProperty = true;
                SetParticleSystemAssetReference(property.value);
            }
        }

        if (hasAssetProperty)
        {
            Clear();
            return;
        }

        for (const auto &property : properties)
        {
            if (property.name == "PlayOnAwake")
                SetPlayOnAwake(ParseBool(property.value));
            else if (property.name == "Looping")
                SetLooping(ParseBool(property.value));
            else if (property.name == "Duration")
                SetDuration(std::stof(property.value));
            else if (property.name == "MaxParticles")
                SetMaxParticles(std::stoi(property.value));
            else if (property.name == "StartLifetime")
                SetStartLifetime(std::stof(property.value));
            else if (property.name == "StartSpeed")
                SetStartSpeed(std::stof(property.value));
            else if (property.name == "StartSize")
                SetStartSize(std::stof(property.value));
            else if (property.name == "StartColor")
                SetStartColor(ParseVec4(property.value, m_startColor));
            else if (property.name == "GravityModifier")
                SetGravityModifier(std::stof(property.value));
            else if (property.name == "EmissionRateOverTime")
                SetEmissionRateOverTime(std::stof(property.value));
            else if (property.name == "BurstTime")
                SetBurstTime(std::stof(property.value));
            else if (property.name == "BurstCount")
                SetBurstCount(std::stoi(property.value));
            else if (property.name == "SimulationSpace")
                SetSimulationSpace(property.value == "World" || property.value == "1" ? ParticleSimulationSpace::World : ParticleSimulationSpace::Local);
            else if (property.name == "Shape")
            {
                if (property.value == "Sphere" || property.value == "1")
                    SetShape(ParticleShape::Sphere);
                else if (property.value == "Box" || property.value == "2")
                    SetShape(ParticleShape::Box);
                else if (property.value == "Cone" || property.value == "3")
                    SetShape(ParticleShape::Cone);
                else
                    SetShape(ParticleShape::Point);
            }
            else if (property.name == "ShapeSize")
                SetShapeSize(ParseVec3(property.value, m_shapeSize));
            else if (property.name == "ShapeRadius")
                SetShapeRadius(std::stof(property.value));
            else if (property.name == "ConeAngle")
                SetConeAngle(std::stof(property.value));
            else if (property.name == "MaterialAsset")
                SetMaterialAssetReference(property.value);
        }
        Clear();
    }

    bool ParticleSystemComponent::SetParticleSystemAssetReference(std::string particleSystemAssetReference)
    {
        if (particleSystemAssetReference == m_particleSystemAssetReference)
        {
            return true;
        }

        if (particleSystemAssetReference.empty())
        {
            m_particleSystemAssetReference.clear();
            ApplyParticleSystemAsset(assets::CreateDefaultParticleSystemAsset());
            return true;
        }

        bool loaded = false;
        const auto asset = core::Engine::GetInstance().GetAssetManager().LoadParticleSystemAsset(particleSystemAssetReference, &loaded);
        if (!loaded)
        {
            return false;
        }

        m_particleSystemAssetReference = std::move(particleSystemAssetReference);
        ApplyParticleSystemAsset(asset);
        return true;
    }

    void ParticleSystemComponent::ApplyParticleSystemAsset(const assets::ParticleSystemAsset &asset)
    {
        m_playOnAwake = asset.playOnAwake;
        m_looping = asset.looping;
        m_duration = std::max(asset.duration, 0.0001f);
        m_maxParticles = std::clamp(asset.maxParticles, 1, 200000);
        m_startLifetime = std::max(asset.startLifetime, 0.0001f);
        m_lifetimeVariation = std::clamp(asset.lifetimeVariation, 0.0f, 1.0f);
        m_startSpeed = std::max(asset.startSpeed, 0.0f);
        m_speedVariation = std::clamp(asset.speedVariation, 0.0f, 1.0f);
        m_startSize = std::max(asset.startSize, 0.0f);
        m_sizeVariation = std::clamp(asset.sizeVariation, 0.0f, 1.0f);
        m_startColor = glm::clamp(asset.startColor, glm::vec4(0.0f), glm::vec4(1.0f));
        m_colorOverLifetimeEnabled = asset.colorOverLifetimeEnabled;
        m_endColor = glm::clamp(asset.endColor, glm::vec4(0.0f), glm::vec4(1.0f));
        m_sizeOverLifetimeEnabled = asset.sizeOverLifetimeEnabled;
        m_endSize = std::max(asset.endSize, 0.0f);
        m_gravityModifier = asset.gravityModifier;
        m_drag = std::max(asset.drag, 0.0f);
        m_buoyancy = asset.buoyancy;
        m_windVelocity = asset.windVelocity;
        m_turbulenceStrength = std::max(asset.turbulenceStrength, 0.0f);
        m_turbulenceFrequency = std::max(asset.turbulenceFrequency, 0.0001f);
        m_rotationSpeed = asset.rotationSpeed;
        m_rotationSpeedVariation = std::clamp(asset.rotationSpeedVariation, 0.0f, 1.0f);
        m_fadeInFraction = std::clamp(asset.fadeInFraction, 0.0f, 1.0f);
        m_fadeOutFraction = std::clamp(asset.fadeOutFraction, 0.0f, 1.0f);
        m_emissionRateOverTime = std::max(asset.emissionRateOverTime, 0.0f);
        m_burstTime = std::max(asset.burstTime, 0.0f);
        m_burstCount = std::max(asset.burstCount, 0);
        m_simulationSpace = ToSceneSimulationSpace(asset.simulationSpace);
        m_shape = ToSceneShape(asset.shape);
        m_shapeSize = glm::max(asset.shapeSize, glm::vec3(0.0f));
        m_shapeRadius = std::max(asset.shapeRadius, 0.0f);
        m_coneAngle = std::clamp(asset.coneAngle, 0.0f, 89.0f);
        m_renderShape = asset.renderShape;
        m_materialAssetReference = asset.materialAssetReference;
        m_collisionEnabled = asset.collisionEnabled;
        m_collisionMode = asset.collisionMode;
        m_collisionDampening = std::clamp(asset.collisionDampening, 0.0f, 1.0f);
        m_collisionBounce = std::max(asset.collisionBounce, 0.0f);
        m_collisionLifetimeLoss = std::clamp(asset.collisionLifetimeLoss, 0.0f, 1.0f);
        m_collisionRadius = std::max(asset.collisionRadius, 0.0f);
        m_collisionMaxChecksPerFrame = std::clamp(asset.collisionMaxChecksPerFrame, 0, 200000);
        m_trailsEnabled = asset.trailsEnabled;
        m_trailLifetime = std::max(asset.trailLifetime, 0.0f);
        m_trailWidth = std::max(asset.trailWidth, 0.0f);
        m_trailInheritParticleColor = asset.trailInheritParticleColor;
        m_trailMaterialAssetReference = asset.trailMaterialAssetReference;
        m_collisionSubEmitterAssetReference = asset.collisionSubEmitterAssetReference;
        m_collisionSubEmitterCount = std::max(asset.collisionSubEmitterCount, 0);
        m_deathSubEmitterAssetReference = asset.deathSubEmitterAssetReference;
        m_deathSubEmitterCount = std::max(asset.deathSubEmitterCount, 0);
        ResizeCpuStorage();
        m_particleCountEstimate = std::min(m_particleCountEstimate, m_maxParticles);
        MarkGpuStateDirty();
        Clear();
    }
}
