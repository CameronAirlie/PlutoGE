#include "PlutoGE/scene/components/AudioEnvironmentVolumeComponent.h"
#include "PlutoGE/scene/Entity.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_inverse.hpp>

namespace PlutoGE::scene
{
    void AudioEnvironmentVolumeComponent::SetSize(const glm::vec3 &size)
    {
        m_size = glm::max(size, glm::vec3(0.0001f));
    }

    void AudioEnvironmentVolumeComponent::SetRadius(float radius)
    {
        m_radius = std::max(radius, 0.0001f);
    }

    void AudioEnvironmentVolumeComponent::SetPreset(AudioEnvironmentPreset preset)
    {
        m_preset = preset;
        switch (preset)
        {
        case AudioEnvironmentPreset::Cave:       m_reverbWet=.75f; m_reverbDecay=3.8f; m_reverbDensity=1; m_reverbDiffusion=.85f; m_echoWet=.18f; m_echoDelay=.22f; m_echoFeedback=.35f; m_lowPass=.08f; m_gain=1; break;
        case AudioEnvironmentPreset::Forest:     m_reverbWet=.18f; m_reverbDecay=1.25f; m_reverbDensity=.35f; m_reverbDiffusion=.65f; m_echoWet=.06f; m_echoDelay=.12f; m_echoFeedback=.15f; m_lowPass=.04f; m_gain=1; break;
        case AudioEnvironmentPreset::City:       m_reverbWet=.32f; m_reverbDecay=1.8f; m_reverbDensity=.7f; m_reverbDiffusion=.55f; m_echoWet=.12f; m_echoDelay=.16f; m_echoFeedback=.25f; m_lowPass=.02f; m_gain=1; break;
        case AudioEnvironmentPreset::Field:      m_reverbWet=.05f; m_reverbDecay=.45f; m_reverbDensity=.15f; m_reverbDiffusion=.35f; m_echoWet=0; m_lowPass=0; m_gain=1; break;
        case AudioEnvironmentPreset::Room:       m_reverbWet=.3f; m_reverbDecay=.9f; m_reverbDensity=.8f; m_reverbDiffusion=.75f; m_echoWet=.03f; m_echoDelay=.08f; m_echoFeedback=.1f; m_lowPass=.03f; m_gain=1; break;
        case AudioEnvironmentPreset::Hall:       m_reverbWet=.62f; m_reverbDecay=2.7f; m_reverbDensity=1; m_reverbDiffusion=.9f; m_echoWet=.08f; m_echoDelay=.14f; m_echoFeedback=.18f; m_lowPass=.04f; m_gain=1; break;
        case AudioEnvironmentPreset::Underwater: m_reverbWet=.28f; m_reverbDecay=1.5f; m_reverbDensity=.9f; m_reverbDiffusion=.7f; m_echoWet=.02f; m_echoDelay=.1f; m_echoFeedback=.1f; m_lowPass=.82f; m_gain=.72f; break;
        case AudioEnvironmentPreset::Custom: break;
        }
    }

    float AudioEnvironmentVolumeComponent::InfluenceAt(const glm::vec3 &worldPosition) const
    {
        if (!GetOwner()) return 0.0f;
        const glm::vec3 p = glm::vec3(glm::inverse(GetOwner()->GetWorldTransform()) * glm::vec4(worldPosition, 1.0f));
        float signedInside = 0.0f;
        if (m_shape == AudioEnvironmentShape::Sphere)
            signedInside = std::max(m_radius, .001f) - glm::length(p);
        else
        {
            const glm::vec3 d = glm::max(glm::abs(p) - glm::max(m_size * .5f, glm::vec3(.001f)), glm::vec3(0));
            if (glm::dot(d, d) > 0) return 0.0f;
            const glm::vec3 edge = glm::max(m_size * .5f, glm::vec3(.001f)) - glm::abs(p);
            signedInside = std::min(edge.x, std::min(edge.y, edge.z));
        }
        if (signedInside < 0) return 0.0f;
        return m_blendDistance <= .001f ? 1.0f : std::clamp(signedInside / m_blendDistance, 0.0f, 1.0f);
    }

    std::vector<Property> AudioEnvironmentVolumeComponent::Serialize() const
    {
        return {{"Preset",PropertyType::Enum,std::to_string((int)m_preset),{"Custom","Cave","Forest","City","Field","Room","Hall","Underwater"}},
                {"Shape",PropertyType::Enum,std::to_string((int)m_shape),{"Box","Sphere"}}, {"Size",PropertyType::Vec3,std::to_string(m_size.x)+","+std::to_string(m_size.y)+","+std::to_string(m_size.z)},
                {"Radius",PropertyType::Float,std::to_string(m_radius)}, {"Blend Distance",PropertyType::Float,std::to_string(m_blendDistance)}, {"Priority",PropertyType::Int,std::to_string(m_priority)},
                {"Reverb Wet",PropertyType::Float,std::to_string(m_reverbWet)}, {"Reverb Decay",PropertyType::Float,std::to_string(m_reverbDecay)}, {"Reverb Density",PropertyType::Float,std::to_string(m_reverbDensity)}, {"Reverb Diffusion",PropertyType::Float,std::to_string(m_reverbDiffusion)},
                {"Echo Wet",PropertyType::Float,std::to_string(m_echoWet)}, {"Echo Delay",PropertyType::Float,std::to_string(m_echoDelay)}, {"Echo Feedback",PropertyType::Float,std::to_string(m_echoFeedback)}, {"Low Pass",PropertyType::Float,std::to_string(m_lowPass)}, {"Gain",PropertyType::Float,std::to_string(m_gain)}};
    }

    void AudioEnvironmentVolumeComponent::Deserialize(const std::vector<Property> &ps)
    {
        const auto previousPreset = m_preset;
        for (const auto &p:ps) {
            if(p.name=="Preset") m_preset=(AudioEnvironmentPreset)std::clamp(std::stoi(p.value),0,7); else if(p.name=="Shape") m_shape=(AudioEnvironmentShape)std::clamp(std::stoi(p.value),0,1);
            else if(p.name=="Size") std::sscanf(p.value.c_str(),"%f,%f,%f",&m_size.x,&m_size.y,&m_size.z); else if(p.name=="Radius") m_radius=std::max(std::stof(p.value),.01f); else if(p.name=="Blend Distance") m_blendDistance=std::max(std::stof(p.value),0.f); else if(p.name=="Priority") m_priority=std::stoi(p.value);
            else if(p.name=="Reverb Wet") m_reverbWet=std::clamp(std::stof(p.value),0.f,1.f); else if(p.name=="Reverb Decay") m_reverbDecay=std::clamp(std::stof(p.value),.1f,20.f); else if(p.name=="Reverb Density") m_reverbDensity=std::clamp(std::stof(p.value),0.f,1.f); else if(p.name=="Reverb Diffusion") m_reverbDiffusion=std::clamp(std::stof(p.value),0.f,1.f);
            else if(p.name=="Echo Wet") m_echoWet=std::clamp(std::stof(p.value),0.f,1.f); else if(p.name=="Echo Delay") m_echoDelay=std::clamp(std::stof(p.value),.01f,.207f); else if(p.name=="Echo Feedback") m_echoFeedback=std::clamp(std::stof(p.value),0.f,1.f); else if(p.name=="Low Pass") m_lowPass=std::clamp(std::stof(p.value),0.f,1.f); else if(p.name=="Gain") m_gain=std::clamp(std::stof(p.value),0.f,2.f);
        }
        // Selecting a named preset in the inspector applies its complete
        // acoustic signature. Choose Custom before hand-authoring parameters.
        if (m_preset != previousPreset && m_preset != AudioEnvironmentPreset::Custom)
            SetPreset(m_preset);
    }
}
