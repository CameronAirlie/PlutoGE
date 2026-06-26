#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <string>
#include <vector>

namespace PlutoGE::scene
{
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

    private:
        MeshComponent *FindSourceMeshComponent() const;

        int m_targetNodeIndex = -1;
        std::string m_jointName;
    };
}
