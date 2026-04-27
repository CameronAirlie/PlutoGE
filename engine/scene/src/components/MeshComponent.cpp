#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/Entity.h"

#include "PlutoGE/render/Renderer.h"

#include "PlutoGE/core/Engine.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

namespace PlutoGE::scene
{
    void MeshComponent::Update(float deltaTime)
    {
        if (m_mesh && m_material)
        {
            auto entity = GetOwner();
            glm::mat4 modelMatrix = glm::mat4(1.0f);

            modelMatrix = glm::translate(modelMatrix, entity->GetPosition());
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entity->GetRotation().x), glm::vec3(1.0f, 0.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entity->GetRotation().y), glm::vec3(0.0f, 1.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entity->GetRotation().z), glm::vec3(0.0f, 0.0f, 1.0f));
            modelMatrix = glm::scale(modelMatrix, entity->GetScale());

            auto &renderer = PlutoGE::core::Engine::GetInstance().GetRenderer();
            render::RenderCommand command;
            command.model = modelMatrix;
            command.material = m_material;
            command.mesh = m_mesh;

            renderer.SubmitRenderCommand(command);
        };
    }
}