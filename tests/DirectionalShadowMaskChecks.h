#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Camera.h"
#include "../engine/render/src/DirectionalShadowCascades.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

// The shadow terminator: a smooth-shaded mesh of flat triangles is lit by its interpolated normals, but the shadow map
// holds its flat triangles. Near the terminator, a triangle whose own plane already turns from the sun while its
// normals still face it lies behind the neighbouring triangles' silhouette, and a shadow lookup from its plane finds
// them: the triangle darkens on its own, and the terminator steps along the polygons. Close to the camera virtual
// shadow texels are finer than the triangles, so the bias no longer hides it. A convex body casts no shadow on itself,
// so a lone smooth sphere of limb-sized triangles, seen up close, must look the same with shadows as without.
template<class ReadPixels>
void CheckDirectionalShadowTerminator(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error(message);
    };
    constexpr std::uint32_t Width = 320, Height = 240;
    renderer.Resize(Width, Height);
    // A limb: 6 cm radius, 24 segments around (1.6 cm triangles, as on a character's body), smooth normals.
    constexpr int Segments = 24, Rings = 12;
    constexpr float Radius = 0.06f;
    std::vector<BasicVertex> vertices;
    std::vector<std::uint32_t> indices;
    for (int ring = 0; ring <= Rings; ++ring)
        for (int segment = 0; segment <= Segments; ++segment)
        {
            const float theta = glm::pi<float>() * float(ring) / Rings;
            const float phi = glm::two_pi<float>() * float(segment) / Segments;
            const glm::vec3 normal(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            BasicVertex vertex;
            vertex.position = {normal.x * Radius, normal.y * Radius, normal.z * Radius};
            vertex.normal = {normal.x, normal.y, normal.z};
            vertex.uv = {float(segment) / Segments, float(ring) / Rings};
            vertices.push_back(vertex);
        }
    for (int ring = 0; ring < Rings; ++ring)
        for (int segment = 0; segment < Segments; ++segment)
        {
            const std::uint32_t a = ring * (Segments + 1) + segment, b = a + Segments + 1;
            indices.insert(indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    auto sphere = renderer.CreateMesh({vertices, indices});
    BasicDraw draw;
    draw.mesh = &sphere;
    draw.baseColor = {0.8f, 0.8f, 0.8f, 1};
    draw.roughness = 1.0f;

    BasicLighting lighting;
    lighting.shadowsEnabled = true;
    lighting.shadowMethod = ShadowMethod::Virtual;
    // The sun from the side and a little above, so the terminator crosses the view.
    lighting.directionalDirection = glm::normalize(glm::vec3(-1.0f, -0.35f, 0.15f));
    lighting.directionalIntensity = 3.0f;
    lighting.ambientIntensity = 0.05f;
    lighting.cameraPosition = {0.0f, 0.02f, 0.22f};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    const auto projection = glm::perspectiveRH_ZO(glm::radians(50.0f), float(Width) / Height, 100.0f, 0.02f);
    const auto render = [&](bool mask, bool shadows)
    {
        renderer.SetDirectionalShadowMask(mask);
        auto frameLighting = lighting;
        frameLighting.shadowsEnabled = shadows;
        for (int frame = 0; frame < 6; ++frame)
            renderer.Render(projection * frameLighting.view, frameLighting, {&draw, 1});
        return readPixels(renderer.GetColorTexture());
    };
    // Plain, with subsurface scattering (whose wrapped lighting reaches past the terminator, onto the triangles that
    // shadow themselves), two-sided as well (a character's skin: its casters draw back faces too), and with the
    // directional light's screen-space contact shadows, which trace the same triangles' depth.
    struct Variant { const char *name; float subsurface; bool twoSided; float contactShadowLength = 0.0f; };
    std::string failures;
    for (const Variant variant : {Variant{"plain", 0.0f, false}, Variant{"subsurface", 0.2f, false},
                                  Variant{"two-sided subsurface", 0.2f, true},
                                  Variant{"two-sided subsurface, contact shadows", 0.2f, true, 0.5f}})
    {
        draw.subsurface = variant.subsurface;
        draw.subsurfaceColor = {0.97f, 0.38f, 0.27f};
        draw.subsurfaceRadius = 1.0f;
        draw.twoSided = variant.twoSided;
        lighting.contactShadowLength = variant.contactShadowLength;
        const auto unshadowed = render(false, false);
        for (const bool mask : {false, true})
        {
            const auto shadowed = render(mask, true);
            const std::string name = std::string(variant.name) + (mask ? ", mask" : ", filter");
            // Each channel's darkening counts up to 8 levels: a two-sided silhouette's few grazing specular sparkles
            // (which differ between any two renders) must not outweigh the shaded band. Before the terminator lift the
            // plain sphere measured 0.0137 and the subsurface ones about 0.08; after it, at most 0.005.
            double darkening = 0.0;
            for (std::size_t i = 0; i < shadowed.size(); ++i)
                if (i % 4 != 3) darkening += std::min(std::max(int(unshadowed[i]) - int(shadowed[i]), 0), 8);
            darkening /= double(Width) * Height * 3.0;
            std::cout << "Directional shadow terminator (" << name << "): darkening " << darkening << " mean\n";
            if (darkening >= 0.008) failures += (failures.empty() ? "" : "; ") + name;
        }
    }
    require(failures.empty(), "shadow terminator (" + failures + "): a smooth convex mesh must not shadow its own triangles");
}

// The directional shadow mask (DirectionalShadowMask.h) filters the directional shadow once per pixel of the coverage
// depth, from receivers rebuilt from that depth, and prepassed surfaces read it. Planar receivers rebuild exactly, so
// a scene of boxes on a ground must render as the surfaces' own filter does, apart from silhouette pixels, with
// virtual shadow maps and with cascades. The shadows must be real (the comparison would otherwise pass trivially), and
// the mask must actually be in use.
template<class ReadPixels>
void CheckDirectionalShadowMask(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error(message);
    };
    const auto width = renderer.GetWidth(), height = renderer.GetHeight();
    constexpr std::uint32_t Width = 160, Height = 120;
    renderer.Resize(Width, Height);
    constexpr std::array<BasicVertex, 8> vertices = {{
        {{{-.5f,-.5f,-.5f}},{{0,0,-1}},{{0,0}}}, {{{.5f,-.5f,-.5f}},{{0,0,-1}},{{1,0}}},
        {{{.5f,.5f,-.5f}},{{0,0,-1}},{{1,1}}}, {{{-.5f,.5f,-.5f}},{{0,0,-1}},{{0,1}}},
        {{{-.5f,-.5f,.5f}},{{0,0,1}},{{0,0}}}, {{{.5f,-.5f,.5f}},{{0,0,1}},{{1,0}}},
        {{{.5f,.5f,.5f}},{{0,0,1}},{{1,1}}}, {{{-.5f,.5f,.5f}},{{0,0,1}},{{0,1}}}}};
    constexpr std::array<std::uint32_t, 36> indices{
        0,2,1, 0,3,2, 4,5,6, 4,6,7, 0,1,5, 0,5,4, 3,6,2, 3,7,6, 0,4,7, 0,7,3, 1,2,6, 1,6,5};
    constexpr std::array<BasicVertex, 4> planeVertices = {{
        {{{-1,0,-1}},{{0,1,0}},{{0,0}}}, {{{1,0,-1}},{{0,1,0}},{{1,0}}},
        {{{1,0,1}},{{0,1,0}},{{1,1}}}, {{{-1,0,1}},{{0,1,0}},{{0,1}}}}};
    constexpr std::array<std::uint32_t, 6> planeIndices{0,2,1, 0,3,2};
    auto cube = renderer.CreateMesh({vertices, indices});
    auto plane = renderer.CreateMesh({planeVertices, planeIndices});

    std::vector<BasicDraw> draws;
    BasicDraw ground;
    ground.mesh = &plane;
    ground.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, -1, 0)), glm::vec3(8));
    draws.push_back(ground);
    for (int i = 0; i < 6; ++i)
    {
        BasicDraw box;
        box.mesh = &cube;
        // Some boxes turned, so receivers face the light at several slopes.
        box.model = glm::rotate(glm::translate(glm::mat4(1), glm::vec3(float(i % 3) * 1.5f - 1.5f, -0.4f, float(i / 3) * 1.6f - 2.6f)),
                                glm::radians(20.0f * float(i)), glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), glm::vec3(0.7f, 1.2f, 0.7f));
        box.baseColor = {0.7f, 0.65f, 0.6f, 1};
        draws.push_back(box);
    }

    BasicLighting lighting;
    lighting.shadowsEnabled = true;
    lighting.directionalDirection = glm::normalize(glm::vec3(-0.6f, -1.0f, -0.4f));
    lighting.directionalIntensity = 3.0f;
    lighting.ambientIntensity = 0.2f;
    lighting.cameraPosition = {0, 3.5f, 4.5f};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0, -0.8f, -1.5f), glm::vec3(0, 1, 0));
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), float(Width) / Height, 100.0f, 0.1f);

    for (const auto method : {ShadowMethod::Virtual, ShadowMethod::Cascaded})
    {
        const char *name = method == ShadowMethod::Virtual ? "virtual shadow maps" : "cascades";
        lighting.shadowMethod = method;
        if (method == ShadowMethod::Cascaded)
        {
            // One cascade fitted to the view, as RhiSceneRenderer fits them (Vulkan: the map's rows flip).
            lighting.shadowCascadeCount = 1;
            lighting.shadowResolution = 1024;
            lighting.shadowFlipY = true;
            CameraData camera{lighting.view, projection, 0.1f, 100.0f};
            const auto cascade = BuildDirectionalShadowProjection(camera, lighting, 0.1f, 20.0f, 20.0f, lighting.shadowResolution);
            lighting.shadowMatrices[0] = cascade.matrix;
            lighting.shadowCascadeMetrics[0] = {cascade.worldTexelSize, cascade.depthRange, 0.0f, 0.0f};
            lighting.shadowCascadeSplits[0] = 20.0f;
        }
        const auto render = [&](bool mask, bool shadows)
        {
            renderer.SetDirectionalShadowMask(mask);
            auto frameLighting = lighting;
            frameLighting.shadowsEnabled = shadows;
            // Virtual shadow pages converge over a few frames.
            for (int frame = 0; frame < 6; ++frame)
                renderer.Render(projection * frameLighting.view, frameLighting, draws);
            return std::pair{readPixels(renderer.GetColorTexture()), renderer.GetFrameStats().directionalShadowMask};
        };
        const auto [unshadowed, unused] = render(false, false);
        const auto [filtered, filteredMask] = render(false, true);
        const auto [masked, maskUsed] = render(true, true);
        (void)unused;
        require(!filteredMask, std::string(name) + ": the mask must be off when disabled");
        require(maskUsed, std::string(name) + ": prepassed surfaces must read the directional shadow mask");
        double shadowed = 0.0, difference = 0.0;
        std::size_t different = 0;
        for (std::size_t i = 0; i < filtered.size(); ++i)
        {
            if (i % 4 == 3) continue;
            shadowed += std::abs(int(unshadowed[i]) - int(filtered[i]));
            const int delta = std::abs(int(masked[i]) - int(filtered[i]));
            difference += delta;
            if (delta > 16) ++different;
        }
        const double channels = double(Width) * Height * 3.0;
        std::cout << "Directional shadow mask (" << name << "): shadows " << shadowed / channels << ", mask against filter "
                  << difference / channels << " mean, " << 100.0 * double(different) / channels << "% beyond 16\n";
        require(shadowed / channels > 1.5, std::string(name) + ": the fixture must cast visible shadows");
        require(difference / channels < 0.6, std::string(name) + ": the mask must match the surfaces' own filter");
        require(double(different) / channels < 0.015, std::string(name) + ": only silhouette pixels may differ");
    }

    // A camera low over open ground sees it at grazing angles: one pixel's step across the ground grows to many pixel
    // footprints near the horizon. The mask must still rebuild the ground plane there, or the receiver turns towards
    // the camera, its bias and filter plane go wrong, and the ground shadows itself in a dark band (no caster here).
    {
        BasicDraw field;
        field.mesh = &plane;
        field.model = glm::scale(glm::mat4(1), glm::vec3(400.0f, 1.0f, 400.0f));
        field.baseColor = {0.7f, 0.65f, 0.6f, 1};
        auto grazing = lighting;
        grazing.shadowMethod = ShadowMethod::Virtual;
        grazing.directionalDirection = glm::normalize(glm::vec3(-0.5f, -0.35f, -0.8f));
        grazing.cameraPosition = {0, 1.0f, 0};
        grazing.view = glm::lookAtRH(grazing.cameraPosition, glm::vec3(0, 0.6f, -20.0f), glm::vec3(0, 1, 0));
        const auto render = [&](bool mask, bool shadows)
        {
            renderer.SetDirectionalShadowMask(mask);
            auto frameLighting = grazing;
            frameLighting.shadowsEnabled = shadows;
            for (int frame = 0; frame < 6; ++frame)
                renderer.Render(projection * frameLighting.view, frameLighting, {&field, 1});
            return readPixels(renderer.GetColorTexture());
        };
        const auto unshadowed = render(false, false);
        const auto filtered = render(false, true);
        const auto masked = render(true, true);
        double filterDarkening = 0.0, maskDarkening = 0.0;
        std::size_t different = 0;
        for (std::size_t i = 0; i < filtered.size(); ++i)
        {
            if (i % 4 == 3) continue;
            filterDarkening += std::max(int(unshadowed[i]) - int(filtered[i]), 0);
            maskDarkening += std::max(int(unshadowed[i]) - int(masked[i]), 0);
            if (std::abs(int(masked[i]) - int(filtered[i])) > 16) ++different;
        }
        const double channels = double(Width) * Height * 3.0;
        std::cout << "Directional shadow mask (grazing ground): filter darkening " << filterDarkening / channels
                  << ", mask darkening " << maskDarkening / channels << ", "
                  << 100.0 * double(different) / channels << "% beyond 16\n";
        require(filterDarkening / channels < 0.5, "grazing ground: an uncovered ground must not shadow itself");
        require(maskDarkening / channels < 0.5, "grazing ground: the mask must not shadow an uncovered ground");
        require(double(different) / channels < 0.005, "grazing ground: the mask must match the surfaces' own filter");
    }
    CheckDirectionalShadowTerminator(renderer, readPixels);
    renderer.SetDirectionalShadowMask(true);
    renderer.Resize(width, height);
}
