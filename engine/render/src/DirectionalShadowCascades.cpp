#include "DirectionalShadowCascades.h"

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    DirectionalShadowProjection BuildDirectionalShadowProjection(const CameraData &camera,
                                                                 const BasicLighting &lighting,
                                                                 float cascadeNear,
                                                                 float shadowDistance,
                                                                 float casterDistance,
                                                                 std::uint32_t shadowResolution)
    {
        const glm::mat4 inverseView = glm::inverse(camera.view);
        const glm::vec3 cameraPosition = glm::vec3(inverseView[3]);
        const glm::vec3 right = glm::normalize(glm::vec3(inverseView[0]));
        const glm::vec3 up = glm::normalize(glm::vec3(inverseView[1]));
        const glm::vec3 forward = -glm::normalize(glm::vec3(inverseView[2]));
        const float nearDistance = std::max(cascadeNear, camera.nearPlane);
        const float farDistance = std::max(shadowDistance, nearDistance + 0.01f);
        const float projectionX = std::max(std::abs(camera.projection[0][0]), 0.0001f);
        const float projectionY = std::max(std::abs(camera.projection[1][1]), 0.0001f);
        // Use actual camera coverage, matching the legacy cascade fit.
        // Radius quantization and world-anchored snapping still stabilize
        // camera translation and rotation at a fixed projection.
        const float inverseProjectionY = 1.0f / projectionY;
        const float inverseProjectionX = 1.0f / projectionX;

        std::array<glm::vec3, 8> corners{};
        std::size_t cornerIndex = 0;
        for (const float distance : {nearDistance, farDistance})
        {
            const glm::vec3 center = cameraPosition + forward * distance;
            const float coverageDepth = std::abs(camera.projection[3][3]) > 0.5f ? 1.0f : distance;
            const glm::vec3 horizontal = right * (coverageDepth * inverseProjectionX);
            const glm::vec3 vertical = up * (coverageDepth * inverseProjectionY);
            corners[cornerIndex++] = center - horizontal - vertical;
            corners[cornerIndex++] = center + horizontal - vertical;
            corners[cornerIndex++] = center - horizontal + vertical;
            corners[cornerIndex++] = center + horizontal + vertical;
        }

        glm::vec3 center(0.0f);
        for (const auto &corner : corners)
            center += corner;
        center /= static_cast<float>(corners.size());
        float radius = 0.0f;
        for (const auto &corner : corners)
            radius = std::max(radius, glm::length(corner - center));
        radius = std::ceil(std::max(radius, 0.1f) * 16.0f) / 16.0f;
        glm::vec3 lightDirection = lighting.directionalDirection;
        if (glm::dot(lightDirection, lightDirection) < 0.000001f)
            lightDirection = {0.4f, -0.8f, 0.3f};
        lightDirection = glm::normalize(lightDirection);
        const glm::vec3 lightUp = std::abs(lightDirection.y) > 0.98f
                                      ? glm::vec3(0.0f, 0.0f, 1.0f)
                                      : glm::vec3(0.0f, 1.0f, 0.0f);
        const float lightOffset = std::max(casterDistance, farDistance) + radius + 1.0f;
        const glm::mat4 lightView = glm::lookAtRH(center - lightDirection * lightOffset, center, lightUp);
        const glm::vec3 lightSpaceCenter = glm::vec3(lightView * glm::vec4(center, 1.0f));
        // Tent support plus half a texel for world-grid snapping.
        const float guardTexels = std::clamp(lighting.shadowSoftness, 0.0f, 4.0f) + 1.5f;
        const float guard = guardTexels * (radius * 2.0f / std::max(shadowResolution, 1u)) + 0.01f;
        glm::vec3 minimum = lightSpaceCenter - glm::vec3(radius + guard, radius + guard, radius);
        glm::vec3 maximum = lightSpaceCenter + glm::vec3(radius + guard, radius + guard, radius);
        // The receiver slice alone is not a sufficient shadow-caster
        // volume. A directional-light caster may be outside the camera
        // slice on the light-facing side while its projection still
        // reaches a receiver inside it. Extrude toward the light (the
        // negative light direction), and include the full caster sphere.
        // Extending minimum.z instead grows the volume downstream and
        // makes valid casters cross the near plane as the camera rotates.
        const float casterExtrusion = std::max(casterDistance, farDistance);
        const glm::vec3 extrudedCenter = center - lightDirection * casterExtrusion;
        const glm::vec3 lightSpaceExtrudedCenter =
            glm::vec3(lightView * glm::vec4(extrudedCenter, 1.0f));
        minimum.z = std::min(minimum.z, lightSpaceExtrudedCenter.z - radius - 0.01f);
        maximum.z = std::max(maximum.z, lightSpaceExtrudedCenter.z + radius + 0.01f);
        const glm::vec2 extent(maximum.x - minimum.x, maximum.y - minimum.y);
        const glm::vec2 texelSize = extent / static_cast<float>(std::max(shadowResolution, 1u));
        // Anchor the grid to absolute world zero, not the moving cascade
        // centre. This is the key phase-stability rule used by GLSL CSM.
        const glm::vec2 worldAnchor = glm::vec2(lightView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        const glm::vec2 snappedMinimum = worldAnchor -
                                         glm::round((worldAnchor - glm::vec2(minimum)) / texelSize) * texelSize;
        const glm::vec2 snapOffset = snappedMinimum - glm::vec2(minimum);
        minimum.x += snapOffset.x;
        maximum.x += snapOffset.x;
        minimum.y += snapOffset.y;
        maximum.y += snapOffset.y;
        const float nearPlane = std::max(-maximum.z, 0.01f);
        const float farPlane = std::max(-minimum.z, nearPlane + 0.01f);
        DirectionalShadowProjection projection;
        projection.matrix = glm::orthoRH_ZO(minimum.x, maximum.x, minimum.y, maximum.y, nearPlane, farPlane) * lightView;
        projection.worldTexelSize = std::max(extent.x, extent.y) / static_cast<float>(std::max(shadowResolution, 1u));
        projection.depthRange = farPlane - nearPlane;
        projection.resolution = shadowResolution;
        projection.lightDirection = lightDirection;
        projection.sliceCorners = corners;
        projection.casterExtrusion = -lightDirection * casterExtrusion;
        // The guard less the half texel snapping may already have spent of it.
        projection.guardClip = std::max(guard - 0.5f * texelSize.x, 0.0f) / (radius + guard);
        return projection;
    }

    bool CoversDirectionalShadowSlice(const DirectionalShadowProjection &held,
                                      const DirectionalShadowProjection &fresh) noexcept
    {
        // A different light, resolution or slice size is a different texel grid: sampling would change.
        if (held.resolution != fresh.resolution || held.worldTexelSize != fresh.worldTexelSize ||
            held.lightDirection != fresh.lightDirection || held.guardClip != fresh.guardClip)
            return false;
        // Orthographic with zero-to-one depth (BuildDirectionalShadowProjection): w is 1.
        const float edge = 1.0f - fresh.guardClip;
        for (const auto &corner : fresh.sliceCorners)
        {
            const glm::vec4 receiver = held.matrix * glm::vec4(corner, 1.0f);
            if (std::abs(receiver.x) > edge || std::abs(receiver.y) > edge || receiver.z < 0.0f || receiver.z > 1.0f)
                return false;
            // Toward the light only depth changes; casters there must not cross the near plane.
            if ((held.matrix * glm::vec4(corner + fresh.casterExtrusion, 1.0f)).z < 0.0f)
                return false;
        }
        return true;
    }

    bool DirectionalShadowCascadeRetention::Retain(std::uint32_t cascade, DirectionalShadowProjection &projection)
    {
        if (cascade >= MaxCascades)
            return false;
        auto &held = m_held[cascade];
        if (held && CoversDirectionalShadowSlice(*held, projection))
        {
            projection = *held;
            return true;
        }
        held = projection;
        return false;
    }
}
