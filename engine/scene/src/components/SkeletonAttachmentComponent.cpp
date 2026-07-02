#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <algorithm>
#include <glm/gtc/matrix_inverse.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

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

            // Entity builds its local transform as Rx * Ry * Rz, so extract
            // the angles using that same convention. glm::eulerAngles uses a
            // different convention and does not reconstruct the source
            // matrix, which made attached entities wobble as bones rotated.
            float rotationX = 0.0f;
            float rotationY = 0.0f;
            float rotationZ = 0.0f;
            glm::extractEulerAngleXYZ(rotationMatrix, rotationX, rotationY, rotationZ);
            result.rotation = glm::degrees(glm::vec3(rotationX, rotationY, rotationZ));
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

        int FindJointIndex(const render::Skeleton &skeleton, int nodeIndex, const std::string &jointName)
        {
            for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
            {
                if (skeleton.joints[jointIndex].nodeIndex == nodeIndex)
                {
                    return static_cast<int>(jointIndex);
                }
            }

            if (!jointName.empty())
            {
                for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
                {
                    if (skeleton.joints[jointIndex].name == jointName)
                    {
                        return static_cast<int>(jointIndex);
                    }
                }
            }

            return -1;
        }

        glm::mat4 ComputeJointMeshMatrix(render::Mesh &mesh,
                                         AnimationComponent *animationComponent,
                                         int nodeIndex,
                                         const std::string &jointName)
        {
            const auto &nodes = mesh.GetAnimationNodes();
            const auto &skeleton = mesh.GetSkeleton();
            const int jointIndex = FindJointIndex(skeleton, nodeIndex, jointName);

            if (jointIndex >= 0)
            {
                const auto &joint = skeleton.joints[static_cast<size_t>(jointIndex)];
                if (animationComponent)
                {
                    // Use the exact skinning pose consumed by rendering. A
                    // skin matrix is boneMesh * inverseBind, so removing the
                    // inverse bind recovers the animated bone transform in
                    // mesh space, including retargeting and root correction.
                    const auto &jointMatrices = animationComponent->GetJointMatrices(skeleton, nodes);
                    if (jointIndex < static_cast<int>(jointMatrices.size()))
                    {
                        return jointMatrices[static_cast<size_t>(jointIndex)] * glm::inverse(joint.inverseBindMatrix);
                    }
                }

                if (nodeIndex >= 0 && nodeIndex < static_cast<int>(nodes.size()))
                {
                    return joint.inverseRootMatrix * ComputeAnimationNodeBindMatrix(nodes, nodeIndex);
                }

                return glm::inverse(joint.inverseBindMatrix);
            }

            return animationComponent
                       ? animationComponent->GetNodeMatrix(nodes, nodeIndex)
                       : ComputeAnimationNodeBindMatrix(nodes, nodeIndex);
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
        const glm::mat4 jointMeshMatrix = ComputeJointMeshMatrix(*mesh, animationComponent, m_targetNodeIndex, m_jointName);

        glm::mat4 localTransform(1.0f);
        auto *parent = owner->GetParent();
        auto *parentAttachment = parent ? parent->GetComponent<SkeletonAttachmentComponent>() : nullptr;
        if (parentAttachment && parentAttachment->FindSourceMeshComponent() == sourceMeshComponent)
        {
            // Derive the local pose directly from both bone transforms. This
            // is independent of entity update order and avoids a one-frame
            // rotation/translation feedback through the parent's old world
            // transform.
            const glm::mat4 parentJointMeshMatrix = ComputeJointMeshMatrix(
                *mesh,
                animationComponent,
                parentAttachment->GetTargetNodeIndex(),
                parentAttachment->GetJointName());
            localTransform = glm::inverse(parentJointMeshMatrix) * jointMeshMatrix;
        }
        else
        {
            const glm::mat4 targetWorld = sourceMeshEntity->GetWorldTransform() *
                                          sourceMeshComponent->GetMeshOffsetTransform() *
                                          jointMeshMatrix;
            const glm::mat4 parentWorld = parent ? parent->GetWorldTransform() : glm::mat4(1.0f);
            localTransform = glm::inverse(parentWorld) * targetWorld;
        }

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
