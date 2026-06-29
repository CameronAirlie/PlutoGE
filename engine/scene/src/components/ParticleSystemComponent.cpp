#include "PlutoGE/scene/components/ParticleSystemComponent.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <sstream>

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

        if (!m_playing || m_paused)
        {
            return;
        }

        const float step = std::max(deltaTime, 0.0f);
        if (step <= 0.0f)
        {
            return;
        }

        const float previousTime = m_time;
        m_time += step;
        m_pendingDeltaTime += step;

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

    void ParticleSystemComponent::Play()
    {
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
        m_clearRequested = true;
        m_pendingEmitCount = 0;
    }

    void ParticleSystemComponent::Emit(int count)
    {
        if (count <= 0)
        {
            return;
        }

        const int available = std::max(0, m_maxParticles - m_particleCountEstimate);
        const int accepted = std::min(count, available);
        m_pendingEmitCount += accepted;
        m_particleCountEstimate = std::min(m_maxParticles, m_particleCountEstimate + accepted);
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

    void ParticleSystemComponent::SetShapeRadius(float radius)
    {
        m_shapeRadius = std::max(radius, 0.0f);
    }

    void ParticleSystemComponent::SetConeAngle(float angle)
    {
        m_coneAngle = std::clamp(angle, 0.0f, 89.0f);
    }

    std::vector<Property> ParticleSystemComponent::Serialize() const
    {
        return {
            {"PlayOnAwake", PropertyType::Bool, m_playOnAwake ? "true" : "false"},
            {"Looping", PropertyType::Bool, m_looping ? "true" : "false"},
            {"Duration", PropertyType::Float, std::to_string(m_duration)},
            {"MaxParticles", PropertyType::Int, std::to_string(m_maxParticles)},
            {"StartLifetime", PropertyType::Float, std::to_string(m_startLifetime)},
            {"StartSpeed", PropertyType::Float, std::to_string(m_startSpeed)},
            {"StartSize", PropertyType::Float, std::to_string(m_startSize)},
            {"StartColor", PropertyType::String, SerializeVec4(m_startColor)},
            {"GravityModifier", PropertyType::Float, std::to_string(m_gravityModifier)},
            {"EmissionRateOverTime", PropertyType::Float, std::to_string(m_emissionRateOverTime)},
            {"BurstTime", PropertyType::Float, std::to_string(m_burstTime)},
            {"BurstCount", PropertyType::Int, std::to_string(m_burstCount)},
            {"SimulationSpace", PropertyType::Enum, m_simulationSpace == ParticleSimulationSpace::World ? "World" : "Local", {"Local", "World"}},
            {"Shape", PropertyType::Enum, m_shape == ParticleShape::Sphere ? "Sphere" : m_shape == ParticleShape::Box ? "Box" : m_shape == ParticleShape::Cone ? "Cone" : "Point", {"Point", "Sphere", "Box", "Cone"}},
            {"ShapeSize", PropertyType::String, SerializeVec3(m_shapeSize)},
            {"ShapeRadius", PropertyType::Float, std::to_string(m_shapeRadius)},
            {"ConeAngle", PropertyType::Float, std::to_string(m_coneAngle)},
            {"MaterialAsset", PropertyType::String, m_materialAssetReference},
        };
    }

    void ParticleSystemComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "PlayOnAwake") SetPlayOnAwake(ParseBool(property.value));
            else if (property.name == "Looping") SetLooping(ParseBool(property.value));
            else if (property.name == "Duration") SetDuration(std::stof(property.value));
            else if (property.name == "MaxParticles") SetMaxParticles(std::stoi(property.value));
            else if (property.name == "StartLifetime") SetStartLifetime(std::stof(property.value));
            else if (property.name == "StartSpeed") SetStartSpeed(std::stof(property.value));
            else if (property.name == "StartSize") SetStartSize(std::stof(property.value));
            else if (property.name == "StartColor") SetStartColor(ParseVec4(property.value, m_startColor));
            else if (property.name == "GravityModifier") SetGravityModifier(std::stof(property.value));
            else if (property.name == "EmissionRateOverTime") SetEmissionRateOverTime(std::stof(property.value));
            else if (property.name == "BurstTime") SetBurstTime(std::stof(property.value));
            else if (property.name == "BurstCount") SetBurstCount(std::stoi(property.value));
            else if (property.name == "SimulationSpace") SetSimulationSpace(property.value == "World" || property.value == "1" ? ParticleSimulationSpace::World : ParticleSimulationSpace::Local);
            else if (property.name == "Shape")
            {
                if (property.value == "Sphere" || property.value == "1") SetShape(ParticleShape::Sphere);
                else if (property.value == "Box" || property.value == "2") SetShape(ParticleShape::Box);
                else if (property.value == "Cone" || property.value == "3") SetShape(ParticleShape::Cone);
                else SetShape(ParticleShape::Point);
            }
            else if (property.name == "ShapeSize") SetShapeSize(ParseVec3(property.value, m_shapeSize));
            else if (property.name == "ShapeRadius") SetShapeRadius(std::stof(property.value));
            else if (property.name == "ConeAngle") SetConeAngle(std::stof(property.value));
            else if (property.name == "MaterialAsset") SetMaterialAssetReference(property.value);
        }
        Clear();
    }
}
