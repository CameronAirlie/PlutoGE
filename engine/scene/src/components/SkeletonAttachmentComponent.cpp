#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <algorithm>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

namespace PlutoGE::scene
{
    namespace
    {
        struct DecomposedTransform
        {
            glm::vec3 position{0.0f};
            glm::vec3 rotation{0.0f};
            glm::vec3 scale{1.0f};
        };

        DecomposedTransform DecomposeTransform(const glm::mat4 &transform)
        {
            DecomposedTransform result;
            result.position = glm::vec3(transform[3]);

            glm::vec3 basisX(transform[0]);
            glm::vec3 basisY(transform[1]);
            glm::vec3 basisZ(transform[2]);
            result.scale = glm::vec3(glm::length(basisX), glm::length(basisY), glm::length(basisZ));

            constexpr float epsilon = 0.000001f;
            if (result.scale.x <= epsilon || result.scale.y <= epsilon || result.scale.z <= epsilon)
            {
                return result;
            }

            basisX /= result.scale.x;
            basisY /= result.scale.y;
            basisZ /= result.scale.z;

            if (glm::dot(glm::cross(basisX, basisY), basisZ) < 0.0f)
            {
                result.scale.x = -result.scale.x;
                basisX = -basisX;
            }

            glm::mat4 rotationMatrix(1.0f);
            rotationMatrix[0] = glm::vec4(glm::normalize(basisX), 0.0f);
            rotationMatrix[1] = glm::vec4(glm::normalize(basisY), 0.0f);
            rotationMatrix[2] = glm::vec4(glm::normalize(basisZ), 0.0f);

            const glm::quat rotation = glm::normalize(glm::quat_cast(glm::mat3(rotationMatrix)));
            result.rotation = glm::degrees(glm::eulerAngles(rotation));
            return result;
        }

        AnimationComponent *FindAnimationComponent(Entity *entity)
        {
            for (auto *current = entity; current != nullptr; current = current->GetParent())
            {
                if (auto *animationComponent = current->GetComponent<AnimationComponent>())
                {
                    return animationComponent;
                }
            }

            return nullptr;
        }

        glm::mat4 ComputeAnimationNodeBindMatrix(const std::vector<render::AnimationNode> &nodes, int nodeIndex)
        {
            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size()))
            {
                return glm::mat4(1.0f);
            }

            std::vector<int> chain;
            for (int currentNodeIndex = nodeIndex;
                 currentNodeIndex >= 0 && currentNodeIndex < static_cast<int>(nodes.size());
                 currentNodeIndex = nodes[static_cast<size_t>(currentNodeIndex)].parentNodeIndex)
            {
                chain.push_back(currentNodeIndex);
            }

            glm::mat4 transform(1.0f);
            for (auto iterator = chain.rbegin(); iterator != chain.rend(); ++iterator)
            {
                transform *= nodes[static_cast<size_t>(*iterator)].localBindTransform;
            }
            return transform;
        }
    }

    SkeletonAttachmentComponent::SkeletonAttachmentComponent(int nodeIndex, std::string jointName)
        : m_targetNodeIndex(nodeIndex), m_jointName(std::move(jointName))
    {
    }

    MeshComponent *SkeletonAttachmentComponent::FindSourceMeshComponent() const
    {
        const auto *owner = GetOwner();
        for (auto *current = owner ? owner->GetParent() : nullptr; current != nullptr; current = current->GetParent())
        {
            if (auto *meshComponent = current->GetComponent<MeshComponent>())
            {
                if (meshComponent->GetMesh() && meshComponent->GetMesh()->HasSkeleton())
                {
                    return meshComponent;
                }
            }
        }

        return nullptr;
    }

    void SkeletonAttachmentComponent::Update(float)
    {
        auto *owner = GetOwner();
        auto *sourceMeshComponent = FindSourceMeshComponent();
        auto *sourceMeshEntity = sourceMeshComponent ? sourceMeshComponent->GetOwner() : nullptr;
        auto *mesh = sourceMeshComponent ? sourceMeshComponent->GetMesh() : nullptr;
        if (!owner || !sourceMeshEntity || !mesh || m_targetNodeIndex < 0)
        {
            return;
        }

        const auto &nodes = mesh->GetAnimationNodes();
        if (m_targetNodeIndex >= static_cast<int>(nodes.size()))
        {
            return;
        }

        auto *animationComponent = FindAnimationComponent(sourceMeshEntity);
        const glm::mat4 nodeMatrix = animationComponent
                                         ? animationComponent->GetNodeMatrix(nodes, m_targetNodeIndex)
                                         : ComputeAnimationNodeBindMatrix(nodes, m_targetNodeIndex);
        const glm::mat4 targetWorld = sourceMeshEntity->GetWorldTransform() * nodeMatrix;
        const glm::mat4 parentWorld = owner->GetParent() ? owner->GetParent()->GetWorldTransform() : glm::mat4(1.0f);
        const glm::mat4 localTransform = glm::inverse(parentWorld) * targetWorld;
        const auto decomposed = DecomposeTransform(localTransform);
        owner->SetPosition(decomposed.position);
        owner->SetRotation(decomposed.rotation);
        owner->SetScale(decomposed.scale);
    }

    std::vector<Property> SkeletonAttachmentComponent::Serialize() const
    {
        return {
            {"TargetNodeIndex", PropertyType::Int, std::to_string(m_targetNodeIndex)},
            {"JointName", PropertyType::String, m_jointName},
        };
    }

    void SkeletonAttachmentComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "TargetNodeIndex")
            {
                m_targetNodeIndex = std::stoi(property.value);
            }
            else if (property.name == "JointName")
            {
                m_jointName = property.value;
            }
        }
    }
}
