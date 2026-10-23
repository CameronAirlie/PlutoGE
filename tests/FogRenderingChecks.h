#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/CloudOcclusion.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

template <class ReadPixels>
void CheckFogRendering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels, unsigned divisor = 1)
{
    using namespace PlutoGE::render;
    renderer.Resize(33, 33);
    BasicLighting lighting;
    lighting.view = glm::mat4(1);
    lighting.cameraPosition = {0, 0, 0};
    lighting.ambientIntensity = 0;
    lighting.directionalIntensity = 0;
    lighting.shadowsEnabled = false;
    glm::mat4 projection(0);
    projection[0][0] = projection[1][1] = 1;
    projection[2][2] = .1f / (2000.f - .1f);
    projection[2][3] = -1;
    projection[3][2] = 2000.f * .1f / (2000.f - .1f);
    BasicPostProcessEffect fog{BasicPostProcessEffectType::VolumetricFog};
    fog.volumetricResolutionDivisor = divisor;
    fog.quality = 16;
    fog.parameters[0] = {1, 1, 1, .05f};
    fog.parameters[1] = {0, 0, 1000, 0};
    fog.parameters[2] = {0, 1, 1, 1};
    fog.parameters[3].x = 1;
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-1,-1,0}}, {{0,0,1}}, {{0,0}}},
        {{{1,-1,0}}, {{0,0,1}}, {{1,0}}},
        {{{1,1,0}}, {{0,0,1}}, {{1,1}}},
        {{{-1,1,0}}, {{0,0,1}}, {{0,1}}}
    }};
    constexpr std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw draw;
    draw.mesh = &mesh;
    draw.emission = {1, 1, 1};
    const auto sample = [&](bool surface)
    {
        renderer.Render(projection * lighting.view, lighting,
            surface ? std::span(&draw, 1) : std::span<BasicDraw>{}, std::span(&fog, 1));
        const auto pixels = readPixels(renderer.GetColorTexture());
        return static_cast<int>(pixels[(16 * 33 + 16) * 4]) / 255.0f;
    };
    const auto expect = [](float actual, float expected, const char *message)
    {
        if (std::abs(actual - expected) > .015f)
        {
            std::cerr << message << ": " << actual << " expected " << expected << '\n';
            throw std::runtime_error(message);
        }
    };
    draw.model = glm::translate(glm::mat4(1), glm::vec3(0,0,-2));
    expect(sample(true), std::exp(-.1f), "Near fog extinction");
    fog.parameters[3] = {1,1,4,2000};
    expect(sample(true), std::exp(-.1f), "Horizon haze must preserve nearby surfaces");
    fog.parameters[0].w = 0;
    lighting.ambientIntensity = 1;
    expect(sample(false), .65f, "Horizon haze must work independently of height-fog density");
    fog.parameters[0].w = .05f;
    lighting.ambientIntensity = 0;
    fog.parameters[3] = {1,0,0,0};
    draw.model = glm::translate(glm::mat4(1), glm::vec3(0,0,-20));
    expect(sample(true), std::exp(-1.f), "Distant fog extinction");
    {
        // Particles composite after fog. An opaque black billboard 20 units
        // into the medium must show only the fog in front of it.
        auto particleFog = fog;
        particleFog.parameters[1].w = 1;
        auto particleLighting = lighting;
        particleLighting.ambientIntensity = 1;
        BasicParticleDraw particle;
        particle.parameters.viewProjection = projection * particleLighting.view;
        particle.parameters.inverseProjection = glm::inverse(projection);
        particle.parameters.view = particleLighting.view;
        particle.parameters.values[0] = glm::vec4(1);
        particle.parameters.values[2].x = 1;
        particle.parameters.values[3] = {1, 1, 0, 0};
        BasicParticleInstance instance;
        instance.centerRotation = {0, 0, -20, 0};
        instance.color = {0, 0, 0, 1};
        instance.ageDensityRandomSize = {0, 1, 0, 100};
        particle.instances.push_back(instance);
        renderer.Render(projection * particleLighting.view, particleLighting, {}, std::span(&particleFog, 1), {},
                        PostProcessDebugView::None, nullptr, nullptr, true, {}, std::span(&particle, 1));
        const auto pixels = readPixels(renderer.GetColorTexture());
        expect(static_cast<int>(pixels[(16 * 33 + 16) * 4]) / 255.0f, 1.f - std::exp(-1.f),
               "Particles must receive the fog between them and the camera");
    }
    for (float detailDistance : {.25f, 2.f, 10.f, 40.f})
    {
        fog.parameters[1].z = detailDistance;
        expect(sample(true), std::exp(-1.f), "Surface extinction must continue past shadow detail distance");
    }
    lighting.view = glm::lookAtRH(glm::vec3(0), glm::vec3(0,1,0), glm::vec3(0,0,1));
    fog.parameters[1] = {1, -100, 1000, 1};
    fog.parameters[0].w = 1;
    lighting.ambientIntensity = 1;
    expect(sample(false), .04f, "Sky above the fog layer must stay clear");
    fog.parameters[1].y = 0;
    fog.parameters[0].w = .1f;
    lighting.view = glm::lookAtRH(glm::vec3(0), glm::vec3(0,1,0), glm::vec3(0,0,1));
    const float thinLayer = 1.f - .96f * std::exp(-.1f);
    expect(sample(false), thinLayer, "Long upward ray must integrate thin ground fog");
    fog.quality = 64;
    expect(sample(false), thinLayer, "Fog extinction must not depend on step count");
    for (float detailDistance : {.1f, 1.f, 10.f})
    {
        fog.parameters[1].z = detailDistance;
        expect(sample(false), thinLayer, "Infinite upward integral must not depend on its split");
    }
    lighting.view = glm::mat4(1);
    fog.parameters[1].x = 0;
    fog.parameters[0].w = .00001f;
    expect(sample(false), 1.f, "Infinite horizontal fog must reach full opacity");
    fog.parameters[0].w = 0;
    expect(sample(false), .04f, "Zero density must remain clear even at infinity");
    fog.parameters[0].w = .1f;
    lighting.ambientIntensity = 0;
    expect(sample(false), 0, "Unlit fog must not emit ambient light");
    lighting.directionalIntensity = 1;
    lighting.directionalColor = {1,1,1};
    lighting.directionalDirection = {0,-1,0};
    expect(sample(false), 0, "An infinite homogeneous medium must extinguish sunlight");
    // With falloff 1 the medium above the horizontal ray has optical depth
    // density / falloff = .1 toward an overhead sun.
    fog.parameters[1].x = 1;
    const float sunThroughFog = std::exp(-.1f) / (4.f * 3.14159265f);
    expect(sample(false), sunThroughFog, "Directional fog phase must be normalized");
    lighting.shadowsEnabled = true;
    lighting.shadowResolution = 64;
    lighting.shadowCascadeCount = 1;
    for (float shadowDistance : {1.f, 4.f, 20.f})
    {
        lighting.shadowCascadeSplits = glm::vec4(shadowDistance);
        expect(sample(false), sunThroughFog, "Missing cascade coverage must not darken distant fog");
    }
    lighting.shadowsEnabled = false;
    {
        // A cloud slab overhead dims the sun by its mean extinction. The
        // horizontal view ray never enters the slab, so only fog lighting changes.
        BasicPostProcessEffect cloud{BasicPostProcessEffectType::VolumetricCloud};
        cloud.parameters[0] = {1, 1, 1, 1};
        cloud.parameters[1] = {0, 0, 0, 1};
        cloud.parameters[2] = {0, 1, 0, .3f};
        cloud.parameters[3] = {1, 1, 1, 1};
        cloud.parameters[4] = {.9f, .5f, 0, .01f};
        cloud.parameters[5] = {.05f, 0, 0, 0};
        cloud.worldToLocal = glm::inverse(glm::translate(glm::mat4(1), glm::vec3(0, 15, 0)) *
                                          glm::scale(glm::mat4(1), glm::vec3(10000, 10, 10000)));
        fog.parameters[2].y = 0; // Isolate sunlight from the clouds' overcast fill.
        const std::array clouded{cloud, fog};
        const auto occlusion = BuildCloudOcclusion(clouded);
        const float cloudTransmittance = std::exp(-occlusion.volumes[0].w * 10.f);
        lighting.directionalIntensity = 8;
        renderer.Render(projection * lighting.view, lighting, {}, std::span(&clouded[0], 2));
        const auto pixels = readPixels(renderer.GetColorTexture());
        expect(static_cast<int>(pixels[(16 * 33 + 16) * 4]) / 255.0f, 8.f * sunThroughFog * cloudTransmittance,
               "Clouds overhead must dim fog sunlight by their mean extinction");
        if (cloudTransmittance > .6f || cloudTransmittance < .1f)
            throw std::runtime_error("Cloud occlusion check must exercise a partially opaque slab");
        // Without sky or sun, the only fog light is the cloud base radiance
        // that replaces the blocked fraction of the sky.
        fog.parameters[2].y = 1;
        cloud.parameters[3].w = 20;
        const std::array overcast{cloud, fog};
        const auto overcastOcclusion = BuildCloudOcclusion(overcast);
        const float skyVisibility = std::exp(-1.66f * overcastOcclusion.volumes[0].w * 10.f);
        lighting.directionalIntensity = 0;
        renderer.Render(projection * lighting.view, lighting, {}, std::span(&overcast[0], 2));
        const auto overcastPixels = readPixels(renderer.GetColorTexture());
        expect(static_cast<int>(overcastPixels[(16 * 33 + 16) * 4]) / 255.0f,
               overcastOcclusion.volumes[0].r * (1.f - skyVisibility),
               "Clouds must replace blocked sky light with their base radiance");
    }
    fog.parameters[1].x = 0;
    lighting.directionalIntensity = 0;
    lighting.ambientIntensity = 1;
    {
        // Haze accumulates with distance; opaque fog scattering within metres must hide it.
        auto hazed = fog;
        hazed.parameters[0] = {.5f, .5f, .5f, .1f};
        hazed.parameters[3] = {1, 1, 4, 2000};
        renderer.Render(projection * lighting.view, lighting, {}, std::span(&hazed, 1));
        const auto pixels = readPixels(renderer.GetColorTexture());
        expect(static_cast<int>(pixels[(16 * 33 + 16) * 4]) / 255.0f, .5f,
               "Nearby opaque height fog must hide distant haze");
    }
    fog.parameters[2].w = .25f;
    expect(sample(false), .04f * .75f + .25f, "Sky must respect maximum fog opacity");
    fog.parameters[2].w = 1;
    lighting.ambientIntensity = 0;
    lighting.physicalSkyEnabled = true;
    lighting.physicalSkyParameters[0] = {0, 1, 0, 1};
    lighting.physicalSkyParameters[1] = {1, 1, 1, .1f};
    lighting.physicalSkyParameters[2] = {0, 0, 0, .2f};
    lighting.physicalSkyParameters[3] = {.1f, .1f, .1f, 1};
    lighting.physicalSkyParameters[4] = {10, .5f, 0, 0};
    if (sample(false) <= .01f)
        throw std::runtime_error("Physical sky must provide ambient fog lighting without an artificial floor");
    // A grazing plane has large depth changes between neighboring rows, but
    // no silhouette there. Reduced-resolution fog must not drop whole rows.
    renderer.Resize(129, 97);
    lighting.physicalSkyEnabled = false;
    lighting.ambientIntensity = 1;
    lighting.cameraPosition = {0, 1, 0};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0,1,-1), glm::vec3(0,1,0));
    draw.model = glm::translate(glm::mat4(1), glm::vec3(0,0,-500)) *
        glm::rotate(glm::mat4(1), glm::radians(-90.f), glm::vec3(1,0,0)) *
        glm::scale(glm::mat4(1), glm::vec3(1000));
    draw.baseColor = {0,0,0,1};
    draw.emission = {0,0,0};
    fog.parameters[0] = {1,1,1,.5f};
    fog.parameters[1] = {0,0,20,1};
    fog.parameters[2] = {0,1,0,1};
    fog.volumetricResolutionDivisor = 1;
    renderer.Render(projection * lighting.view, lighting, std::span(&draw,1), std::span(&fog,1));
    const auto reference = readPixels(renderer.GetColorTexture());
    fog.volumetricResolutionDivisor = 2;
    renderer.Render(projection * lighting.view, lighting, std::span(&draw,1), std::span(&fog,1));
    const auto reduced = readPixels(renderer.GetColorTexture());
    int worstDrop = 0;
    int worstX = 0, worstY = 0;
    for (int y = 3; y < 94; ++y)
        for (int x = 32; x < 97; ++x)
        {
            const auto pixel = (y * 129 + x) * 4;
            const int drop = int(reference[pixel]) - int(reduced[pixel]);
            if (drop > worstDrop) { worstDrop = drop; worstX = x; worstY = y; }
        }
    std::cout << "Grazing fog maximum reconstruction drop: " << worstDrop << " at " << worstX << "," << worstY << '\n';
    if (worstDrop > 15)
        throw std::runtime_error("Reduced fog rejects rows on a continuous grazing surface");
    auto foreground = draw;
    foreground.model = glm::translate(glm::mat4(1), glm::vec3(0,1,-.2f)) *
        glm::scale(glm::mat4(1), glm::vec3(.002f,.1f,1));
    const std::array layeredDraws{draw, foreground};
    fog.volumetricResolutionDivisor = 1;
    renderer.Render(projection * lighting.view, lighting, layeredDraws, std::span(&fog,1));
    const auto silhouetteReference = readPixels(renderer.GetColorTexture());
    fog.volumetricResolutionDivisor = 2;
    renderer.Render(projection * lighting.view, lighting, layeredDraws, std::span(&fog,1));
    const auto silhouetteReduced = readPixels(renderer.GetColorTexture());
    int foregroundPixels = 0;
    for (std::size_t pixel = 0; pixel < silhouetteReference.size(); pixel += 4)
        if (int(silhouetteReference[pixel]) < 40)
        {
            ++foregroundPixels;
            if (int(silhouetteReduced[pixel]) > int(silhouetteReference[pixel]) + 15)
                throw std::runtime_error("Fog slope correction leaks across a thin foreground silhouette");
        }
    if (foregroundPixels == 0)
        throw std::runtime_error("Fog silhouette regression did not render its foreground");
    std::cout << "Fog distance, height, integration and lighting checks passed\n";
    if (divisor == 1)
        CheckFogRendering(renderer, readPixels, 2);
}
