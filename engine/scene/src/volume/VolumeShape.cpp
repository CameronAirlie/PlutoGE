#include "PlutoGE/scene/volume/VolumeShape.h"

#include <algorithm>

namespace PlutoGE::scene::volume
{
    BoxShape::BoxShape(const glm::vec3 &centre, const glm::vec3 &halfExtents, const glm::quat &rotation)
        : m_centre(centre), m_halfExtents(glm::abs(halfExtents)), m_inverseRotation(glm::inverse(glm::normalize(rotation)))
    {
        // The turned box's axis-aligned half size: each world axis takes the box axes' projections onto it.
        const glm::mat3 axes = glm::mat3_cast(glm::normalize(rotation));
        m_reach = glm::abs(axes[0]) * m_halfExtents.x + glm::abs(axes[1]) * m_halfExtents.y + glm::abs(axes[2]) * m_halfExtents.z;
    }

    float BoxShape::Distance(const glm::vec3 &position) const
    {
        const glm::vec3 local = m_inverseRotation * (position - m_centre);
        const glm::vec3 q = glm::abs(local) - m_halfExtents;
        return glm::length(glm::max(q, glm::vec3(0.0f))) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
    }

    float CapsuleShape::Distance(const glm::vec3 &position) const
    {
        const glm::vec3 segment = m_b - m_a;
        const float lengthSquared = glm::dot(segment, segment);
        const float t = lengthSquared > 0.0f ? std::clamp(glm::dot(position - m_a, segment) / lengthSquared, 0.0f, 1.0f) : 0.0f;
        return glm::length(position - (m_a + segment * t)) - m_radius;
    }
}
