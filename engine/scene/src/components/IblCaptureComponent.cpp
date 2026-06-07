#include "PlutoGE/scene/components/IblCaptureComponent.h"

#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"

#include <algorithm>
#include <cstdio>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr int kMinCaptureResolution = 32;
        constexpr int kMaxCaptureResolution = 2048;

        glm::vec3 ParseVec3(const std::string &value, const glm::vec3 &fallback)
        {
            glm::vec3 parsed = fallback;
            sscanf_s(value.c_str(), "%f,%f,%f", &parsed.x, &parsed.y, &parsed.z);
            return parsed;
        }

        std::string SerializeVec3(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        void ExpandBounds(glm::vec3 &minBounds, glm::vec3 &maxBounds, const glm::vec3 &point)
        {
            minBounds = glm::min(minBounds, point);
            maxBounds = glm::max(maxBounds, point);
        }
    }

    void IblCaptureComponent::SetSize(const glm::vec3 &size)
    {
        m_size = glm::max(size, glm::vec3(0.0001f));
    }

    void IblCaptureComponent::SetIntensity(float intensity)
    {
        m_intensity = std::max(intensity, 0.0f);
    }

    void IblCaptureComponent::SetBlendDistance(float blendDistance)
    {
        m_blendDistance = std::max(blendDistance, 0.0f);
    }

    void IblCaptureComponent::SetResolution(int resolution)
    {
        const int clampedResolution = std::clamp(resolution, kMinCaptureResolution, kMaxCaptureResolution);
        if (m_resolution != clampedResolution)
        {
            m_resolution = clampedResolution;
            m_captureTexture.reset();
            MarkDirty();
        }
    }

    void IblCaptureComponent::SetFarPlane(float farPlane)
    {
        const float clampedFarPlane = std::max(farPlane, 1.0f);
        if (m_farPlane != clampedFarPlane)
        {
            m_farPlane = clampedFarPlane;
            MarkDirty();
        }
    }

    render::Texture *IblCaptureComponent::EnsureCaptureTexture()
    {
        if (m_captureTexture && m_captureTexture->GetWidth() == m_resolution && m_captureTexture->GetHeight() == m_resolution)
        {
            return m_captureTexture.get();
        }

        m_captureTexture.reset(render::Texture::ColorCubemap(m_resolution, m_resolution));
        return m_captureTexture.get();
    }

    glm::mat4 IblCaptureComponent::GetVolumeTransform() const
    {
        const auto *owner = GetOwner();
        const glm::mat4 ownerTransform = owner ? owner->GetWorldTransform() : glm::mat4(1.0f);
        return ownerTransform * glm::scale(glm::mat4(1.0f), m_size);
    }

    IblCaptureVolume IblCaptureComponent::BuildCaptureVolume() const
    {
        const glm::mat4 volumeTransform = GetVolumeTransform();
        glm::vec3 minBounds(std::numeric_limits<float>::max());
        glm::vec3 maxBounds(-std::numeric_limits<float>::max());

        for (int cornerIndex = 0; cornerIndex < 8; ++cornerIndex)
        {
            const glm::vec3 localCorner(
                (cornerIndex & 1) ? 0.5f : -0.5f,
                (cornerIndex & 2) ? 0.5f : -0.5f,
                (cornerIndex & 4) ? 0.5f : -0.5f);
            ExpandBounds(minBounds, maxBounds, glm::vec3(volumeTransform * glm::vec4(localCorner, 1.0f)));
        }

        return IblCaptureVolume{
            .origin = minBounds,
            .size = glm::max(maxBounds - minBounds, glm::vec3(0.0001f)),
            .environmentMapTexture = m_captureTexture.get(),
            .intensity = m_intensity,
            .blendDistance = m_blendDistance,
        };
    }

    std::vector<Property> IblCaptureComponent::Serialize() const
    {
        return {
            {"Size", PropertyType::Vec3, SerializeVec3(m_size)},
            {"Intensity", PropertyType::Float, std::to_string(m_intensity)},
            {"Blend Distance", PropertyType::Float, std::to_string(m_blendDistance)},
            {"Resolution", PropertyType::Int, std::to_string(m_resolution)},
            {"Far Plane", PropertyType::Float, std::to_string(m_farPlane)},
            {"Dirty", PropertyType::Bool, m_dirty ? "true" : "false"},
        };
    }

    void IblCaptureComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Size")
            {
                SetSize(ParseVec3(property.value, m_size));
            }
            else if (property.name == "Intensity")
            {
                SetIntensity(std::stof(property.value));
            }
            else if (property.name == "Blend Distance")
            {
                SetBlendDistance(std::stof(property.value));
            }
            else if (property.name == "Resolution")
            {
                SetResolution(std::stoi(property.value));
            }
            else if (property.name == "Far Plane")
            {
                SetFarPlane(std::stof(property.value));
            }
            else if (property.name == "Dirty")
            {
                m_dirty = property.value == "true";
            }
        }
    }

    void IblCaptureComponent::Update(float deltaTime)
    {
        (void)deltaTime;

        if (!IsEnabled() || !m_captureTexture)
        {
            return;
        }

        auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        if (!scene)
        {
            return;
        }

        scene->AddIblCaptureVolume(BuildCaptureVolume());
    }
}
