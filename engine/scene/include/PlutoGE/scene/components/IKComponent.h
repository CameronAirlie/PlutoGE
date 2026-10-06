#pragma once
#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/TwoBoneIK.h"
#include <cstdint>

namespace PlutoGE::scene
{
    class MeshComponent;
    struct IKConstraint
    {
        std::string name = "Arm";
        std::string root, middle, tip;
        uint32_t target = 0, hint = 0;
        float weight = 1, rotationWeight = 0;
        bool enabled = true;
        // Optional animator float multiplier, e.g. fade support-hand IK during reload.
        std::string weightParameter;
    };
    // Persistent authoring data. AnimationComponent resolves it at pose evaluation,
    // avoiding entity update order dependencies and cached pointers to scene entities.
    class IKComponent : public TypedComponent<IKComponent>
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        uint32_t GetMeshEntity() const { return m_meshEntity; }
        void SetMeshEntity(uint32_t id) { m_meshEntity = id; }
        bool GetPreviewInEditor() const { return m_previewInEditor; }
        void SetPreviewInEditor(bool enabled) { m_previewInEditor = enabled; }
        const std::vector<IKConstraint> &GetConstraints() const { return m_constraints; }
        bool SetConstraints(std::vector<IKConstraint> constraints);
        MeshComponent *ResolveMesh() const;
        std::string GetConstraintStatus(size_t index) const;
        std::vector<TwoBoneIKTarget> ResolveTargets(const render::Skeleton &skeleton) const;
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;
    private:
        uint32_t m_meshEntity = 0;
        bool m_previewInEditor = true;
        std::vector<IKConstraint> m_constraints{IKConstraint{}};
    };
}
