#pragma once
#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/BakedProbeVolume.h"
#include <cstdint>

namespace PlutoGE::scene
{
    // Oriented bake bounds follow the owner and its parent transforms.
    // Moving the bounds invalidates the bake; baked radiance belongs to static geometry.
    class AmbientLightVolumeComponent : public TypedComponent<AmbientLightVolumeComponent>
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        std::vector<Property> SerializeEditableProperties() const;
        void Deserialize(const std::vector<Property> &properties) override;
        void SetSize(glm::vec3 size);
        glm::vec3 GetSize() const { return m_size; }
        glm::mat4 GetVolumeTransform() const;
        BakedProbeVolume GetGridLayout() const;
        void GetWorldBounds(glm::vec3 &origin, glm::vec3 &size) const;
        float GetProbeSpacing() const { return m_spacing; }
        float GetSurfaceClearance() const { return m_clearance; }
        int GetProbeBudget() const { return m_budget; }
        int GetDirectionCount() const { return m_directions; }
        void Configure(float spacing, float clearance, int budget, int directions);
        // Overlapping volumes are sampled most specific first: higher priority,
        // then smaller volume. Each fades into the next over its blend distance.
        int GetPriority() const { return m_priority; }
        void SetPriority(int priority);
        float GetBlendDistance() const { return m_blendDistance; }
        void SetBlendDistance(float distance);
        bool HasUsableBake() const;
        const BakedProbeVolume &GetBakedVolume() const { return m_baked; }
        void SetBakedVolume(BakedProbeVolume volume);
        void ClearBake();
        std::uint64_t GetRevision() const { return m_revision; }
    private:
        glm::vec3 m_size{10};
        float m_spacing = 1;
        float m_clearance = .02f;
        int m_budget = 4096;
        int m_directions = 128;
        int m_priority = 0;
        float m_blendDistance = 1.0f;
        std::uint64_t m_revision = 0;
        BakedProbeVolume m_baked;
    };
}
