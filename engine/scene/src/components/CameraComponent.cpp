#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/render/Camera.h"

#include <glm/gtc/matrix_transform.hpp>

namespace PlutoGE::scene
{
    void CameraComponent::Update(float deltaTime)
    {
        if (m_camera)
        {
            auto entity = GetOwner();
            glm::mat4 transform = entity->GetWorldTransform();

            m_camera->GetCameraData(transform, 1, 1);
        }
    }

    render::CameraData CameraComponent::GetCameraData(int width, int height) const
    {
        if (m_camera)
        {
            auto entity = GetOwner();
            glm::mat4 transform = entity->GetWorldTransform();

            return m_camera->GetCameraData(transform, width, height);
        }
        return render::CameraData{}; // Return default camera data if no camera is set
    }

    std::vector<Property> CameraComponent::Serialize() const
    {
        std::vector<Property> properties;
        if (m_camera)
        {
            properties.push_back({"FOV", scene::PropertyType::Float, std::to_string(m_camera->GetFOV())});
            properties.push_back({"NearPlane", scene::PropertyType::Float, std::to_string(m_camera->GetNearPlane())});
            properties.push_back({"FarPlane", scene::PropertyType::Float, std::to_string(m_camera->GetFarPlane())});
        }
        return properties;
    }

    void CameraComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "FOV")
            {
                m_camera->SetFOV(std::stof(property.value));
            }
            else if (property.name == "NearPlane")
            {
                m_camera->SetNearPlane(std::stof(property.value));
            }
            else if (property.name == "FarPlane")
            {
                m_camera->SetFarPlane(std::stof(property.value));
            }
        }
    }
}