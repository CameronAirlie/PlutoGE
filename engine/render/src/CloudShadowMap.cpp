#include "PlutoGE/render/CloudShadowMap.h"

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/SkyEnvironmentConfig.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        // Lowest world-space point of the volume's unit box.
        float VolumeBottom(const glm::mat4 &worldToLocal)
        {
            const glm::mat4 localToWorld = glm::inverse(worldToLocal);
            const float halfHeight =
                0.5f * (std::abs(localToWorld[0].y) + std::abs(localToWorld[1].y) + std::abs(localToWorld[2].y));
            return localToWorld[3].y - halfHeight;
        }

        // Light direction with its elevation raised to MinimumElevation, azimuth kept.
        glm::vec3 ProjectionDirection(glm::vec3 light)
        {
            light = glm::normalize(light);
            if (light.y >= CloudShadowMap::MinimumElevation)
                return light;
            const glm::vec2 horizontal{light.x, light.z};
            const float length = glm::length(horizontal);
            const glm::vec2 azimuth = length > 0.000001f ? horizontal / length : glm::vec2(1.0f, 0.0f);
            const float scale = std::sqrt(1.0f - CloudShadowMap::MinimumElevation * CloudShadowMap::MinimumElevation);
            return {azimuth.x * scale, CloudShadowMap::MinimumElevation, azimuth.y * scale};
        }
    }

    glm::vec4 CloudShadowMap::SurfaceMapping() const
    {
        return {pass.mapping.x, pass.mapping.y, pass.mapping.z > 0.0f ? 1.0f / pass.mapping.z : 0.0f, pass.mapping.w};
    }

    CloudShadowMap BuildCloudShadowMap(std::span<const BasicPostProcessEffect> effects, const glm::vec3 &cameraPosition,
                                       const glm::vec3 &lightDirection)
    {
        CloudShadowMap result;
        // A light at or below the horizon lights nothing from above the clouds.
        if (!(glm::dot(lightDirection, lightDirection) > 0.000001f) || glm::normalize(lightDirection).y <= 0.0f)
            return result;
        const glm::vec3 light = ProjectionDirection(lightDirection);

        std::size_t count = 0;
        float planeHeight = 0.0f;
        float distance = 0.0f;
        for (auto effect = effects.rbegin(); effect != effects.rend() && count < result.pass.volumes.size(); ++effect)
        {
            if (effect->type != BasicPostProcessEffectType::VolumetricCloud)
                continue;
            // Lanes follow BuildSceneAtmosphere's cloud packet.
            const auto &lanes = effect->parameters;
            const auto &shadow = lanes[CloudShadowLane];
            const float coverage = lanes[0].w, density = lanes[1].w;
            const float extinction = lanes[2].w * std::max(shadow.z, 0.0f);
            if (shadow.x <= 0.5f || coverage <= 0.0f || density <= 0.0f || extinction <= 0.0f || lanes[4].w <= 0.0f)
                continue;
            auto &volume = result.pass.volumes[count];
            volume.worldToLocal = effect->worldToLocal;
            volume.wind = {glm::vec3(lanes[1]), density};
            volume.shape = {coverage, extinction, lanes[4].w, lanes[5].x};
            volume.detail = {lanes[5].y, 0.0f, 0.0f, 0.0f};
            const float bottom = VolumeBottom(effect->worldToLocal);
            planeHeight = count == 0 ? bottom : std::min(planeHeight, bottom);
            distance = std::max(distance, shadow.y);
            ++count;
        }
        if (count == 0)
            return result;

        distance = std::clamp(distance, CloudShadowMap::MinimumDistance, CloudShadowMap::MaximumDistance);
        const float texelSize = 2.0f * distance / PLUTO_SKY_CLOUD_SHADOW_SIZE;
        // Centre on the plane point the light ray from below the camera crosses;
        // a camera above the plane centres on the plane point beneath it.
        const float receiverHeight = std::min(cameraPosition.y, planeHeight);
        const glm::vec2 centre = glm::vec2(cameraPosition.x, cameraPosition.z) +
                                 glm::vec2(light.x, light.z) * ((planeHeight - receiverHeight) / light.y);
        const glm::vec2 origin = glm::floor(centre / texelSize) * texelSize -
                                 glm::vec2(0.5f * PLUTO_SKY_CLOUD_SHADOW_SIZE * texelSize);
        result.enabled = true;
        result.pass.mapping = {origin.x, origin.y, texelSize, planeHeight};
        result.pass.light = {light, static_cast<float>(count)};
        return result;
    }
}
