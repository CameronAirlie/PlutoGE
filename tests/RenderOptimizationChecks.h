#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "../engine/render/src/ParticleVisibility.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

inline void LoadRenderOptimizationShaders(PlutoGE::render::BasicRendererShaderPackage &shaders)
{
    using namespace PlutoGE::render;
    const auto additions = ShaderArtifactLibrary(PLUTO_RHI_TEST_SHADER_DIR).LoadBasicRendererPackage();
    shaders.particleInstancedVertex = additions.particleInstancedVertex;
    shaders.volumetricTrace = additions.volumetricTrace;
    shaders.volumetricComposite = additions.volumetricComposite;
    shaders.fusedColor = additions.fusedColor;
    shaders.postProcess[static_cast<std::size_t>(BasicPostProcessEffectType::VolumetricCloud)] =
        additions.postProcess[static_cast<std::size_t>(BasicPostProcessEffectType::VolumetricCloud)];
}

template <class ReadPixels>
void CheckRenderOptimizations(PlutoGE::render::BasicRenderer &renderer,
    PlutoGE::render::rhi::IRenderDevice &device,
    PlutoGE::render::BasicRendererShaderPackage shaders, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool value, const char *message)
    {
        if (!value) throw std::runtime_error(message);
    };
    const ParticleVisibility visibility(glm::mat4(1));
    require(visibility.IsVisible({0, 0, 0}, 0.1f), "Visible particle culled");
    require(visibility.IsVisible({1.1f, 0, 0}, 0.2f), "Billboard crossing viewport edge culled");
    require(!visibility.IsVisible({1.5f, 0, 0}, 0.2f), "Offscreen particle survived culling");
    require(!visibility.IsVisible({0, 0, -2}, 0.2f), "Particle outside clip depth survived culling");

    // A second renderer without the optional pipelines provides a standalone
    // pass reference without changing global settings or production behavior.
    shaders.fusedColor = {};
    shaders.volumetricTrace = {};
    shaders.volumetricComposite = {};
    BasicRenderer reference;
    require(reference.Initialize(device, shaders), "Reference renderer initialization failed");
    const auto oldWidth = renderer.GetWidth(), oldHeight = renderer.GetHeight();
    constexpr std::uint32_t width = 65, height = 47;
    require(renderer.Resize(width, height) && reference.Resize(width, height), "Optimization test resize failed");
    const auto compare = [&](const auto &a, const auto &b, double meanTolerance, int maxTolerance, const char *message)
    {
        require(a.size() == width * height * 4 && a.size() == b.size(), "Invalid optimization readback size");
        std::uint64_t sum = 0;
        int maximum = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (i % 4 == 3) continue;
            const int difference = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
            sum += difference;
            maximum = std::max(maximum, difference);
        }
        if (double(sum) / (width * height * 3) > meanTolerance || maximum > maxTolerance)
            throw std::runtime_error(std::string(message) + ": mean=" +
                std::to_string(double(sum) / (width * height * 3)) + ", max=" + std::to_string(maximum));
    };

    constexpr std::array<BasicVertex, 4> vertices{{
        {{{-1,-1,.2f}}, {{0,0,1}}, {{0,0}}}, {{{1,-1,.2f}}, {{0,0,1}}, {{1,0}}},
        {{{-1,1,.2f}}, {{0,0,1}}, {{0,1}}}, {{{1,1,.2f}}, {{0,0,1}}, {{1,1}}}
    }};
    constexpr std::array<std::uint32_t, 6> indices{0,1,2,2,1,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw draw;
    draw.mesh = &mesh;
    draw.emission = {0.23f, 0.51f, 0.82f};
    BasicLighting lighting;
    lighting.ambientIntensity = lighting.directionalIntensity = 0;
    BasicPostProcessEffect tone{BasicPostProcessEffectType::ToneMapping};
    tone.exposure = 1.3f;
    BasicPostProcessEffect grade{BasicPostProcessEffectType::ColorGrading};
    grade.parameters[0] = {.02f, 1.1f, .8f, .3f};
    grade.parameters[1] = {.1f, .15f, .01f, 1.1f};
    grade.parameters[2] = {1.05f, .1f, .25f, 0};
    grade.parameters[3] = {.5f, .7f, 1, .15f};
    grade.parameters[4] = {1, .7f, .4f, .2f};
    grade.parameters[5].x = .4f;
    BasicPostProcessEffect gamma{BasicPostProcessEffectType::GammaCorrection};
    gamma.gamma = 1.1f;
    std::vector<BasicPostProcessEffect> effects{tone, grade, grade, gamma};
    // More than eight operations exercises packet splitting. A UV-dependent
    // effect between runs must remain a separate pass and preserve ordering.
    BasicPostProcessEffect chromatic{BasicPostProcessEffectType::ChromaticAberration};
    chromatic.parameters[0].x = .001f;
    for (int count : {4, 11})
    {
        effects.resize(count, grade);
        if (count == 11) effects[9] = chromatic;
        reference.Render(glm::mat4(1), lighting, {&draw, 1}, effects);
        const auto referenceDraws = device.GetTimingStats("Scene").drawCalls;
        const auto original = readPixels(reference.GetColorTexture());
        renderer.Render(glm::mat4(1), lighting, {&draw, 1}, effects);
        const auto fusedDraws = device.GetTimingStats("Scene").drawCalls;
        if (referenceDraws > 0)
            require(fusedDraws < referenceDraws, "Color fusion did not reduce fullscreen draws");
        compare(original, readPixels(renderer.GetColorTexture()), 1.0, 4, "Fused color changed authored operations");
    }

    // Compare procedural instances to CPU-expanded geometry with asymmetric
    // positions, rotations and overlapping alpha, on two consecutive frames.
    BasicParticleDraw particle, expanded;
    particle.parameters.values[0] = glm::vec4(1);
    particle.parameters.values[2].x = 1;
    particle.parameters.values[3] = {1, 1, 0, 0};
    particle.parameters.view = glm::rotate(glm::mat4(1), .21f, glm::vec3(0,0,1));
    expanded.parameters = particle.parameters;
    for (int frame = 0; frame < 2; ++frame)
    {
        particle.instances.clear();
        expanded.vertices.clear();
        for (int index = 0; index < 3; ++index)
        {
            BasicParticleInstance instance;
            instance.centerRotation = {-.35f + index * .3f, -.2f + frame * .2f, .6f, .3f + index * .4f};
            instance.color = {.2f, .8f - index * .2f, .3f + index * .2f, .4f};
            instance.ageLifetimeRandomSize = {0, 1, 0, .7f};
            particle.instances.push_back(instance);
            constexpr std::array<glm::vec2, 4> corners{{{-.5f,-.5f}, {.5f,-.5f}, {-.5f,.5f}, {.5f,.5f}}};
            for (auto vertex : indices)
            {
                const auto corner = corners[vertex];
                const float cosine = std::cos(instance.centerRotation.w), sine = std::sin(instance.centerRotation.w);
                const glm::vec3 offset = glm::transpose(glm::mat3(particle.parameters.view)) *
                    glm::vec3((cosine * corner.x + sine * corner.y) * .7f,
                              (-sine * corner.x + cosine * corner.y) * .7f, 0);
                const glm::vec3 center(instance.centerRotation);
                expanded.vertices.push_back({center + offset, instance.color, corner + .5f,
                                             instance.ageLifetimeRandomSize, center});
            }
        }
        reference.Render(glm::mat4(1), lighting, {&draw,1}, {}, {}, PostProcessDebugView::None,
                         nullptr, nullptr, true, {}, {&expanded,1});
        const auto original = readPixels(reference.GetColorTexture());
        renderer.Render(glm::mat4(1), lighting, {&draw,1}, {}, {}, PostProcessDebugView::None,
                        nullptr, nullptr, true, {}, {&particle,1});
        compare(original, readPixels(renderer.GetColorTexture()), .1, 2, "Instanced particle output differs");
    }

    // Smooth media should reconstruct closely at odd dimensions. Include both
    // zero-density identity and illuminated media to catch empty/black traces.
    BasicPostProcessEffect fog{BasicPostProcessEffectType::VolumetricFog};
    fog.quality = 32;
    fog.parameters[0] = {.3f,.5f,.8f, .03f};
    fog.parameters[1] = {0,0,30,1};
    fog.parameters[2] = {0,1,0,1};
    fog.parameters[3].x = 1;
    BasicPostProcessEffect cloud{BasicPostProcessEffectType::VolumetricCloud};
    cloud.quality = 32u | (4u << 8u);
    cloud.parameters[0] = {1,1,1,1};
    cloud.parameters[1].w = .5f;
    cloud.parameters[2] = {.25f,.8f,.4f,.1f};
    cloud.parameters[3] = {1,.95f,.85f,2};
    cloud.parameters[4] = {.95f,.35f,.3f,.008f};
    cloud.worldToLocal = glm::inverse(glm::scale(glm::mat4(1), glm::vec3(20)));
    for (const auto &volume : {fog, cloud})
    {
        for (bool empty : {false, true})
        {
            auto effect = volume;
            if (empty) effect.parameters[effect.type == BasicPostProcessEffectType::VolumetricFog ? 0 : 1].w = 0;
            reference.Render(glm::mat4(1), lighting, {}, {&effect,1});
            const auto original = readPixels(reference.GetColorTexture());
            renderer.Render(glm::mat4(1), lighting, {}, {&effect,1});
            compare(original, readPixels(renderer.GetColorTexture()), empty ? 0.0 : 2.0, empty ? 0 : 15,
                    "Reduced volumetric reconstruction differs");
            effect.volumetricResolutionDivisor = 1;
            renderer.Render(glm::mat4(1), lighting, {}, {&effect,1});
            compare(original, readPixels(renderer.GetColorTexture()), 0.1, 1, "Full-resolution volumetric fallback differs");
        }
    }
    renderer.Resize(oldWidth, oldHeight);
}
