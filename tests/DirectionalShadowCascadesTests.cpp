#include "DirectionalShadowCascades.h"

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE::render;

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    constexpr std::uint32_t Resolution = 2048;

    CameraData ViewFrom(const glm::vec3 &position, float yawDegrees)
    {
        const float yaw = glm::radians(yawDegrees);
        const glm::vec3 forward(std::sin(yaw), -0.2f, -std::cos(yaw));
        CameraData camera;
        camera.view = glm::lookAtRH(position, position + glm::normalize(forward), glm::vec3(0, 1, 0));
        camera.projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f);
        camera.nearPlane = 0.1f;
        camera.farPlane = 500.0f;
        return camera;
    }

    DirectionalShadowProjection Fit(const CameraData &camera, const BasicLighting &lighting)
    {
        return BuildDirectionalShadowProjection(camera, lighting, 20.0f, 60.0f, 150.0f, Resolution);
    }

    // A world point's position in a map's texel grid.
    glm::vec2 Texel(const glm::mat4 &matrix, const glm::vec3 &world)
    {
        const glm::vec4 clip = matrix * glm::vec4(world, 1.0f);
        return (glm::vec2(clip) * 0.5f + 0.5f) * static_cast<float>(Resolution);
    }
}

int main() try
{
    BasicLighting lighting;
    lighting.directionalDirection = glm::normalize(glm::vec3(0.4f, -0.8f, 0.3f));
    lighting.shadowSoftness = 1.5f;

    DirectionalShadowCascadeRetention retention;
    auto first = Fit(ViewFrom({0, 10, 0}, 0), lighting);
    const auto held = first;
    Require(!retention.Retain(0, first), "The first fit was reported as retained");
    Require(CoversDirectionalShadowSlice(held, Fit(ViewFrom({0, 10, 0}, 0), lighting)), "A fit does not cover its own slice");

    // A small step keeps the projection, and it samples the world on the same texel grid as a fresh fit.
    auto stepped = Fit(ViewFrom({0.05f, 10, -0.05f}, 0.2f), lighting);
    const auto fresh = stepped;
    Require(retention.Retain(0, stepped), "A small camera step refitted the cascade");
    Require(stepped.matrix == held.matrix, "The retained projection is not the held one");
    for (const glm::vec3 world : {glm::vec3(3, 0, -30), glm::vec3(-12.5f, 4, -45), glm::vec3(20, -2, -25)})
    {
        const auto a = Texel(held.matrix, world), b = Texel(fresh.matrix, world);
        const auto phase = glm::abs((a - glm::floor(a)) - (b - glm::floor(b)));
        Require(glm::all(glm::lessThan(glm::min(phase, 1.0f - phase), glm::vec2(0.02f))),
                "A retained projection samples a different texel phase than a fresh fit");
    }

    // Walking keeps a projection for many frames and refits as it is about to stop covering the slice.
    std::uint32_t refits = 0;
    constexpr int Frames = 240;
    for (int frame = 1; frame <= Frames; ++frame)
    {
        auto projection = Fit(ViewFrom({0.08f * frame, 10, -0.05f * frame}, 0.1f * frame), lighting);
        const auto current = projection;
        if (!retention.Retain(0, projection)) ++refits;
        Require(CoversDirectionalShadowSlice(projection, current), "A retained projection no longer covers the slice");
    }
    Require(refits > 0 && refits < Frames / 4, "Walking refitted too often or never");

    // A teleport or a different light refits.
    auto far = Fit(ViewFrom({400, 10, 400}, 0), lighting);
    Require(!retention.Retain(0, far), "A teleport kept a projection that cannot cover the new slice");
    auto moved = lighting;
    moved.directionalDirection = glm::normalize(glm::vec3(0.41f, -0.8f, 0.3f));
    auto relit = Fit(ViewFrom({400, 10, 400}, 0), moved);
    Require(!retention.Retain(0, relit), "A new light direction kept the old projection");
    // Cascades are independent, and Reset drops what each holds.
    auto other = Fit(ViewFrom({400, 10, 400}, 0), moved);
    Require(!retention.Retain(1, other), "Another cascade inherited a projection");
    retention.Reset();
    auto reset = Fit(ViewFrom({400, 10, 400}, 0), moved);
    Require(!retention.Retain(0, reset), "Reset kept a projection");

    std::cout << "Directional shadow cascade tests passed (" << refits << " refits over " << Frames << " frames)\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
