#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/PhysicalSkyModel.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>
#include <vector>

// Renders the physical sky at night through the backend-neutral renderer:
// moon phase and shading, stars, moonlit scattering, and daytime invariance.
template<class ReadPixels>
void CheckNightSky(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    constexpr int size = 192;
    renderer.Resize(size, size);

    const auto elevated = [](float elevation, float azimuth)
    {
        const float e = glm::radians(elevation), a = glm::radians(azimuth);
        return glm::vec3(std::cos(e) * std::sin(a), std::sin(e), -std::cos(e) * std::cos(a));
    };
    const auto render = [&](const PhysicalSkySettings &sky, const glm::vec3 &sun, const glm::vec3 &forward,
                            float fieldOfView)
    {
        const auto celestial = ResolveCelestialState(sky, sun);
        BasicPostProcessEffect effect{BasicPostProcessEffectType::PhysicalSky};
        effect.exposure = sky.exposure;
        effect.parameters = PackPhysicalSkyParameters(sky, celestial);
        effect.worldToLocal = glm::mat4(celestial.worldToCelestial);
        BasicLighting lighting;
        lighting.directionalIntensity = 0.0f;
        lighting.shadowsEnabled = false;
        lighting.view = glm::lookAtRH(glm::vec3(0.0f), forward, glm::vec3(0.0f, 1.0f, 0.0f));
        const auto projection = glm::perspectiveRH_ZO(glm::radians(fieldOfView), 1.0f, 5000.0f, 0.1f);
        renderer.Render(projection * lighting.view, lighting, {}, std::span(&effect, 1));
        auto pixels = readPixels(renderer.GetColorTexture());
        if (pixels.size() != std::size_t(size * size * 4))
            throw std::runtime_error("Night sky readback failed");
        return pixels;
    };
    const auto luminance = [](const auto &pixels, std::size_t pixel)
    { return int(pixels[pixel * 4]) + int(pixels[pixel * 4 + 1]) + int(pixels[pixel * 4 + 2]); };
    const auto total = [&](const auto &pixels)
    {
        long long sum = 0;
        for (std::size_t pixel = 0; pixel < std::size_t(size * size); ++pixel) sum += luminance(pixels, pixel);
        return sum;
    };

    PhysicalSkySettings sky;
    sky.starIntensity = 0.0f;
    sky.starTwinkle = 0.0f; // Twinkle follows frame time; keep comparisons deterministic.
    sky.milkyWayIntensity = 0.0f;
    const glm::vec3 midnightSun = elevated(-50.0f, 200.0f);

    // A full moon is a bright disc; a thin crescent is far dimmer.
    sky.moonAngularRadius = 3.0f;
    const auto fullMoonDirection = ResolveCelestialState(sky, midnightSun).moonDirection;
    const auto fullMoon = render(sky, midnightSun, fullMoonDirection, 20.0f);
    const std::size_t centre = std::size_t(size / 2) * size + size / 2;
    if (luminance(fullMoon, centre) < 120)
        throw std::runtime_error("The full moon disc is missing or too dim");

    // Quarter moon, with the sun due east: only the eastern (screen-right) half is lit.
    PhysicalSkySettings quarter = sky;
    quarter.moonPlacement = MoonPlacement::Manual;
    quarter.moonAzimuth = 0.0f;
    quarter.moonElevation = 45.0f;
    quarter.moonEarthshine = 0.0f;
    const auto quarterMoon = render(quarter, elevated(-10.0f, 90.0f), elevated(45.0f, 0.0f), 20.0f);
    long long left = 0, right = 0;
    for (int y = size / 2 - 10; y < size / 2 + 10; ++y)
        for (int x = 0; x < size; ++x)
            (x < size / 2 ? left : right) += luminance(quarterMoon, std::size_t(y) * size + x);
    if (right < left * 3 || right < 2000)
        throw std::runtime_error("The quarter moon is not lit from the sun's side");

    // Moonlight scatters into the sky away from the disc.
    PhysicalSkySettings moonlit = sky;
    moonlit.exposure = 16.0f;
    moonlit.moonSkyIntensity = 0.05f;
    const glm::vec3 awayFromMoon = elevated(30.0f, 200.0f);
    PhysicalSkySettings moonless = moonlit;
    moonless.moonSkyIntensity = 0.0f;
    if (total(render(moonlit, midnightSun, awayFromMoon, 40.0f)) <=
        total(render(moonless, midnightSun, awayFromMoon, 40.0f)) + size * size)
        throw std::runtime_error("Moonlight does not brighten the night sky");

    // Stars appear as distinct points in a narrow field, and only when enabled.
    PhysicalSkySettings starry = sky;
    starry.starIntensity = 2.0f;
    const glm::vec3 zenithward = elevated(70.0f, 200.0f);
    const auto stars = render(starry, midnightSun, zenithward, 10.0f);
    const auto background = render(sky, midnightSun, zenithward, 10.0f);
    int starPixels = 0;
    for (std::size_t pixel = 0; pixel < std::size_t(size * size); ++pixel)
        starPixels += luminance(stars, pixel) > luminance(background, pixel) + 60 ? 1 : 0;
    if (starPixels < 3)
        throw std::runtime_error("The star field rendered no visible stars");
    if (starPixels > size * size / 50)
        throw std::runtime_error("The star field is not made of distinct points");

    // Night-only settings must not change the daytime sky.
    PhysicalSkySettings dayA = sky, dayB = sky;
    dayB.starIntensity = 5.0f;
    dayB.milkyWayIntensity = 3.0f;
    dayB.moonSkyIntensity = 0.5f;
    dayB.lightPollutionIntensity = 1.0f;
    const glm::vec3 noon = elevated(50.0f, 200.0f);
    if (render(dayA, noon, elevated(20.0f, 0.0f), 60.0f) != render(dayB, noon, elevated(20.0f, 0.0f), 60.0f))
        throw std::runtime_error("Night-sky settings changed the daytime sky");
}
