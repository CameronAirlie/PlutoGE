#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/scene/DirectionalShadowLighting.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        struct CloudPacket
        {
            render::BasicPostProcessEffect effect;
            float distanceSquared = 0.0f;
        };

        const scene::PhysicalSkyComponent *FindPhysicalSky(const scene::Entity *entity)
        {
            if (!entity || !entity->IsActive())
                return nullptr;
            for (const auto *sky : entity->GetComponents<scene::PhysicalSkyComponent>())
                if (sky && sky->IsEnabled())
                    return sky;
            for (const auto *child : entity->GetChildren())
                if (const auto *sky = FindPhysicalSky(child))
                    return sky;
            return nullptr;
        }

        const scene::PhysicalSkyComponent *FindPhysicalSky(const scene::Scene *scene)
        {
            if (!scene)
                return nullptr;
            for (const auto *root : scene->GetRootEntities())
                if (const auto *sky = FindPhysicalSky(root))
                    return sky;
            return nullptr;
        }

        glm::vec3 AtmosphericSunTransmittance(const scene::PhysicalSkyComponent &sky,
                                              const glm::vec3 &sunDirection)
        {
            const float airMass = 1.0f / std::max(sunDirection.y + 0.075f, 0.04f);
            const glm::vec3 extinction =
                glm::vec3(0.028f, 0.067f, 0.155f) * std::max(sky.GetRayleighStrength(), 0.0f) +
                glm::vec3(0.035f) * std::max(sky.GetMieStrength(), 0.0f) +
                glm::vec3(0.004f, 0.012f, 0.002f) * std::max(sky.GetOzoneStrength(), 0.0f);
            return glm::exp(-extinction * airMass) * glm::max(sky.GetSunColor(), glm::vec3(0.0f));
        }

        void CollectAtmosphere(const scene::Entity *entity, const glm::vec3 &cameraPosition,
                               const render::BasicLighting &lighting,
                               std::vector<render::BasicPostProcessEffect> &effects,
                               std::vector<CloudPacket> &clouds, bool &hasSky)
        {
            if (!entity || !entity->IsActive())
                return;
            if (!hasSky)
                for (const auto *sky : entity->GetComponents<scene::PhysicalSkyComponent>())
                    if (sky && sky->IsEnabled())
                    {
                        render::BasicPostProcessEffect effect{render::BasicPostProcessEffectType::PhysicalSky};
                        // Horizon attenuation can make a valid sun's intensity zero.
                        // Keep its direction so sunset cannot reset the sky to daytime.
                        glm::vec3 sunDirection = -lighting.directionalDirection;
                        if (glm::dot(sunDirection, sunDirection) < 0.000001f)
                            sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
                        effect.exposure = sky->GetExposure();
                        effect.parameters[0] = {glm::normalize(sunDirection), sky->GetRayleighStrength()};
                        effect.parameters[1] = {sky->GetSunColor(), sky->GetMieStrength()};
                        effect.parameters[2] = {sky->GetMoonColor(), sky->GetMieAnisotropy()};
                        effect.parameters[3] = {sky->GetGroundColor(), sky->GetOzoneStrength()};
                        effect.parameters[4] = {sky->GetSunIntensity(), sky->GetSunAngularRadius(),
                                                sky->GetNightIntensity(), sky->GetStarIntensity()};
                        effect.parameters[5] = {sky->GetMoonIntensity(), sky->GetMoonAngularRadius(), 0.0f, 0.0f};
                        effects.push_back(effect);
                        hasSky = true;
                        break;
                    }

            for (const auto *cloud : entity->GetComponents<scene::VolumetricCloudComponent>())
                if (cloud && cloud->IsEnabled() && cloud->GetDensity() > 0.0f && cloud->GetCoverage() > 0.0f)
                {
                    render::BasicPostProcessEffect effect{render::BasicPostProcessEffectType::VolumetricCloud};
                    glm::vec3 lightDirection = -lighting.directionalDirection;
                    if (glm::dot(lightDirection, lightDirection) < 0.000001f)
                        lightDirection = glm::vec3(0.0f, 1.0f, 0.0f);
                    lightDirection = glm::normalize(lightDirection);
                    const float horizonVisibility = glm::smoothstep(-0.02f, 0.03f, lightDirection.y);
                    const glm::vec3 windDirection = glm::dot(cloud->GetWindDirection(), cloud->GetWindDirection()) > 0.000001f
                                                        ? glm::normalize(cloud->GetWindDirection())
                                                        : glm::vec3(0.0f);
                    effect.quality = static_cast<std::uint32_t>(std::clamp(cloud->GetPrimaryStepCount(), 1, 128)) |
                                     (static_cast<std::uint32_t>(std::clamp(cloud->GetLightStepCount(), 1, 16)) << 8u);
                    effect.parameters[0] = {cloud->GetCloudColor(), cloud->GetCoverage()};
                    effect.parameters[1] = {windDirection * cloud->GetWindSpeed() * cloud->GetSimulationTime(),
                                            cloud->GetDensity()};
                    effect.parameters[2] = {lightDirection, cloud->GetExtinction()};
                    effect.parameters[3] = {glm::max(lighting.directionalColor, glm::vec3(0.0f)),
                                            std::max(lighting.directionalIntensity, 0.0f) * horizonVisibility};
                    effect.parameters[4] = {cloud->GetScatteringAlbedo(), cloud->GetAnisotropy(),
                                            cloud->GetAmbientLight(), cloud->GetBaseNoiseScale()};
                    effect.parameters[5] = {cloud->GetDetailNoiseScale(), cloud->GetDetailErosion(), 0.0f, 0.0f};
                    const glm::mat4 volumeTransform = entity->GetWorldTransform() *
                                                      glm::scale(glm::mat4(1.0f), cloud->GetSize());
                    effect.worldToLocal = glm::inverse(volumeTransform);
                    const glm::vec3 offset = entity->GetWorldPosition() - cameraPosition;
                    clouds.push_back({effect, glm::dot(offset, offset)});
                }
            for (const auto *child : entity->GetChildren())
                CollectAtmosphere(child, cameraPosition, lighting, effects, clouds, hasSky);
        }
    }

    BasicLighting BuildSceneLighting(const CameraData &cameraData, const scene::Scene *scene)
    {
        render::BasicLighting lighting;
        lighting.cameraPosition = glm::vec3(glm::inverse(cameraData.view)[3]);
        lighting.view = cameraData.view;
        lighting.ambientIntensity = 0.0f;
        lighting.directionalIntensity = 0.0f;
        lighting.directionalDirection = -glm::normalize(glm::vec3(0.25f, 0.8f, 0.4f));
        if (scene)
            for (const auto *light : scene->GetLights())
                if (light && light->type == scene::LightType::Directional)
                {
                    lighting.directionalDirection = light->direction;
                    lighting.directionalColor = light->color;
                    lighting.directionalIntensity = light->intensity;
                    lighting.shadowsEnabled = light->castsShadows;
                    scene::ApplyDirectionalShadowSettings(lighting, light->directionalShadowSettings);
                    break;
                }

        if (const auto *sky = FindPhysicalSky(scene); sky && lighting.directionalIntensity > 0.0f)
        {
            glm::vec3 sunDirection = -lighting.directionalDirection;
            if (glm::dot(sunDirection, sunDirection) > 0.000001f)
            {
                sunDirection = glm::normalize(sunDirection);
                lighting.directionalColor *= AtmosphericSunTransmittance(*sky, sunDirection);
                lighting.directionalIntensity *= glm::smoothstep(-0.02f, 0.03f, sunDirection.y);
            }
        }

        return lighting;
    }

    std::vector<BasicPostProcessEffect> BuildSceneAtmosphere(const scene::Scene *scene,
                                                           const BasicLighting &lighting)
    {
        std::vector<render::BasicPostProcessEffect> atmosphereEffects;
        std::vector<CloudPacket> clouds;
        bool hasSky = false;
        if (scene)
            for (const auto *root : scene->GetRootEntities())
                CollectAtmosphere(root, lighting.cameraPosition, lighting, atmosphereEffects, clouds, hasSky);
        std::sort(clouds.begin(), clouds.end(), [](const CloudPacket &lhs, const CloudPacket &rhs)
                  { return lhs.distanceSquared > rhs.distanceSquared; });
        for (auto &cloud : clouds)
        {
            if (const auto *sky = FindPhysicalSky(scene); sky && cloud.effect.parameters[2].y <= -0.02f)
            {
                auto &effect = cloud.effect;
                const float sunHeight = effect.parameters[2].y;
                const float night = 1.0f - glm::smoothstep(-0.31f, -0.04f, sunHeight);
                const float visibility = night * glm::smoothstep(-0.04f, 0.04f, -sunHeight);
                effect.parameters[2] = {-glm::vec3(effect.parameters[2]), effect.parameters[2].w};
                effect.parameters[3] = {sky->GetMoonColor(), sky->GetMoonIntensity() * visibility};
                // The scattering light is now the moon; ambient still follows the sun.
                effect.parameters[5].z = 1.0f;
            }
            atmosphereEffects.push_back(std::move(cloud.effect));
        }

        return atmosphereEffects;
    }
}
