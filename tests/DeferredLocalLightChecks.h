#pragma once
// Deferred local lights (src/DeferredLocalLights.h) light eligible surfaces like the forward loop they replace, within
// the G-buffer normal's 8-bit precision; ineligible surfaces (clear coat) keep forward lighting and are not lit twice.
#include "PlutoGE/render/BasicRenderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

template <class ReadPixels>
void CheckDeferredLocalLights(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool value, const std::string &message) { if (!value) throw std::runtime_error(message); };
    constexpr std::uint32_t width = 160, height = 96;
    const auto previousWidth = renderer.GetWidth(), previousHeight = renderer.GetHeight();
    require(renderer.Resize(width, height), "Deferred local light fixture resize failed");

    std::vector<BasicVertex> vertices;
    std::vector<std::uint32_t> indices;
    constexpr unsigned divisions = 24;
    for (unsigned z = 0; z <= divisions; ++z)
        for (unsigned x = 0; x <= divisions; ++x)
        {
            BasicVertex vertex{};
            vertex.position = {float(x) / divisions * 30.0f - 15.0f, 0.0f, float(z) / divisions * 30.0f - 25.0f};
            vertex.normal = {0, 1, 0};
            vertex.uv = {float(x) / divisions, float(z) / divisions};
            vertices.push_back(vertex);
        }
    for (unsigned z = 0; z < divisions; ++z)
        for (unsigned x = 0; x < divisions; ++x)
        {
            const auto a = z * (divisions + 1) + x, b = a + divisions + 1;
            indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    auto plane = renderer.CreateMesh({vertices, indices});
    // A pillar between the shadowed light and the floor.
    std::vector<BasicVertex> boxVertices;
    std::vector<std::uint32_t> boxIndices;
    const glm::vec3 centre{0.5f, 1.0f, -6.0f}, half{0.4f, 1.0f, 0.4f};
    for (int axis = 0; axis < 3; ++axis)
        for (int sign = -1; sign <= 1; sign += 2)
        {
            glm::vec3 normal(0.0f);
            normal[axis] = float(sign);
            const glm::vec3 u = axis == 0 ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            const glm::vec3 v = glm::cross(normal, u);
            const auto base = static_cast<std::uint32_t>(boxVertices.size());
            for (const auto [a, b] : {std::pair{-1.f, -1.f}, std::pair{1.f, -1.f}, std::pair{1.f, 1.f}, std::pair{-1.f, 1.f}})
            {
                BasicVertex vertex{};
                const glm::vec3 position = centre + (normal + u * a + v * b) * half;
                vertex.position = {position.x, position.y, position.z};
                vertex.normal = {normal.x, normal.y, normal.z};
                boxVertices.push_back(vertex);
            }
            boxIndices.insert(boxIndices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
    auto box = renderer.CreateMesh({boxVertices, boxIndices});

    std::vector<BasicDraw> draws(2);
    draws[0].mesh = &plane;
    draws[0].baseColor = {0.8f, 0.7f, 0.6f, 1.0f};
    draws[0].roughness = 0.45f;
    draws[1].mesh = &box;
    draws[1].baseColor = {0.5f, 0.6f, 0.8f, 1.0f};
    draws[1].roughness = 0.3f;
    draws[1].metallic = 0.5f;
    draws[1].twoSided = true;

    BasicLighting lighting;
    lighting.directionalIntensity = 0.0f;
    lighting.ambientIntensity = 0.02f;
    for (unsigned index = 0; index < 6; ++index)
    {
        BasicPointLight light;
        light.position = {float(index % 3) * 6.0f - 6.0f, 0.8f + float(index % 2), -2.0f - float(index / 3) * 8.0f};
        light.range = 7.0f;
        light.color = {1.0f, 0.5f + 0.1f * float(index), 0.3f + 0.1f * float(index)};
        light.intensity = 8.0f;
        light.castsShadows = index == 1;
        lighting.pointLights.push_back(light);
    }
    BasicLighting::SpotLight spot;
    spot.light.position = {-3.0f, 3.0f, -10.0f};
    spot.light.range = 8.0f;
    spot.light.color = {0.6f, 0.8f, 1.0f};
    spot.light.intensity = 14.0f;
    spot.direction = {0.3f, -1.0f, 0.1f};
    lighting.spotLights.push_back(spot);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 4.0f, 4.0f), glm::vec3(0.0f, 0.0f, -8.0f), glm::vec3(0, 1, 0));
    const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), float(width) / height, 0.1f, 100.0f);
    lighting.view = view;
    lighting.cameraPosition = glm::vec3(glm::inverse(view)[3]);

    const auto original = renderer.GetDeferredLocalLights();
    const auto render = [&](bool deferred) {
        auto policy = original;
        policy.enabled = deferred;
        renderer.SetDeferredLocalLights(policy);
        renderer.Render(projection * view, lighting, draws);
        require(renderer.GetFrameStats().deferredLocalLights == deferred,
                deferred ? "Deferred local lights did not run" : "Forward reference ran the deferred pass");
        return readPixels(renderer.GetColorTexture());
    };
    const auto compare = [&](const char *label) {
        const auto forward = render(false);
        const auto deferred = render(true);
        require(forward.size() == deferred.size() && !forward.empty(), "Deferred local light images differ in size");
        double total = 0.0;
        std::vector<int> errors;
        int brightest = 0;
        for (std::size_t i = 0; i < forward.size(); ++i)
        {
            if (i % 4 == 3) continue;
            const int a = std::to_integer<int>(forward[i]), b = std::to_integer<int>(deferred[i]);
            brightest = std::max(brightest, a);
            errors.push_back(std::abs(a - b));
            total += std::abs(a - b);
        }
        std::sort(errors.begin(), errors.end());
        const double mean = total / errors.size();
        const int p99 = errors[errors.size() * 99 / 100], worst = errors.back();
        std::cout << "Deferred local lights (" << label << "): mean " << mean << ", p99 " << p99 << ", max " << worst << '\n';
        require(brightest > 64, std::string("Deferred local light fixture is unlit: ") + label);
        require(mean < 0.75 && p99 <= 4 && worst <= 24,
                std::string("Deferred local lights differ from the forward loop: ") + label);
    };
    compare("standard surfaces");
    // Clear coat keeps forward local lights; the deferred pass must leave its pixels alone.
    draws[0].clearCoat = 0.8f;
    draws[0].clearCoatRoughness = 0.2f;
    compare("clear coat floor");

    renderer.SetDeferredLocalLights(original);
    renderer.Resize(previousWidth, previousHeight);
}
