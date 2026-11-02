#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/CloudShadowMap.h"
#include "PlutoGE/render/SkyEnvironmentConfig.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <span>
#include <stdexcept>

// Cloud shadows on a lit ground plane under a cloud deck: a clear sky leaves
// full sunlight, a closed deck leaves only overcast light, a broken deck casts
// patches through the shadow map (not the uniform deck estimate), and wind
// moves them. Also covers the map's CPU placement rules.
template <class ReadPixels>
void CheckCloudShadows(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    };

    BasicPostProcessEffect cloud{BasicPostProcessEffectType::VolumetricCloud};
    cloud.parameters[0] = {1, 1, 1, .5f};
    cloud.parameters[1] = {0, 0, 0, 1};
    cloud.parameters[2] = {0, 1, 0, .05f};
    cloud.parameters[3] = {1, 1, 1, 2};
    cloud.parameters[4] = {.9f, .6f, 0, .006f};
    cloud.parameters[5] = {.035f, .3f, 0, 0};
    cloud.parameters[CloudShadowLane] = {1, 1000, 1, 0};
    cloud.worldToLocal = glm::inverse(glm::translate(glm::mat4(1), glm::vec3(0, 600, 0)) *
                                      glm::scale(glm::mat4(1), glm::vec3(6000, 200, 6000)));

    {
        const auto map = BuildCloudShadowMap(std::span(&cloud, 1), {13, 20, -7}, {0, 1, 0});
        require(map.enabled, "Cloud shadow map must build for a shadow-casting deck");
        require(std::abs(map.pass.mapping.w - 500.f) < .01f, "Cloud shadow plane must lie at the deck's base");
        const float texel = map.pass.mapping.z;
        require(std::abs(texel - 2000.f / PLUTO_SKY_CLOUD_SHADOW_SIZE) < .001f, "Shadow distance must set the map's texel size");
        require(std::abs(std::remainder(map.pass.mapping.x, texel)) < .01f, "Cloud shadow map must snap to whole texels");
        const float centre = map.pass.mapping.x + .5f * PLUTO_SKY_CLOUD_SHADOW_SIZE * texel;
        require(std::abs(centre - 13.f) <= texel, "Overhead light must centre the map above the camera");
        const auto low = BuildCloudShadowMap(std::span(&cloud, 1), {0, 0, 0}, {1, .01f, 0});
        require(low.enabled && std::abs(low.pass.light.y - CloudShadowMap::MinimumElevation) < 1.e-5f,
                "A low light must project at the minimum elevation");
        require(!BuildCloudShadowMap(std::span(&cloud, 1), {0, 0, 0}, {0, -1, 0}).enabled,
                "A light below the horizon must not build a cloud shadow map");
        auto silent = cloud;
        silent.parameters[CloudShadowLane].x = 0;
        require(!BuildCloudShadowMap(std::span(&silent, 1), {0, 0, 0}, {0, 1, 0}).enabled,
                "Volumes that do not cast shadows must not build a map");
    }

    constexpr unsigned size = 33;
    renderer.Resize(size, size);
    BasicLighting lighting;
    lighting.cameraPosition = {0, 300, 0};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0), glm::vec3(0, 0, 1));
    lighting.ambientIntensity = 0;
    lighting.directionalDirection = {0, -1, 0};
    lighting.directionalColor = glm::vec3(1);
    lighting.directionalIntensity = 2;
    lighting.shadowsEnabled = false;
    // Reverse depth, as the renderer expects.
    const auto projection = glm::perspectiveRH_ZO(glm::radians(90.f), 1.f, 5000.f, .1f);
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-3000, 0, -3000}}, {{0, 1, 0}}, {{0, 0}}},
        {{{3000, 0, -3000}}, {{0, 1, 0}}, {{1, 0}}},
        {{{3000, 0, 3000}}, {{0, 1, 0}}, {{1, 1}}},
        {{{-3000, 0, 3000}}, {{0, 1, 0}}, {{0, 1}}}
    }};
    constexpr std::array<std::uint32_t, 6> indices{0, 2, 1, 0, 3, 2};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw draw;
    draw.mesh = &mesh;
    draw.roughness = 1;
    struct Statistics { float mean = 0, minimum = 1, maximum = 0; };
    const auto render = [&](std::span<const BasicPostProcessEffect> effects)
    {
        renderer.Render(projection * lighting.view, lighting, std::span(&draw, 1), effects);
        const auto pixels = readPixels(renderer.GetColorTexture());
        require(pixels.size() == size * size * 4, "Cloud shadow readback size mismatch");
        Statistics result;
        for (unsigned i = 0; i < size * size; ++i)
        {
            const float value = static_cast<int>(pixels[i * 4 + 1]) / 255.f;
            result.mean += value / (size * size);
            result.minimum = std::min(result.minimum, value);
            result.maximum = std::max(result.maximum, value);
        }
        return result;
    };

    const auto clear = render({});
    require(clear.mean > .45f && clear.maximum - clear.minimum < .05f, "A clear sky must leave uniform full sunlight");

    auto overcast = cloud;
    overcast.parameters[0].w = 1;
    const auto closed = render(std::span(&overcast, 1));
    // Real overcast days receive roughly a tenth to a third of clear-sky light, all of it diffuse.
    require(closed.mean < clear.mean * .3f, "A closed cloud deck must leave only overcast light");
    require(closed.maximum < clear.mean * .7f, "No part of a closed cloud deck may let full sunlight through");

    const auto broken = render(std::span(&cloud, 1));
    require(broken.maximum - broken.minimum > .25f * clear.mean, "A broken deck must cast distinct cloud shadows");
    require(broken.maximum > clear.mean * .8f, "Gaps in a broken deck must keep near-full sunlight");

    auto coarse = cloud;
    coarse.parameters[CloudShadowLane].x = 0;
    const auto uniform = render(std::span(&coarse, 1));
    require(uniform.maximum - uniform.minimum < .05f, "Without the map a deck must shade uniformly");

    // Moving the clouds by a few hundred metres must move their shadows.
    renderer.Render(projection * lighting.view, lighting, std::span(&draw, 1), std::span(&cloud, 1));
    const auto before = readPixels(renderer.GetColorTexture());
    auto windy = cloud;
    windy.parameters[1] = {350, 0, 120, 1};
    renderer.Render(projection * lighting.view, lighting, std::span(&draw, 1), std::span(&windy, 1));
    const auto after = readPixels(renderer.GetColorTexture());
    float change = 0;
    for (unsigned i = 0; i < size * size; ++i)
        change += std::abs(int(before[i * 4 + 1]) - int(after[i * 4 + 1])) / (255.f * size * size);
    require(change > .03f, "Wind must move cloud shadows");

    std::cout << "Cloud shadows: clear " << clear.mean << ", overcast " << closed.mean << ", broken "
              << broken.minimum << ".." << broken.maximum << ", wind change " << change << '\n';
}
