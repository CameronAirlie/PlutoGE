#pragma once
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RhiPostProcessAdapter.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Disabling a post-process effect must remove all of its cost, not only its output: a camera holding a disabled
// effect must record exactly the GPU work, and produce exactly the image, of the same camera without it. Checked for
// every effect type the RHI renderer supports, alone and all together. GPU scopes are compared when the backend reports
// them (`requireGpuScopes` fails the check when it does not).
template <class Device>
void CheckDisabledPostProcessEffects(Device &device, const PlutoGE::render::BasicRendererShaderPackage &shaders,
                                     const std::function<std::vector<std::byte>(PlutoGE::render::rhi::TextureHandle)> &readPixels,
                                     bool requireGpuScopes)
{
    using namespace PlutoGE::render;
    const auto require = [](bool ok, const std::string &message) { if (!ok) throw std::runtime_error(message); };
    const auto makeQuad = [](glm::vec3 center, glm::vec3 u, glm::vec3 v)
    {
        const glm::vec3 normal = glm::normalize(glm::cross(u, v));
        MeshConfig config;
        for (const auto corner : {center - u - v, center + u - v, center + u + v, center - u + v})
            config.data.vertices.push_back({{corner.x, corner.y, corner.z}, {normal.x, normal.y, normal.z}, {0, 0}, {1, 0, 0, 1}});
        config.data.indices = {0, 1, 2, 0, 2, 3};
        return std::make_unique<Mesh>(config);
    };
    // A glossy floor with a wall standing on it: shadows, occlusion, reflections and bounce light all have work.
    auto floorMesh = makeQuad({0, 0, 0}, {4, 0, 0}, {0, 0, -4});
    auto wallMesh = makeQuad({0, 1, -1}, {2, 0, 0}, {0, 1, 0});
    Material floor({.color = {0.6f, 0.6f, 0.6f, 1.0f}, .roughness = 0.3f});
    Material wall({.color = {0.8f, 0.3f, 0.2f, 1.0f}});
    const std::array commands{RenderCommand{.material = &floor, .mesh = floorMesh.get()},
                              RenderCommand{.material = &wall, .mesh = wallMesh.get()}};
    constexpr std::uint32_t size = 96;
    CameraData camera{.view = glm::lookAtRH(glm::vec3(0, 2, 5), glm::vec3(0, 0.3f, 0), glm::vec3(0, 1, 0)),
                      .projection = glm::perspective(glm::radians(60.0f), 1.0f, 100.0f, 0.1f)};
    BasicLighting lighting;
    lighting.cameraPosition = {0, 2, 5};
    lighting.view = camera.view;
    lighting.directionalDirection = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    lighting.directionalIntensity = 3.0f;
    lighting.ambientIntensity = 0.3f;
    lighting.shadowsEnabled = true;

    RhiSceneRenderer renderer;
    require(renderer.Initialize(device, shaders), "Disabled-effect renderer initialization failed");
    device.GetImmediateContext().SetGpuProfilingEnabled(true);
    struct Result
    {
        std::vector<std::byte> image;
        std::set<std::string> scopes;
        bool observed = false;
    };
    // Settles for eight frames (GPU observations lag a few), then gathers the scopes of the next eight.
    const auto run = [&](std::span<IPostProcessEffect *const> effects)
    {
        Result result;
        std::uint64_t previousObservation = device.GetTimingStats("Scene").gpuObservationId;
        for (int frame = 0; frame < 16; ++frame)
        {
            require(renderer.Render(size, size, camera, lighting, commands, commands, effects), "Disabled-effect render failed");
            result.image = readPixels(renderer.GetColorTexture());
            const auto timing = device.GetTimingStats("Scene");
            const bool fresh = timing.hasGpuResult && timing.gpuObservationId != 0 && timing.gpuObservationId != previousObservation;
            previousObservation = timing.gpuObservationId;
            if (frame < 8 || !fresh) continue;
            result.observed = true;
            for (const auto &scope : timing.gpuScopes) result.scopes.insert(scope.name);
        }
        return result;
    };
    const auto describe = [](const std::set<std::string> &extra, const std::set<std::string> &missing)
    {
        std::string text;
        for (const auto &name : extra) text += " +" + name;
        for (const auto &name : missing) text += " -" + name;
        return text;
    };
    const auto compare = [&](const Result &baseline, const Result &disabled, const std::string &label)
    {
        require(disabled.image == baseline.image, label + " changed the image while disabled");
        if (!baseline.observed || !disabled.observed)
        {
            require(!requireGpuScopes, "GPU scopes were not observed for " + label);
            return;
        }
        std::set<std::string> extra, missing;
        for (const auto &name : disabled.scopes) if (!baseline.scopes.contains(name)) extra.insert(name);
        for (const auto &name : baseline.scopes) if (!disabled.scopes.contains(name)) missing.insert(name);
        require(extra.empty() && missing.empty(), label + " still records GPU work while disabled:" + describe(extra, missing));
    };

    const auto baseline = run({});
    std::vector<std::unique_ptr<IPostProcessEffect>> owned;
    std::vector<IPostProcessEffect *> every;
    std::vector<std::string> checked;
    for (const auto &type : GetRegisteredPostProcessEffectTypes())
    {
        if (!IsRhiPostProcessEffectSupported(type)) continue;
        auto effect = CreatePostProcessEffect(type);
        require(effect != nullptr, "Could not create post-process effect " + type);
        effect->SetEnabled(false);
        IPostProcessEffect *const single = effect.get();
        compare(baseline, run(std::span(&single, 1)), type);
        every.push_back(single);
        owned.push_back(std::move(effect));
        checked.push_back(type);
    }
    require(!checked.empty(), "No RHI post-process effects were checked");
    compare(baseline, run(every), "Every effect");

    // The comparison above is only meaningful if enabling the same effects does record their work.
    for (auto *effect : every) effect->SetEnabled(true);
    const auto enabled = run(every);
    if (baseline.observed && enabled.observed)
        require(enabled.scopes.size() > baseline.scopes.size(), "Enabled effects recorded no GPU work of their own");
    std::cout << "Disabled post-process effects record no work (" << checked.size() << " types";
    if (baseline.observed) std::cout << "; " << baseline.scopes.size() << " GPU scopes without them, "
                                     << enabled.scopes.size() << " with them enabled";
    std::cout << ")\n";
}
