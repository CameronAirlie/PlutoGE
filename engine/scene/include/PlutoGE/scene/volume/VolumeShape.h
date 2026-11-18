#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace PlutoGE::scene::volume
{
    // A solid as a signed distance (negative inside), for edits and stamps. New kinds (a mesh's distance field, a
    // spline tunnel, noise-displaced rock) implement this; edits only need the distance and the bounds.
    class VolumeShape
    {
    public:
        virtual ~VolumeShape() = default;
        // Signed distance in metres from a position in the volume's space; an under-estimate is fine (edits read it near
        // the surface), an over-estimate is not.
        [[nodiscard]] virtual float Distance(const glm::vec3 &position) const = 0;
        // A box holding everything the shape encloses.
        [[nodiscard]] virtual glm::vec3 BoundsMin() const = 0;
        [[nodiscard]] virtual glm::vec3 BoundsMax() const = 0;
    };

    class SphereShape final : public VolumeShape
    {
    public:
        SphereShape(const glm::vec3 &centre, float radius) : m_centre(centre), m_radius(radius) {}
        [[nodiscard]] float Distance(const glm::vec3 &position) const override { return glm::length(position - m_centre) - m_radius; }
        [[nodiscard]] glm::vec3 BoundsMin() const override { return m_centre - m_radius; }
        [[nodiscard]] glm::vec3 BoundsMax() const override { return m_centre + m_radius; }

    private:
        glm::vec3 m_centre;
        float m_radius;
    };

    // A box, optionally turned.
    class BoxShape final : public VolumeShape
    {
    public:
        BoxShape(const glm::vec3 &centre, const glm::vec3 &halfExtents, const glm::quat &rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        [[nodiscard]] float Distance(const glm::vec3 &position) const override;
        [[nodiscard]] glm::vec3 BoundsMin() const override { return m_centre - m_reach; }
        [[nodiscard]] glm::vec3 BoundsMax() const override { return m_centre + m_reach; }

    private:
        glm::vec3 m_centre;
        glm::vec3 m_halfExtents;
        glm::quat m_inverseRotation;
        glm::vec3 m_reach; // half size of the turned box's axis-aligned bounds
    };

    // Every point within `radius` of the segment a..b: a tunnel or a pickaxe swing.
    class CapsuleShape final : public VolumeShape
    {
    public:
        CapsuleShape(const glm::vec3 &a, const glm::vec3 &b, float radius) : m_a(a), m_b(b), m_radius(radius) {}
        [[nodiscard]] float Distance(const glm::vec3 &position) const override;
        [[nodiscard]] glm::vec3 BoundsMin() const override { return glm::min(m_a, m_b) - m_radius; }
        [[nodiscard]] glm::vec3 BoundsMax() const override { return glm::max(m_a, m_b) + m_radius; }

    private:
        glm::vec3 m_a;
        glm::vec3 m_b;
        float m_radius;
    };

    // Another shape seen from a different space: a world-space edit applied to a volume in a terrain's local space.
    // `toShape` maps this space's positions into the inner shape's space. Distances stay exact under rotation and
    // uniform scale; under non-uniform scale they are divided by the largest axis scale, an under-estimate (allowed).
    // Holds a reference: the inner shape must outlive it.
    class TransformedShape final : public VolumeShape
    {
    public:
        TransformedShape(const VolumeShape &shape, const glm::mat4 &toShape);
        [[nodiscard]] float Distance(const glm::vec3 &position) const override;
        [[nodiscard]] glm::vec3 BoundsMin() const override { return m_boundsMin; }
        [[nodiscard]] glm::vec3 BoundsMax() const override { return m_boundsMax; }

    private:
        const VolumeShape &m_shape;
        glm::mat4 m_toShape;
        float m_distanceScale; // this space's metres per shape-space metre
        glm::vec3 m_boundsMin;
        glm::vec3 m_boundsMax;
    };
}
