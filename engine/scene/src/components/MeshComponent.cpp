#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/Entity.h"

#include "PlutoGE/render/Renderer.h"

#include "PlutoGE/core/Engine.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

namespace PlutoGE::scene
{
    std::vector<Property> MeshComponent::Serialize() const
    {
        return {};
    }

    void MeshComponent::Deserialize(const std::vector<Property> &properties)
    {
        (void)properties;
    }

    void MeshComponent::Update(float deltaTime)
    {
        if (m_mesh)
        {
            auto entity = GetOwner();
            glm::mat4 modelMatrix = glm::mat4(1.0f);

            modelMatrix = glm::translate(modelMatrix, entity->GetWorldPosition());
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entity->GetWorldRotation().x), glm::vec3(1.0f, 0.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entity->GetWorldRotation().y), glm::vec3(0.0f, 1.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entity->GetWorldRotation().z), glm::vec3(0.0f, 0.0f, 1.0f));
            modelMatrix = glm::scale(modelMatrix, entity->GetScale());

            auto &renderer = PlutoGE::core::Engine::GetInstance().GetRenderer();
            const size_t submeshCount = std::max<size_t>(m_mesh->GetSubmeshCount(), 1);
            for (size_t submeshIndex = 0; submeshIndex < submeshCount; ++submeshIndex)
            {
                auto *material = GetMaterialForSubmesh(submeshIndex);
                if (!material)
                {
                    continue;
                }

                render::RenderCommand command;
                command.model = modelMatrix;
                command.material = material;
                command.mesh = m_mesh;
                command.submeshIndex = static_cast<uint32_t>(submeshIndex);

                renderer.SubmitRenderCommand(command);
            }
        };
    }
}