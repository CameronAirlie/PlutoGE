#include "PlutoGE/scene/volume/VolumeShape.h"

#include <algorithm>
#include <limits>

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

    TransformedShape::TransformedShape(const VolumeShape &shape, const glm::mat4 &toShape) : m_shape(shape), m_toShape(toShape)
    {
        // A shape-space metre is 1 / (axis scale) metres here; the largest axis scale gives the safe (smallest) ratio.
        const float largestScale = std::max({glm::length(glm::vec3(toShape[0])), glm::length(glm::vec3(toShape[1])),
                                             glm::length(glm::vec3(toShape[2]))});
        m_distanceScale = largestScale > 0.0f ? 1.0f / largestScale : 1.0f;
        // The inner bounds' corners carried back into this space.
        const glm::mat4 fromShape = glm::inverse(toShape);
        const glm::vec3 low = shape.BoundsMin(), high = shape.BoundsMax();
        m_boundsMin = glm::vec3(std::numeric_limits<float>::max());
        m_boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
        for (int corner = 0; corner < 8; ++corner)
        {
            const glm::vec3 point((corner & 1) ? high.x : low.x, (corner & 2) ? high.y : low.y, (corner & 4) ? high.z : low.z);
            const glm::vec3 mapped = glm::vec3(fromShape * glm::vec4(point, 1.0f));
            m_boundsMin = glm::min(m_boundsMin, mapped);
            m_boundsMax = glm::max(m_boundsMax, mapped);
        }
    }

    float TransformedShape::Distance(const glm::vec3 &position) const
    {
        return m_shape.Distance(glm::vec3(m_toShape * glm::vec4(position, 1.0f))) * m_distanceScale;
    }

    float CapsuleShape::Distance(const glm::vec3 &position) const
    {
        const glm::vec3 segment = m_b - m_a;
        const float lengthSquared = glm::dot(segment, segment);
        const float t = lengthSquared > 0.0f ? std::clamp(glm::dot(position - m_a, segment) / lengthSquared, 0.0f, 1.0f) : 0.0f;
        return glm::length(position - (m_a + segment * t)) - m_radius;
    }
}
