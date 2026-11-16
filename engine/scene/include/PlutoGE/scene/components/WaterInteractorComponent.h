#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/water/WaterInteraction.h"

#include <glm/glm.hpp>

#include <vector>

namespace PlutoGE::scene
{
    // Makes an entity disturb water, or tunes how it does (docs/WATER.md). Dynamic rigidbodies disturb water on their
    // own, sized from their collider; add this to a character or kinematic body that should too (its velocity then
    // comes from how far it moved), to change the size or strength of a rigidbody's effects, or to turn some off.
    class WaterInteractorComponent : public TypedComponent<WaterInteractorComponent>
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        // Sphere radius the water sees; 0 sizes it from the entity's collider (0.5 m without one).
        [[nodiscard]] float GetRadius() const { return m_radius; }
        void SetRadius(float radius);
        // Sphere centre relative to the entity, in its local space (a character's pivot is usually at its feet).
        [[nodiscard]] const glm::vec3 &GetCenterOffset() const { return m_centerOffset; }
        void SetCenterOffset(const glm::vec3 &offset) { m_centerOffset = offset; }
        // Multiplies the intensity of every ripple, splash and sound it causes.
        [[nodiscard]] float GetEffectScale() const { return m_effectScale; }
        void SetEffectScale(float scale);
        [[nodiscard]] const water::WaterInteractionChannels &GetChannels() const { return m_channels; }
        void SetChannels(const water::WaterInteractionChannels &channels) { m_channels = channels; }

    private:
        float m_radius = 0.0f;
        glm::vec3 m_centerOffset{0.0f};
        float m_effectScale = 1.0f;
        water::WaterInteractionChannels m_channels;
    };
}
