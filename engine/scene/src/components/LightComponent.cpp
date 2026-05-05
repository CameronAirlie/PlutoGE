#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/render/Texture.h"

namespace PlutoGE::scene
{
    namespace
    {
        render::Texture *CreateShadowTextureForLight(const Light &light)
        {
            if (light.type == LightType::Point)
            {
                return render::Texture::DepthCubemap(1024, 1024);
            }

            return render::Texture::DepthTexture(1024, 1024);
        }
    }

    void LightComponent::Initialize()
    {
        if (m_config.castsShadows && !m_config.shadowMap)
        {
            m_config.shadowMap = CreateShadowTextureForLight(m_config);
        }
    }

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

        if (m_config.castsShadows && !m_config.shadowMap)
        {
            m_config.shadowMap = CreateShadowTextureForLight(m_config);
        }
    }
}