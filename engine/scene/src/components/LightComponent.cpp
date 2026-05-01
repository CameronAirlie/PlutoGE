#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/Entity.h"

namespace PlutoGE::scene
{
    void LightComponent::Update(float deltaTime)
    {
        if (auto *owner = GetOwner())
        {
            m_config.position = owner->GetWorldPosition();
        }
    }
}