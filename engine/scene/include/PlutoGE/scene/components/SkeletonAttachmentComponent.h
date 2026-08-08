#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    class AnimationComponent;
    class MeshComponent;

    class SkeletonAttachmentComponent : public TypedComponent<SkeletonAttachmentComponent>
    {
    public:
        SkeletonAttachmentComponent() = default;
        explicit SkeletonAttachmentComponent(int nodeIndex, std::string jointName = {});
        ~SkeletonAttachmentComponent() override = default;

        void Update(float deltaTime) override;

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void SetTargetNodeIndex(int nodeIndex) { m_targetNodeIndex = nodeIndex; }
        int GetTargetNodeIndex() const { return m_targetNodeIndex; }
        void SetJointName(std::string jointName) { m_jointName = std::move(jointName); }
        const std::string &GetJointName() const { return m_jointName; }
        void BindSource(MeshComponent *source, int jointIndex);

    private:
        MeshComponent *FindSourceMeshComponent() const;
        AnimationComponent *FindAnimationComponent() const;

        int m_targetNodeIndex = -1;
        std::string m_jointName;
        MeshComponent *m_cachedSourceMeshComponent = nullptr;
        AnimationComponent *m_cachedAnimationComponent = nullptr;
        const void *m_cachedMesh = nullptr;
        int m_cachedJointIndex = -1;
        glm::mat4 m_cachedJointMeshMatrix{1.0f};
        glm::mat4 m_cachedBindMatrix{1.0f};
        uint64_t m_cachedUpdateSequence = 0;
    };
}
