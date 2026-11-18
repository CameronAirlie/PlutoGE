#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/volume/TerrainVolume.h"

#include <glm/glm.hpp>

#include <functional>
#include <memory>

namespace PlutoGE::scene
{
    class Scene;

    // An authored piece of 3D ground (docs/dev/TERRAIN_VOLUMES_PLAN.md, phase 7): a shape on an entity that carves a
    // cave, a tunnel or a pit into the terrain volumes under it, or builds rock up, or repaints rock. Unlike edits it is
    // not stored: it is part of each volume's base, so moving, resizing or removing the entity changes the ground with
    // it, and saving the scene saves it. Volumes apply stamps in Order (then by entity), after generated caves.
    class VolumeStampComponent : public TypedComponent<VolumeStampComponent>
    {
    public:
        enum class Shape
        {
            Sphere,  // Radius
            Box,     // HalfExtents
            Capsule, // Radius, Length along the entity's Y
        };

        VolumeStampComponent();
        ~VolumeStampComponent() override;
        VolumeStampComponent(const VolumeStampComponent &) = delete;
        VolumeStampComponent &operator=(const VolumeStampComponent &) = delete;

        void Update(float) override {}
        [[nodiscard]] bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        [[nodiscard]] Shape GetShape() const { return m_shape; }
        void SetShape(Shape shape) { m_shape = shape; }
        [[nodiscard]] volume::VolumeEditMode GetMode() const { return m_mode; }
        void SetMode(volume::VolumeEditMode mode) { m_mode = mode; }
        // The terrain layer rock gets (Add and Paint).
        [[nodiscard]] int GetLayer() const { return m_layer; }
        void SetLayer(int layer);
        [[nodiscard]] float GetRadius() const { return m_radius; }
        void SetRadius(float radius);
        [[nodiscard]] glm::vec3 GetHalfExtents() const { return m_halfExtents; }
        void SetHalfExtents(const glm::vec3 &halfExtents);
        [[nodiscard]] float GetLength() const { return m_length; }
        void SetLength(float length);
        [[nodiscard]] int GetOrder() const { return m_order; }
        void SetOrder(int order) { m_order = order; }

        // The shape in the entity's own space (scale and rotation come from its transform).
        [[nodiscard]] std::shared_ptr<const volume::VolumeShape> MakeLocalShape() const;
        [[nodiscard]] static std::shared_ptr<const volume::VolumeShape> MakeShape(Shape shape, float radius, const glm::vec3 &halfExtents,
                                                                                float length);
        // Whether it takes part: enabled, on an active entity.
        [[nodiscard]] bool IsLive() const;

        // The live stamps of a scene, in the order volumes apply them.
        static void ForEach(const Scene &scene, const std::function<void(const VolumeStampComponent &)> &visit);

    private:
        Shape m_shape = Shape::Sphere;
        volume::VolumeEditMode m_mode = volume::VolumeEditMode::Subtract;
        int m_layer = 0;
        float m_radius = 3.0f;
        glm::vec3 m_halfExtents{2.0f};
        float m_length = 6.0f;
        int m_order = 0;
    };
}
