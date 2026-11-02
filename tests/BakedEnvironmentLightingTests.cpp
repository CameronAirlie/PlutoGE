// Live sky and reflection settings for baked ambient volumes (docs/BAKED_ENVIRONMENT_LIGHTING.md): the sky
// projection and visibility maths the baker and renderer share, and the authoring data they persist.
#include "PlutoGE/render/PhysicalSkyModel.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/SkyIrradiance.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AmbientLightVolumeComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"

#include <cmath>
#include <iostream>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    std::vector<glm::vec3> SphereDirections(int count)
    {
        std::vector<glm::vec3> directions;
        for (int index = 0; index < count; ++index)
        {
            const float z = 1.0f - 2.0f * (index + 0.5f) / count;
            const float radial = std::sqrt(std::max(1.0f - z * z, 0.0f));
            const float angle = 2.39996322972865332f * index;
            directions.emplace_back(std::cos(angle) * radial, std::sin(angle) * radial, z);
        }
        return directions;
    }

    // The visibility a probe reaches the sky with must be 1 open, 0 enclosed, and follow an opening's direction.
    void CheckSkyVisibility()
    {
        const auto directions = SphereDirections(4096);
        render::SkyVisibilityAccumulator open, enclosed, upper;
        for (const auto &direction : directions)
        {
            open.Add(direction, true);
            enclosed.Add(direction, false);
            upper.Add(direction, direction.y > 0.0f);
        }
        for (const glm::vec3 normal : {glm::vec3(0, 1, 0), glm::vec3(1, 0, 0), glm::vec3(0, -1, 0)})
        {
            Require(std::abs(render::EvaluateSkyVisibility(open.Result(), normal) - 1.0f) < 0.01f, "An open probe does not see the whole sky");
            Require(render::EvaluateSkyVisibility(enclosed.Result(), normal) < 0.001f, "An enclosed probe sees the sky");
        }
        // A floor blocks the lower hemisphere: a surface facing up sees all of its sky, one facing sideways half
        // (the exact cosine-weighted share), one facing down none.
        Require(std::abs(render::EvaluateSkyVisibility(upper.Result(), {0, 1, 0}) - 1.0f) < 0.02f, "Sky visibility above a floor is not complete facing up");
        Require(std::abs(render::EvaluateSkyVisibility(upper.Result(), {1, 0, 0}) - 0.5f) < 0.02f, "Sky visibility above a floor is not half facing sideways");
        Require(render::EvaluateSkyVisibility(upper.Result(), {0, -1, 0}) < 0.02f, "Sky visibility above a floor is not blocked facing down");
        Require(render::SkyVisibilityAccumulator{}.Result() == glm::vec4(0, 0, 0, 1), "An empty accumulator does not default to open sky");
    }

    // The projected sky must match the cosine-weighted mean radiance the shaders' sky irradiance integrates.
    void CheckSkyProjection()
    {
        const render::PhysicalSkySettings sky;
        const auto directions = SphereDirections(8192);
        for (const glm::vec3 sun : {glm::vec3(0.3f, 0.8f, -0.2f), glm::vec3(0.9f, 0.05f, 0.1f)})
        {
            const auto celestial = render::ResolveCelestialState(sky, glm::normalize(sun));
            const auto projected = render::ProjectPhysicalSkyIrradiance(sky, celestial);
            const auto integrate = [&](glm::vec3 normal)
            {
                glm::vec3 reference(0.0f);
                float weightSum = 0.0f;
                for (const auto &direction : directions)
                {
                    const float weight = std::max(glm::dot(direction, normal), 0.0f);
                    reference += render::EvaluatePhysicalSkyEnvironment(sky, celestial, direction) * weight;
                    weightSum += weight;
                }
                return reference / weightSum;
            };
            // The same sky through the L2 cosine kernel written as Legendre polynomials, independent of the SH basis
            // constants and coefficient order: K(t) = (1/4pi) sum_l A_l (2l+1) P_l(t), divided by pi for mean radiance.
            const auto convolveL2 = [&](glm::vec3 normal)
            {
                glm::vec3 result(0.0f);
                for (const auto &direction : directions)
                {
                    const float t = glm::dot(direction, normal);
                    const float kernel = (1.0f + 2.0f * t + 0.625f * (3.0f * t * t - 1.0f)) / (4.0f * 3.14159265f);
                    result += render::EvaluatePhysicalSkyEnvironment(sky, celestial, direction) * kernel;
                }
                return result * (4.0f * 3.14159265f / static_cast<float>(directions.size()));
            };
            const float scale = glm::length(integrate({0, 1, 0}));
            for (const glm::vec3 normal : {glm::vec3(0, 1, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, -1, 0)})
            {
                const glm::vec3 value = projected.Evaluate(normal);
                const auto describe = [&](glm::vec3 expected)
                {
                    return " facing " + std::to_string(normal.x) + "," + std::to_string(normal.y) + "," + std::to_string(normal.z) +
                           ": " + std::to_string(glm::length(value)) + " vs " + std::to_string(glm::length(expected));
                };
                const glm::vec3 kernel = glm::max(convolveL2(normal), glm::vec3(0.0f));
                // The projection samples 256 directions, the reference 8192: allow for sampling noise.
                Require(glm::length(value - kernel) <= 0.03f * std::max(scale, glm::length(kernel)),
                        "Sky projection is not the L2 cosine convolution" + describe(kernel));
                // L2 is what the baker stores too; it stays near the exact integral, short of the sun's sharp
                // forward-scattering lobe.
                const glm::vec3 exact = integrate(normal);
                Require(glm::length(value - exact) <= 0.25f * scale, "Sky projection is far from the integrated sky" + describe(exact));
            }
            Require(projected.Evaluate({0, 1, 0}).b > 2.0f * projected.Evaluate({0, -1, 0}).b, "The projected sky is not above the ground");
        }
        render::PhysicalSkyIrradianceCache cache;
        const auto celestial = render::ResolveCelestialState(sky, glm::normalize(glm::vec3(0.3f, 0.8f, -0.2f)));
        const auto &first = cache.Get(sky, celestial);
        Require(first == render::ProjectPhysicalSkyIrradiance(sky, celestial), "Cached sky irradiance differs from a projection");
        auto dusk = sky;
        dusk.sunIntensity *= 0.5f;
        Require(!(cache.Get(dusk, celestial) == render::ProjectPhysicalSkyIrradiance(sky, celestial)),
                "The sky irradiance cache ignores changed sky settings");
    }

    // Scene lighting carries the live sky only with a physical sky.
    void CheckSceneLighting()
    {
        scene::Scene scene;
        render::CameraData camera{glm::mat4(1.0f), glm::mat4(1.0f)};
        Require(!render::BuildSceneLighting(camera, &scene).skyIrradiance, "Sky irradiance without a physical sky");
        auto sun = std::make_unique<scene::Entity>();
        sun->CreateComponent<scene::LightComponent>()->GetLight().type = scene::LightType::Directional;
        scene.AddEntity(std::move(sun));
        auto skyEntity = std::make_unique<scene::Entity>();
        skyEntity->CreateComponent<scene::PhysicalSkyComponent>();
        scene.AddEntity(std::move(skyEntity));
        const auto lighting = render::BuildSceneLighting(camera, &scene);
        Require(lighting.skyIrradiance && glm::length(lighting.skyIrradiance->Evaluate({0, 1, 0})) > 0.0f,
                "A physical sky does not supply live sky irradiance");
    }

    void CheckVolumePersistence()
    {
        scene::AmbientLightVolumeComponent volume;
        Require(volume.GetSkyLighting() == scene::AmbientSkyLighting::Live && volume.GetReflectionOcclusion() == 1.0f,
                "New ambient volumes do not default to a live sky and full reflection occlusion");
        scene::BakedProbeVolume baked;
        baked.resolution = {1, 1, 1};
        baked.irradiance = {glm::vec3(1)};
        baked.positions = {glm::vec4(0, 0, 0, 1)};
        baked.sphericalHarmonics.resize(1);
        baked.sphericalHarmonics[0][0] = glm::vec3(0.25f, 0.5f, 1.0f);
        baked.visibility.resize(1);
        baked.visibility[0].fill(2.0f);
        baked.skyVisibility = {glm::vec4(0.0f, 0.5f, 0.0f, 0.5f)};
        baked.skyIrradianceScale = 2.0f;
        volume.SetBakedVolume(baked);
        volume.SetReflectionOcclusion(0.25f);
        volume.SetSkyLighting(scene::AmbientSkyLighting::Baked);
        Require(volume.GetBakedVolume().reflectionOcclusion == 0.25f, "Reflection occlusion is not mirrored into the bake");
        Require(volume.SkyLightingNeedsRebake(), "A live bake under Baked mode does not ask for a rebake");

        scene::AmbientLightVolumeComponent restored;
        restored.Deserialize(volume.Serialize());
        const auto &loaded = restored.GetBakedVolume();
        Require(restored.GetSkyLighting() == scene::AmbientSkyLighting::Baked && restored.GetReflectionOcclusion() == 0.25f,
                "Sky lighting settings are not persisted");
        Require(loaded.HasLiveSky() && loaded.skyIrradianceScale == 2.0f && loaded.skyVisibility[0] == baked.skyVisibility[0] &&
                    loaded.sphericalHarmonics[0][0] == baked.sphericalHarmonics[0][0] && loaded.reflectionOcclusion == 0.25f,
                "Sky visibility or its scale does not survive serialization");
        restored.SetReflectionOcclusion(7.0f);
        Require(restored.GetReflectionOcclusion() == 1.0f, "Reflection occlusion is not limited to 0..1");

        // Version 2 bakes predate sky visibility: they load, keep their sky and ask for a rebake.
        // Header: version, origin, size, basis, resolution, directional, normals; then one probe's irradiance,
        // position, validity, nine SH coefficients and 64 visibility distances.
        std::ostringstream version2;
        version2 << "2 0 0 0 1 1 1 1 0 0 0 1 0 0 0 1 1 1 1 1 0 1 1 1 0 0 0 1 ";
        for (int i = 0; i < 27; ++i) version2 << "0.5 ";
        for (int i = 0; i < 64; ++i) version2 << "2 ";
        std::vector<scene::Property> legacy = {{"Baked Data", scene::PropertyType::String, version2.str()}};
        scene::AmbientLightVolumeComponent old;
        old.Deserialize(legacy);
        Require(old.GetBakedVolume().HasDirectionalData() && !old.GetBakedVolume().HasSkyVisibility() &&
                    old.GetBakedVolume().skyIrradianceScale == 0.0f && old.SkyLightingNeedsRebake(),
                "A version 2 bake does not load without sky visibility");
    }

    void CheckCapturePersistence()
    {
        scene::IblCaptureComponent capture;
        Require(capture.RelightsFromAmbient() && capture.BuildCaptureVolume().relightFromAmbient,
                "Captures do not relight from ambient volumes by default");
        capture.SetRelightFromAmbient(false);
        scene::IblCaptureComponent restored;
        restored.Deserialize(capture.Serialize());
        Require(!restored.RelightsFromAmbient() && !restored.BuildCaptureVolume().relightFromAmbient,
                "Capture relighting is not persisted");
    }
}

int main()
try
{
    CheckSkyVisibility();
    CheckSkyProjection();
    CheckSceneLighting();
    CheckVolumePersistence();
    CheckCapturePersistence();
    std::cout << "PASS: baked environment lighting\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
