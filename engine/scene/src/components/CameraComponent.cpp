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
}