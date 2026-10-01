#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "../engine/render/src/ParticleVisibility.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

inline void LoadRenderOptimizationShaders(PlutoGE::render::BasicRendererShaderPackage &shaders)
{
    using namespace PlutoGE::render;
    const auto additions = ShaderArtifactLibrary(PLUTO_RHI_TEST_SHADER_DIR).LoadBasicRendererPackage();
    shaders.particleInstancedVertex = additions.particleInstancedVertex;
    shaders.standardFragment = additions.standardFragment;
    shaders.standardVertices = additions.standardVertices;
    shaders.standardColorVertices = additions.standardColorVertices;
    shaders.colorVertex = additions.colorVertex;
    shaders.colorInstancedVertex = additions.colorInstancedVertex;
    shaders.compactFragments = additions.compactFragments;
    shaders.coverageFragments = additions.coverageFragments;
    shaders.opaqueDepth = additions.opaqueDepth;
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
    {
        const std::array<std::byte, 64> constants{};
        rhi::Buffer persistent(device, device.CreateBuffer(
            {constants.size(), rhi::BufferUsage::Uniform, "Immutable residency regression", true}, constants));
        bool rejected = false;
        try { device.UpdateBuffer(persistent.Get(), 0, constants); }
        catch (const std::invalid_argument &) { rejected = true; }
        require(rejected, "Immutable parameter storage accepted an in-place mutation");
    }
    const ParticleVisibility visibility(glm::mat4(1));
    require(visibility.IsVisible({0, 0, 0}, 0.1f), "Visible particle culled");
    require(visibility.IsVisible({1.1f, 0, 0}, 0.2f), "Billboard crossing viewport edge culled");
    require(!visibility.IsVisible({1.5f, 0, 0}, 0.2f), "Offscreen particle survived culling");
    require(!visibility.IsVisible({0, 0, -2}, 0.2f), "Particle outside clip depth survived culling");

    // A second renderer without the optional pipelines provides a standalone
    // pass reference without changing global settings or production behavior.
    shaders.fusedColor = {};
    shaders.standardFragment = {};
    shaders.volumetricTrace = {};
    shaders.volumetricComposite = {};
    BasicRenderer reference;
    reference.SetGeometryOptimizations(false, false, false);
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
    // Deliberately collide preparation keys: retained materials must validate
    // the complete surface instead of treating a hash as resource identity.
    draw.preparationRevision = 1;
    draw.preparedMaterialHash = 42;
    BasicLighting lighting;
    lighting.ambientIntensity = lighting.directionalIntensity = 0;
    // More than the number of buffered frames: immutable geometry records
    // must stay resident, while a changed material must become visible.
    std::uint64_t coldPersistentBytes = 0;
    for (int frame = 0; frame < 8; ++frame)
    {
        renderer.Render(glm::mat4(1), lighting, {&draw, 1});
        if (frame == 0) coldPersistentBytes = device.GetTimingStats().persistentUniformBytesUploaded;
        if (frame >= 3)
        {
            require(renderer.GetFrameStats().materialPreparations == 0,
                "Stationary materials were reconstructed despite retained GPU records");
            require(renderer.GetFrameStats().geometryParameterCreates == 0, "Stationary geometry recreated parameter records");
            if (device.GetApi() == rhi::GraphicsApi::Vulkan)
                require(device.GetTimingStats().persistentUniformBytesUploaded == 0,
                    "Stationary parameters were reuploaded after all buffered frames warmed");
        }
    }
    if (device.GetApi() == rhi::GraphicsApi::Vulkan)
    {
        require(coldPersistentBytes > 0, "Residency test never populated persistent GPU storage");
        std::cout << "Stationary persistent geometry upload: " << coldPersistentBytes << " bytes cold, 0 bytes warm\n";
    }
    const auto stationaryImage = readPixels(renderer.GetColorTexture());
    draw.emission.x += .2f;
    renderer.Render(glm::mat4(1), lighting, {&draw, 1});
    require(renderer.GetFrameStats().geometryParameterCreates > 0, "Material edit reused stale GPU parameters");
    require(stationaryImage != readPixels(renderer.GetColorTexture()), "Material edit did not reach GPU");
    draw.emission.x -= .2f;
    require(renderer.Resize(width + 1, height), "Material viewport test resize failed");
    renderer.Render(glm::mat4(1), lighting, {&draw, 1});
    require(renderer.GetFrameStats().materialPreparations > 0, "Viewport change retained stale material constants");
    require(renderer.Resize(width, height), "Material viewport test restore failed");
    renderer.Render(glm::mat4(1), lighting, {&draw, 1});
    require(readPixels(renderer.GetColorTexture()) == stationaryImage, "Viewport round trip changed stationary material pixels");
    {
        auto animatedDraw = draw;
        auto program = std::make_shared<ShaderGraphProgram>();
        program->usesTime = true;
        animatedDraw.shaderGraphProgram = program;
        for (int frame = 0; frame < 2; ++frame)
        {
            renderer.Render(glm::mat4(1), lighting, {&animatedDraw, 1});
            require(renderer.GetFrameStats().materialPreparations > 0,
                "Time-dependent graph reused a previous frame's material record");
        }
    }
    // Compare the interpreter-free material variant against the full shader,
    // including instancing and both alpha coverage paths.
    for (int scenario = 0; scenario < 18; ++scenario)
    {
        auto materialDraw = draw;
        materialDraw.baseColor = {.2f, .4f, .6f, scenario % 2 ? .4f : 1.0f};
        materialDraw.alphaMode = scenario % 3;
        materialDraw.metallic = (scenario % 3) * .5f;
        materialDraw.roughness = .1f + (scenario % 3) * .45f;
        if (scenario >= 9)
        {
            auto instances = std::make_shared<std::vector<glm::mat4>>();
            for (float x : {-.4f, .4f})
                instances->push_back(glm::translate(glm::mat4(1), glm::vec3(x, 0, 0)) *
                                     glm::scale(glm::mat4(1), glm::vec3(.45f)));
            materialDraw.instanceModels = instances;
            materialDraw.previousInstanceModels = instances;
        }
        auto materialLighting = lighting;
        materialLighting.ambientIntensity = .2f;
        materialLighting.directionalIntensity = 1;
        materialLighting.directionalDirection = {-.2f, -.4f, -1};
        materialLighting.shadowsEnabled = false;
        materialLighting.cameraPosition = {0, 0, 3};
        materialLighting.pointLights = {{{.5f, .5f, 2}, 5, {1, .5f, .25f}, 2}};
        reference.Render(glm::mat4(1), materialLighting, {&materialDraw, 1});
        const auto original = readPixels(reference.GetColorTexture());
        renderer.Render(glm::mat4(1), materialLighting, {&materialDraw, 1});
        compare(original, readPixels(renderer.GetColorTexture()), .01, 1,
                "Standard material variant differs from interpreter shader");
    }
    // An opaque graph with custom lighting: its depth prepass uses the
    // material-free depth shader, and Direct Lighting reuses the surface
    // registers it shares with Albedo. Masked graphs keep graph coverage.
    const auto litGraph = [&]
    {
        ShaderGraph graph;
        graph.nodes = {{.id = 1, .kind = ShaderGraphNodeKind::MaterialInput},
            {.id = 2, .kind = ShaderGraphNodeKind::Float, .value = glm::vec4(.5f)},
            {.id = 3, .kind = ShaderGraphNodeKind::Multiply},
            {.id = 4, .kind = ShaderGraphNodeKind::ShadowAttenuation},
            {.id = 5, .kind = ShaderGraphNodeKind::Multiply},
            {.id = 100, .kind = ShaderGraphNodeKind::Output}};
        graph.links = {{1, 1, "Out", 3, "A"}, {2, 2, "Out", 3, "B"}, {3, 3, "Out", 100, "Albedo"},
            {4, 3, "Out", 5, "A"}, {5, 4, "Out", 5, "B"}, {6, 5, "Out", 100, "Direct Lighting"}};
        auto program = BuildShaderGraphProgram(graph);
        require(program && program->data.outputs1.w != 0 && program->data.header.z == 0,
            "Lit coverage graph did not compile as an opaque Direct Lighting program");
        return program;
    }();
    // Requirements must follow consumers on every frame, including transitions
    // at an unchanged viewport size. Compare against full-output forward rendering.
    for (int scenario = 0; scenario < 9; ++scenario)
    {
        auto surface = draw;
        if (scenario == 1) surface.model[0][0] = -1; // mirrored rigid winding
        if (scenario == 2) { surface.twoSided = true; surface.model[0][0] = -1; }
        if (scenario == 3) { surface.alphaMode = 1; surface.baseColor.a = 0; }
        if (scenario >= 7) surface.shaderGraphProgram = litGraph;
        if (scenario == 8) { surface.alphaMode = 1; surface.baseColor.a = 0; }
        auto surfaceLighting = lighting;
        if (scenario >= 7)
        {
            surfaceLighting.directionalIntensity = 1;
            surfaceLighting.directionalDirection = {-.2f, -.4f, -1};
            surfaceLighting.pointLights = {{{.5f, .5f, 2}, 5, {1, .5f, .25f}, 2}};
        }
        std::vector<BasicPostProcessEffect> requiredEffects;
        if (scenario == 4) requiredEffects.push_back({BasicPostProcessEffectType::MotionBlur});
        const auto debug = scenario == 5 ? PostProcessDebugView::Normal : PostProcessDebugView::None;
        reference.Render(glm::mat4(1), surfaceLighting, {&surface, 1}, requiredEffects, {}, debug);
        const auto original = readPixels(reference.GetColorTexture());
        renderer.Render(glm::mat4(1), surfaceLighting, {&surface, 1}, requiredEffects, {}, debug);
        compare(original, readPixels(renderer.GetColorTexture()), .01, 1,
                "Geometry coverage/output layout changed the reference image");
        const auto &stats = renderer.GetFrameStats();
        require(stats.geometryColorOutputs == (scenario == 5 ? 6u : scenario == 4 ? 2u : 1u),
                "Geometry output requirements did not follow active consumers");
        require(stats.geometryDepthDraws == 1, "Opaque coverage prepass was not recorded");
    }
    // Shadow derivatives must match the full-output shader, including helper
    // invocations at the receiver boundary and the first frame of a new pipeline.
    {
        auto receiver = draw;
        receiver.emission = {0, 0, 0};
        receiver.model[3].z = .6f;
        auto caster = receiver;
        caster.model = glm::translate(glm::mat4(1), glm::vec3(-.3f, 0, .1f)) *
            glm::rotate(glm::mat4(1), .25f, glm::vec3(0, 0, 1)) *
            glm::scale(glm::mat4(1), glm::vec3(.5f, 2, 1));
        auto shadowLighting = lighting;
        shadowLighting.shadowsEnabled = true;
        shadowLighting.shadowMethod = ShadowMethod::Cascaded;
        shadowLighting.shadowCascadeCount = 1;
        shadowLighting.shadowResolution = 64;
        shadowLighting.shadowMatrices[0] = glm::mat4(1);
        shadowLighting.directionalIntensity = 1;
        shadowLighting.directionalDirection = {0, 0, -1};
        shadowLighting.cameraPosition = {0, 0, 3};
        for (int frame = 0; frame < 3; ++frame)
        {
            reference.Render(glm::mat4(1), shadowLighting, {&receiver, 1}, {}, {&caster, 1});
            const auto original = readPixels(reference.GetColorTexture());
            renderer.Render(glm::mat4(1), shadowLighting, {&receiver, 1}, {}, {&caster, 1});
            compare(original, readPixels(renderer.GetColorTexture()), .01, 1,
                    "Compact shadow shading differs from the full-output reference");
        }
    }
    // A repeatable overdraw workload measures the complete geometry scope,
    // including coverage cost. Timings are diagnostic, never a flaky pass/fail gate.
    {
        require(renderer.Resize(490, 231), "Geometry benchmark resize failed");
        std::vector<BasicDraw> layers(32, draw);
        for (std::size_t i = 0; i < layers.size(); ++i)
        {
            layers[i].model[3].z = float(i) * .02f;
            layers[i].baseColor = {.3f, .5f, .7f, 1};
        }
        auto lit = lighting;
        lit.ambientIntensity = .2f;
        lit.directionalIntensity = 1;
        lit.directionalDirection = {-.2f, -.4f, -1};
        lit.cameraPosition = {0, 0, 3};
        decltype(readPixels(renderer.GetColorTexture())) baseline;
        for (bool optimized : {false, true})
        {
            renderer.SetGeometryOptimizations(optimized, optimized, optimized);
            double milliseconds = 0, recordingMilliseconds = 0, descriptorMilliseconds = 0;
            std::uint64_t materialPreparations = 0, descriptorBinds = 0;
            unsigned observations = 0, cpuSamples = 0;
            std::uint64_t lastObservation = 0;
            for (unsigned frame = 0; frame < 48; ++frame)
            {
                renderer.Render(glm::mat4(1), lit, layers);
                auto pixels = readPixels(renderer.GetColorTexture());
                if (!optimized && frame == 47) baseline = pixels;
                if (optimized && frame == 47)
                    require(pixels == baseline, "Depth-first overdraw benchmark changed the image");
                const auto timing = device.GetTimingStats("Scene");
                if (frame >= 12)
                {
                    recordingMilliseconds += renderer.GetTimingStats().geometryRecordingMs;
                    descriptorMilliseconds += timing.descriptorCpuMs;
                    descriptorBinds += timing.descriptorBindCalls;
                    materialPreparations += renderer.GetFrameStats().materialPreparations;
                    ++cpuSamples;
                }
                if (frame < 12 || !timing.hasGpuResult || timing.gpuObservationId == lastObservation) continue;
                lastObservation = timing.gpuObservationId;
                for (const auto &scope : timing.gpuScopes)
                    if (scope.name == "RHI Geometry") { milliseconds += scope.milliseconds; ++observations; }
            }
            if (observations) std::cout << "Core geometry overdraw " << (optimized ? "optimized" : "reference")
                << ": " << milliseconds / observations << " ms (" << observations << " observations)\n";
            if (cpuSamples) std::cout << "Geometry submission " << (optimized ? "optimized" : "reference")
                << ": recording=" << recordingMilliseconds / cpuSamples
                << " ms, descriptors=" << descriptorMilliseconds / cpuSamples
                << " ms, descriptor binds=" << double(descriptorBinds) / cpuSamples
                << ", material preparations=" << double(materialPreparations) / cpuSamples << '\n';
        }
        require(renderer.Resize(width, height), "Geometry benchmark restore failed");
    }
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
