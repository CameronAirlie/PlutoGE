#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/PhysicalSkyModel.h"
#include "PlutoGE/render/SkyIrradiance.h"
#include "PlutoGE/scene/DirectionalShadowLighting.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/CameraTagFilter.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"
#include "PlutoGE/scene/components/WindComponent.h"
#include "PlutoGE/scene/components/WeatherComponent.h"

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
            const scene::PhysicalSkyComponent *sky = FindPhysicalSky(scene, filter);
            render::CelestialState celestial;
            glm::vec3 skyAmbient(0.0f);
            if (sky)
            {
                // Horizon attenuation can make a valid sun's intensity zero.
                // Keep its direction so sunset cannot reset the sky to daytime.
                const auto &settings = sky->GetSettings();
                celestial = render::ResolveCelestialState(settings, SunDirection(lighting));
                render::BasicPostProcessEffect effect{render::BasicPostProcessEffectType::PhysicalSky};
                effect.exposure = settings.exposure;
                effect.parameters = render::PackPhysicalSkyParameters(settings, celestial);
                effect.worldToLocal = glm::mat4(celestial.worldToCelestial);
                effects.push_back(effect);
                skyAmbient = render::MeanPhysicalSkyRadiance(settings, celestial);
            }
            const auto luminance = [](const glm::vec3 &color) {
                return glm::dot(glm::max(color, glm::vec3(0.0f)), glm::vec3(0.2126f, 0.7152f, 0.0722f));
            };

            for (const auto *cloud : scene->GetVolumetricCloudComponents())
                if (cloud && cloud->IsEnabled() && cloud->GetOwner() && cloud->GetOwner()->IsActiveInHierarchy() &&
                    (!filter || filter->Accepts(cloud->GetOwner())) && cloud->GetDensity() > 0.0f && cloud->GetCoverage() > 0.0f)
                {
                    render::BasicPostProcessEffect effect{render::BasicPostProcessEffectType::VolumetricCloud};
                    const auto *entity = cloud->GetOwner();
                    const glm::mat4 volumeTransform =
                        entity->GetWorldTransform() * glm::scale(glm::mat4(1.0f), cloud->GetSize());
                    glm::vec3 lightDirection = -lighting.directionalDirection;
                    if (glm::dot(lightDirection, lightDirection) < 0.000001f)
                        lightDirection = glm::vec3(0.0f, 1.0f, 0.0f);
                    lightDirection = glm::normalize(lightDirection);
                    glm::vec3 lightColor = glm::max(lighting.directionalColor, glm::vec3(0.0f));
                    float lightIntensity = std::max(lighting.directionalIntensity, 0.0f);
                    if (sky && lighting.sunLightIntensity > 0.0f)
                    {
                        // The cloud sees the sun from its own altitude: past the
                        // ground horizon and through thinner air. Keep the moon
                        // only once it outshines that sun.
                        const auto sun = render::ResolveSunlightAtAltitude(
                            sky->GetSettings(), celestial, lighting.sunLightColor, lighting.sunLightIntensity,
                            volumeTransform[3].y);
                        if (!lighting.directionalIsMoon ||
                            luminance(sun.color) * sun.intensity >= luminance(lightColor) * lightIntensity)
                        {
                            lightDirection = -sun.direction;
                            lightColor = sun.color;
                            lightIntensity = sun.intensity;
                        }
                    }
                    else if (!sky)
                        lightIntensity *= glm::smoothstep(-0.02f, 0.03f, lightDirection.y);
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
                    effect.parameters[3] = {lightColor, lightIntensity};
                    // Ambient fill: the artist's flat daylight fill, faded through
                    // twilight by the sun, plus the physical sky's own light.
                    // RGB travels in lanes 4.z, 5.z and 6.w (VolumetricCloud.slang);
                    // the renderer owns 5.w (water volume depth).
                    const float daylight = glm::smoothstep(-0.12f, 0.035f, SunDirection(lighting).y);
                    const glm::vec3 ambient = glm::vec3(cloud->GetAmbientLight() * daylight * daylight) + skyAmbient;
                    effect.parameters[4] = {cloud->GetScatteringAlbedo(), cloud->GetAnisotropy(),
                                            ambient.r, cloud->GetBaseNoiseScale()};
                    effect.parameters[5] = {cloud->GetDetailNoiseScale(), cloud->GetDetailErosion(), ambient.g, 0.0f};
                    // CloudShadowLane (CloudShadowMap.h).
                    effect.parameters[6] = {cloud->GetCastShadows() ? 1.0f : 0.0f, cloud->GetShadowDistance(),
                                            cloud->GetShadowDensity(), ambient.b};
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
                lighting.sunLightColor = lighting.directionalColor;
                lighting.sunLightIntensity = lighting.directionalIntensity;
                lighting.sunDirection = celestial.sunDirection;
                lighting.directionalIsMoon = light.isMoon;
                lighting.directionalDirection = light.direction;
                lighting.directionalColor = light.color;
                lighting.directionalIntensity = light.intensity;
                // Per thread, so views built concurrently never share it; reprojects only when the sky changes.
                thread_local PhysicalSkyIrradianceCache skyIrradiance;
                lighting.skyIrradiance = skyIrradiance.Get(settings, celestial);
            }
        }

        lighting.wind = BuildSceneWind(scene);
        lighting.weather = BuildSceneWeather(scene);
        return lighting;
    }

    SceneWindSettings BuildSceneWind(const scene::Scene *scene)
    {
        if (scene)
            for (const auto *wind : scene->GetWindComponents())
                if (wind->IsEnabled() && wind->GetOwner() && wind->GetOwner()->IsActiveInHierarchy())
                    return wind->GetSettings();
        return {};
    }

    SceneWeatherSettings BuildSceneWeather(const scene::Scene *scene)
    {
        if (scene)
            for (const auto *weather : scene->GetWeatherComponents())
                if (weather->IsEnabled() && weather->GetOwner() && weather->GetOwner()->IsActiveInHierarchy())
                    return weather->GetSettings();
        return {};
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
