#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>

namespace PlutoGE::scene
{
    enum class ColliderShape
    {
        Box,
        Sphere,
        Capsule,
        Terrain,
        Mesh,
    };

    struct ColliderComponentConfig
    {
        ColliderShape shape = ColliderShape::Box;
        glm::vec3 center{0.0f};
        glm::vec3 size{1.0f, 1.0f, 1.0f};
        float radius = 0.5f;
        float height = 2.0f;
        bool isTrigger = false;
    };

    class ColliderComponent : public TypedComponent<ColliderComponent>
    {
    public:
        explicit ColliderComponent(const ColliderComponentConfig &config = {});
        ~ColliderComponent() override = default;

        void Update(float deltaTime) override;

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        ColliderShape GetShape() const { return m_config.shape; }
        void SetShape(ColliderShape shape) { m_config.shape = shape; }

        const glm::vec3 &GetCenter() const { return m_config.center; }
        void SetCenter(const glm::vec3 &center) { m_config.center = center; }

        const glm::vec3 &GetSize() const { return m_config.size; }
        void SetSize(const glm::vec3 &size);

        float GetRadius() const { return m_config.radius; }
        void SetRadius(float radius);

        float GetHeight() const { return m_config.height; }
        void SetHeight(float height);

        bool IsTrigger() const { return m_config.isTrigger; }
        void SetTrigger(bool isTrigger) { m_config.isTrigger = isTrigger; }

        glm::vec3 GetScaledCenter(const glm::vec3 &objectScale) const;
        glm::vec3 GetScaledSize(const glm::vec3 &objectScale) const;
        float GetScaledRadius(const glm::vec3 &objectScale) const;
        float GetScaledHeight(const glm::vec3 &objectScale) const;

        const ColliderComponentConfig &GetConfig() const { return m_config; }

    private:
        ColliderComponentConfig m_config;
    };
}
