#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/Entity.h"

namespace PlutoGE::scene
{
    void LightComponent::Update(float deltaTime)
    {
        if (auto *owner = GetOwner())
        {
            m_config.position = owner->GetWorldPosition();

            // Get direction from world rotation
            // convert rotation from degrees to radians
            glm::vec3 rotationRadians = glm::radians(owner->GetWorldRotation());
            // Calculate direction vector from rotation (assuming forward is -Z)
            glm::vec3 direction;
            direction.x = -sin(rotationRadians.y) * cos(rotationRadians.x);
            direction.y = sin(rotationRadians.x);
            direction.z = -cos(rotationRadians.y) * cos(rotationRadians.x);
            m_config.direction = glm::normalize(direction);
        }
    }
}