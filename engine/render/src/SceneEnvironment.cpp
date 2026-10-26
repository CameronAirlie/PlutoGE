#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/PhysicalSkyModel.h"
#include "PlutoGE/scene/DirectionalShadowLighting.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/CameraTagFilter.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace PlutoGE::render
{
    namespace
    {
        struct CloudPacket
        {
            render::BasicPostProcessEffect effect;
            float distanceSquared = 0.0f;
        };

        const scene::PhysicalSkyComponent *FindPhysicalSky(const scene::Scene *scene, const scene::CameraTagFilter *filter = nullptr)
        {
            if (scene)
                for (const auto *sky : scene->GetPhysicalSkyComponents())
                    if (sky->IsEnabled() && sky->GetOwner() && sky->GetOwner()->IsActiveInHierarchy() && (!filter || filter->Accepts(sky->GetOwner())))
                        return sky;
            return nullptr;
        }

        // The sky's sun follows the directional light unless the sky handed
        // that light to the moon (BuildSceneLighting records the true sun).
        glm::vec3 SunDirection(const render::BasicLighting &lighting)
        {
            const glm::vec3 sun = glm::dot(lighting.sunDirection, lighting.sunDirection) > 0.000001f
                                      ? lighting.sunDirection
                                      : -lighting.directionalDirection;
            return glm::dot(sun, sun) > 0.000001f ? glm::normalize(sun) : glm::vec3(0.0f, 1.0f, 0.0f);
        }

        void CollectAtmosphere(const scene::Scene *scene, const glm::vec3 &cameraPosition,
                               const render::BasicLighting &lighting,
                               std::vector<render::BasicPostProcessEffect> &effects, std::vector<CloudPacket> &clouds,
                               const scene::CameraTagFilter *filter)
        {
            if (!scene)
                return;
            if (const auto *sky = FindPhysicalSky(scene, filter))
            {
                // Horizon attenuation can make a valid sun's intensity zero.
                // Keep its direction so sunset cannot reset the sky to daytime.
                const auto &settings = sky->GetSettings();
                const auto celestial = render::ResolveCelestialState(settings, SunDirection(lighting));
                render::BasicPostProcessEffect effect{render::BasicPostProcessEffectType::PhysicalSky};
                effect.exposure = settings.exposure;
                effect.parameters = render::PackPhysicalSkyParameters(settings, celestial);
                effect.worldToLocal = glm::mat4(celestial.worldToCelestial);
                effects.push_back(effect);
            }

            for (const auto *cloud : scene->GetVolumetricCloudComponents())
                if (cloud && cloud->IsEnabled() && cloud->GetOwner() && cloud->GetOwner()->IsActiveInHierarchy() &&
                    (!filter || filter->Accepts(cloud->GetOwner())) && cloud->GetDensity() > 0.0f && cloud->GetCoverage() > 0.0f)
                {
                    render::BasicPostProcessEffect effect{render::BasicPostProcessEffectType::VolumetricCloud};
                    glm::vec3 lightDirection = -lighting.directionalDirection;
                    if (glm::dot(lightDirection, lightDirection) < 0.000001f)
                        lightDirection = glm::vec3(0.0f, 1.0f, 0.0f);
                    lightDirection = glm::normalize(lightDirection);
                    const float horizonVisibility = glm::smoothstep(-0.02f, 0.03f, lightDirection.y);
                    const glm::vec3 windDirection =
                        glm::dot(cloud->GetWindDirection(), cloud->GetWindDirection()) > 0.000001f
                            ? glm::normalize(cloud->GetWindDirection())
                            : glm::vec3(0.0f);
                    // Render Scale (0.25-1) selects the reduced trace: 0.5 traces at half resolution.
                    effect.volumetricResolutionDivisor = static_cast<std::uint32_t>(
                        std::clamp(std::lround(1.0f / std::max(cloud->GetRenderScale(), 0.25f)), 1l, 4l));
                    effect.quality = static_cast<std::uint32_t>(std::clamp(cloud->GetPrimaryStepCount(), 1, 128)) |
                                     (static_cast<std::uint32_t>(std::clamp(cloud->GetLightStepCount(), 1, 16)) << 8u);
                    effect.parameters[0] = {cloud->GetCloudColor(), cloud->GetCoverage()};
                    effect.parameters[1] = {windDirection * cloud->GetWindSpeed() * cloud->GetSimulationTime(),
                                            cloud->GetDensity()};
                    effect.parameters[2] = {lightDirection, cloud->GetExtinction()};
                    effect.parameters[3] = {glm::max(lighting.directionalColor, glm::vec3(0.0f)),
                                            std::max(lighting.directionalIntensity, 0.0f) * horizonVisibility};
                    // Daylight fill follows the sun even when the moon is the scattering light.
                    float ambientLight = cloud->GetAmbientLight();
                    if (lighting.directionalIsMoon)
                    {
                        const float daylight = glm::smoothstep(-0.12f, 0.035f, SunDirection(lighting).y);
                        ambientLight *= daylight * daylight;
                    }
                    effect.parameters[4] = {cloud->GetScatteringAlbedo(), cloud->GetAnisotropy(),
                                            ambientLight, cloud->GetBaseNoiseScale()};
                    effect.parameters[5] = {cloud->GetDetailNoiseScale(), cloud->GetDetailErosion(),
                                            lighting.directionalIsMoon ? 1.0f : 0.0f, 0.0f};
                    const auto *entity = cloud->GetOwner();
                    const glm::mat4 volumeTransform =
                        entity->GetWorldTransform() * glm::scale(glm::mat4(1.0f), cloud->GetSize());
                    effect.worldToLocal = glm::inverse(volumeTransform);
                    const glm::vec3 offset = entity->GetWorldPosition() - cameraPosition;
                    clouds.push_back({effect, glm::dot(offset, offset)});
                }
        }
    } // namespace

    BasicLighting BuildSceneLighting(const CameraData &cameraData, const scene::Scene *scene)
    {
        const auto lights = scene ? scene->GetLights() : std::vector<scene::Light *>{};
        return BuildSceneLighting(cameraData, scene, lights);
    }

    BasicLighting BuildSceneLighting(const CameraData &cameraData, const scene::Scene *scene,
                                     std::span<scene::Light *const> lights)
    {
        render::BasicLighting lighting;
        lighting.cameraPosition = glm::vec3(glm::inverse(cameraData.view)[3]);
        lighting.view = cameraData.view;
        // Preview lighting has no sky or probes, so fill faces turned from the key light.
        lighting.ambientIntensity = scene && scene->IsPreviewLightingEnabled() ? 0.55f : 0.0f;
        lighting.directionalIntensity = 0.0f;
        lighting.directionalDirection = -glm::normalize(glm::vec3(0.25f, 0.8f, 0.4f));
        for (const auto *light : lights)
            if (light && light->type == scene::LightType::Directional)
            {
                lighting.directionalDirection = light->direction;
                lighting.directionalColor = light->color;
                lighting.directionalIntensity = light->intensity;
                lighting.shadowsEnabled = light->castsShadows;
                scene::ApplyDirectionalShadowSettings(lighting, light->directionalShadowSettings);
                break;
            }

        if (const auto *sky = FindPhysicalSky(scene))
        {
            const glm::vec3 sunDirection = -lighting.directionalDirection;
            if (glm::dot(sunDirection, sunDirection) > 0.000001f)
            {
                const auto &settings = sky->GetSettings();
                const auto celestial = render::ResolveCelestialState(settings, sunDirection);
                const auto light = render::ResolveCelestialLight(settings, celestial, lighting.directionalColor,
                                                                 lighting.directionalIntensity);
                lighting.sunDirection = celestial.sunDirection;
                lighting.directionalIsMoon = light.isMoon;
                lighting.directionalDirection = light.direction;
                lighting.directionalColor = light.color;
                lighting.directionalIntensity = light.intensity;
            }
        }

        return lighting;
    }

    std::vector<BasicPostProcessEffect> BuildSceneAtmosphere(const scene::Scene *scene, const BasicLighting &lighting,
                                                             const scene::CameraTagFilter *filter)
    {
        std::vector<render::BasicPostProcessEffect> atmosphereEffects;
        std::vector<CloudPacket> clouds;
        CollectAtmosphere(scene, lighting.cameraPosition, lighting, atmosphereEffects, clouds, filter);
        std::sort(clouds.begin(), clouds.end(), [](const CloudPacket &lhs, const CloudPacket &rhs) {
            return lhs.distanceSquared > rhs.distanceSquared;
        });
        for (auto &cloud : clouds)
            atmosphereEffects.push_back(std::move(cloud.effect));

        return atmosphereEffects;
    }
} // namespace PlutoGE::render
