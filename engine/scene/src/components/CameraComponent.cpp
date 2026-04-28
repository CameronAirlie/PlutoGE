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
            glm::mat4 viewMatrix = glm::mat4(1.0f);

            // Calculate the view matrix based on the entity's world position and rotation
            viewMatrix = glm::translate(viewMatrix, -entity->GetWorldPosition());
            auto worldRotation = entity->GetWorldRotation();
            viewMatrix = glm::rotate(viewMatrix, glm::radians(-worldRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            viewMatrix = glm::rotate(viewMatrix, glm::radians(-worldRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            viewMatrix = glm::rotate(viewMatrix, glm::radians(-worldRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

            m_camera->SetViewMatrix(viewMatrix);
            m_camera->GetCameraData().position = entity->GetWorldPosition();
        }
    }
}