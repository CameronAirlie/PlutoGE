#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "../engine/render/src/GlassSnapshotBounds.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

// The same image checks run against real OpenGL and Vulkan devices.
template <class ReadPixels>
void CheckGlassRendering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    };
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-.9f,-.9f,0}}, {{0,0,1}}, {{0,0}}},
        {{{ .9f,-.9f,0}}, {{0,0,1}}, {{1,0}}},
        {{{ .9f, .9f,0}}, {{0,0,1}}, {{1,1}}},
        {{{-.9f, .9f,0}}, {{0,0,1}}, {{0,1}}}
    }};
    constexpr std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicLighting lighting;
    lighting.cameraPosition = {0,0,2};
    lighting.ambientIntensity = 0;
    lighting.directionalIntensity = 0;
    BasicDraw background;
    background.mesh = &mesh;
    background.model[3].z = .2f;
    background.emission = {.6f,.4f,.2f};
    BasicDraw pane;
    pane.mesh = &mesh;
    pane.model[3].z = .6f;
    pane.surfaceType = 1;
    pane.transmission = 1;
    pane.ior = 1;
    pane.roughness = .04f;
    pane.thickness = 1;
    pane.twoSided = true;
    const auto render = [&](std::span<const BasicDraw> draws,
                            std::span<const BasicPostProcessEffect> effects = {})
    {
        renderer.Render(glm::mat4(1), lighting, draws, effects);
        const auto pixels = readPixels(renderer.GetColorTexture());
        const auto center = (renderer.GetHeight()/2 * renderer.GetWidth() + renderer.GetWidth()/2) * 4;
        require(pixels.size() >= center + 4, "Glass readback size mismatch");
        return std::array<int,3>{int(pixels[center]),int(pixels[center+1]),int(pixels[center+2])};
    };
    const auto reference = render(std::span(&background,1));
    const auto opaqueNormals = readPixels(renderer.GetNormalTexture());
    const auto opaqueMaterial = readPixels(renderer.GetMaterialTexture());
    std::array draws{pane, background}; // Deliberately submit glass first.
    const auto clear = render(draws);
    for (int c=0;c<3;++c)
        require(std::abs(clear[c]-reference[c]) <= 3, "Clear glass did not preserve its background");
    require(readPixels(renderer.GetNormalTexture()) == opaqueNormals, "Glass overwrote opaque normals");
    require(readPixels(renderer.GetMaterialTexture()) == opaqueMaterial, "Glass overwrote opaque material");
    draws[0].attenuationColor = {.2f,1,1};
    const auto tinted = render(draws);
    require(tinted[0] + 20 < clear[0] && std::abs(tinted[1]-clear[1]) <= 3,
            "Glass absorption did not tint transmitted light");
    draws[0].twoSided = false;
    require(render(draws) == tinted, "Single-sided glass culled its front face");
    draws[0].twoSided = true;
    draws[0].baseColor.a = .5f;
    const auto partial = render(draws);
    require(partial[0] > tinted[0] && partial[0] < clear[0], "Partial coverage did not blend glass");
    draws[0].baseColor.a = 1;
    draws[0].thickness = 2;
    const auto thicker = render(draws);
    require(thicker[0] < tinted[0], "Increasing glass thickness did not increase absorption");
    draws[0].baseColor.a = 0;
    require(render(draws) == reference, "Zero coverage glass changed scene color");
    draws[0].baseColor.a = 1;
    draws[0].model[3].z = .1f;
    require(render(draws) == reference, "Glass was not occluded by opaque depth");
    // A fully transmitting pane in front must preserve the tinted pane behind.
    auto rear = pane;
    rear.attenuationColor = {.2f,1,1};
    rear.model[3].z = .4f;
    std::array layers{pane, background, rear};
    const auto layered = render(layers);
    require(std::abs(layered[0]-tinted[0]) <= 3, "Front glass erased a previously rendered layer");
    std::swap(layers[0], layers[2]);
    require(render(layers) == layered, "Glass depended on submission order");
    auto instances = std::make_shared<std::vector<glm::mat4>>();
    instances->push_back(pane.model);
    instances->push_back(rear.model);
    draws = {rear, background};
    draws[0].instanceModels = instances;
    require(render(draws)[0] < tinted[0], "Glass instances were not individually composited");
    const std::array effects{BasicPostProcessEffect{BasicPostProcessEffectType::ToneMapping}};
    const auto mappedReference = render(std::span(&background,1), effects);
    std::array clearDraws{pane,background};
    const auto mappedGlass = render(clearDraws, effects);
    for (int c=0;c<3;++c)
        require(std::abs(mappedGlass[c]-mappedReference[c]) <= 3, "Glass composited after tone mapping");
    BasicPostProcessEffect fogEffect{BasicPostProcessEffectType::VolumetricFog};
    fogEffect.quality = 32;
    fogEffect.parameters[0] = {0,0,0,2};
    fogEffect.parameters[1] = {0,0,100,1};
    fogEffect.parameters[2] = {0,0,0,1};
    const std::array fogEffects{fogEffect};
    const auto fogReference = render(std::span(&background,1), fogEffects);
    const auto fogGlass = render(clearDraws, fogEffects);
    for (int c=0;c<3;++c)
        require(std::abs(fogGlass[c]-fogReference[c]) <= 3, "Clear glass applied background fog twice");
    auto hazeEffect = fogEffect;
    hazeEffect.parameters[3] = {1,1,4,1};
    const std::array hazeEffects{hazeEffect};
    const auto hazeReference = render(std::span(&background,1), hazeEffects);
    const auto hazeGlass = render(clearDraws, hazeEffects);
    for (int c=0;c<3;++c)
        require(std::abs(hazeGlass[c]-hazeReference[c]) <= 3, "Clear glass applied horizon haze twice");
    auto glowingPane = pane;
    glowingPane.transmission = 0;
    glowingPane.emission = {.8f,.8f,.8f};
    std::array glowingDraws{glowingPane, background};
    const auto unfoggedSurface = render(glowingDraws);
    const auto foggedSurface = render(glowingDraws, fogEffects);
    require(foggedSurface[0] + 40 < unfoggedSurface[0], "Glass surface emission bypassed fog");
    fogEffect.parameters[1].z = .1f;
    const std::array shortDetailEffects{fogEffect};
    const auto distantFoggedSurface = render(glowingDraws, shortDetailEffects);
    for (int c = 0; c < 3; ++c)
        require(std::abs(distantFoggedSurface[c] - foggedSurface[c]) <= 3,
                "Glass fog stopped at shadow detail distance");
    auto reflectivePane = pane;
    reflectivePane.model[3].z = .9f;
    reflectivePane.ior = 1.5f;
    reflectivePane.transmission = 0;
    reflectivePane.baseColor = {0,0,0,1};
    reflectivePane.roughness = .4f;
    std::array shadowDraws{reflectivePane, background};
    lighting.directionalDirection = {0,0,-1};
    lighting.directionalIntensity = 1;
    const auto litReflection = render(shadowDraws);
    lighting.shadowMethod = ShadowMethod::Cascaded; // This fixture uses the identity cascade projection.
    lighting.shadowsEnabled = true;
    const auto shadowedReflection = render(shadowDraws);
    require(shadowedReflection[0] + 10 < litReflection[0], "Glass direct reflection ignored shadow visibility");
    lighting.shadowsEnabled = false;
    lighting.directionalIntensity = 0;
    auto blended = pane;
    blended.surfaceType = 0;
    blended.alphaMode = 2;
    blended.baseColor.a = .5f;
    blended.emission = {0,0,.8f};
    std::array blendDraws{blended,background};
    const auto blendedColor = render(blendDraws);
    require(blendedColor[0] < clear[0] && blendedColor[2] > clear[2],
            "Standard alpha blend material was not composited");
    // Alpha blends on either side of a refractor retain sorted dependencies.
    // This also exercises scratch snapshot reuse and switching render scopes.
    auto rearBlend = blended;
    rearBlend.model[3].z = .3f;
    auto frontBlend = blended;
    frontBlend.model[3].z = .8f;
    frontBlend.emission = {.8f, 0, 0};
    const std::array blendLayers{background, rearBlend, frontBlend};
    const auto blendReference = render(blendLayers);
    const std::array mixedLayers{frontBlend, pane, rearBlend, background};
    const auto mixedColor = render(mixedLayers);
    for (int c = 0; c < 3; ++c)
        require(std::abs(blendReference[c] - mixedColor[c]) <= 3,
                "Clear glass changed interleaved alpha-blended layers");
    // Refraction must move a color boundary, rather than merely change Fresnel brightness.
    auto angledVertices = vertices;
    for (auto &vertex : angledVertices) vertex.normal = {.7071f,0,.7071f};
    auto angledMesh = renderer.CreateMesh({angledVertices, indices});
    auto left = background;
    left.model = glm::translate(glm::mat4(1), glm::vec3(-.45f,0,.2f)) *
                 glm::scale(glm::mat4(1), glm::vec3(.5f,1,1));
    left.emission = {.8f,0,0};
    auto right = background;
    right.model = glm::translate(glm::mat4(1), glm::vec3(.45f,0,.2f)) *
                  glm::scale(glm::mat4(1), glm::vec3(.5f,1,1));
    right.emission = {0,.8f,0};
    auto refractor = pane;
    refractor.mesh = &angledMesh;
    refractor.thickness = .5f;
    std::array boundary{refractor,left,right};
    const auto straight = render(boundary);
    boundary[0].ior = 1.5f;
    const auto refracted = render(boundary);
    require(straight[1] > straight[0] + 30 && refracted[0] > refracted[1] + 30,
            "IOR did not refract the background color boundary");
    boundary[0].ior = 1;
    boundary[0].roughness = 1;
    const auto frosted = render(boundary);
    require(frosted[0] > straight[0] + 20, "Roughness did not blur transmission");
    boundary[0].ior = 1.5f;
    boundary[0].roughness = .04f;
    boundary[1].model[3].z = .8f;
    const auto foreground = render(boundary);
    require(foreground[1] > foreground[0] + 30, "Glass refracted opaque foreground into the background");
    require(renderer.Resize(320,180), "Glass snapshot test resize failed");
    boundary[1] = left;
    boundary[0].model = glm::translate(glm::mat4(1), glm::vec3(.2f,-.4f,.6f)) *
                        glm::scale(glm::mat4(1), glm::vec3(.2f));
    boundary[0].thickness = .005f;
    boundary[0].roughness = 1;
    render(boundary);
    const auto fullSnapshot = readPixels(renderer.GetColorTexture());
    boundary[0].shadowBoundsCenter = {.2f,-.4f,.6f};
    boundary[0].shadowBoundsRadius = .26f;
    boundary[0].occlusionBoundsCenter = boundary[0].shadowBoundsCenter;
    boundary[0].occlusionBoundsExtents = {.18f,.18f,0};
    const auto footprint = GlassSnapshotBounds(boundary[0], glm::mat4(1), {}, 320, 180, false);
    require(footprint.width * footprint.height < 320u * 180u / 2, "Glass snapshot failed to restrict its copy region");
    render(boundary);
    const auto boundedSnapshot = readPixels(renderer.GetColorTexture());
    require(boundedSnapshot.size() == fullSnapshot.size(), "Glass snapshot readback size mismatch");
    for (size_t i = 0; i < fullSnapshot.size(); ++i)
        require(std::abs(int(fullSnapshot[i]) - int(boundedSnapshot[i])) <= 1,
                "Bounded snapshot changed refracted or frosted glass pixels");
    auto eyePlane = glm::mat4(1);
    eyePlane[3][3] = 0;
    require(GlassSnapshotBounds(boundary[0], eyePlane, {}, 320, 180, false).width == 320,
            "Glass crossing the eye plane did not fall back to a full snapshot");
    // Disjoint panes share a snapshot; unknown bounds force the original
    // per-pane path, providing an independent full-image reference.
    auto groupedPane = boundary[0];
    groupedPane.model[3].x = -.55f;
    groupedPane.shadowBoundsCenter.x = groupedPane.occlusionBoundsCenter.x = -.55f;
    auto otherPane = groupedPane;
    otherPane.model[3].x = .55f;
    otherPane.shadowBoundsCenter.x = otherPane.occlusionBoundsCenter.x = .55f;
    std::array disjoint{groupedPane, otherPane};
    require(PlanGlassSnapshotGroup(disjoint, 0, glm::mat4(1), {}, 320, 180, false).end == 2,
            "Disjoint glass did not share a snapshot");
    std::array groupedScene{disjoint[0], disjoint[1], left, right};
    render(groupedScene);
    require(renderer.GetFrameStats().glassSnapshots == 1 && renderer.GetFrameStats().glassPanes == 2,
            "Disjoint glass did not reduce recorded snapshot copies");
    const auto groupedPixels = readPixels(renderer.GetColorTexture());
    require(renderer.GetFrameStats().materialPreparations == 3 &&
            renderer.GetFrameStats().materialPreparationHits == 1,
            "Repeated glass material was prepared more than once");
    // Force hash collisions: surface equality, not the hash, must decide reuse.
    for (auto &draw : groupedScene)
    {
        draw.preparationRevision = 1;
        draw.preparedMaterialHash = 0;
    }
    render(groupedScene);
    require(readPixels(renderer.GetColorTexture()) == groupedPixels,
            "Material hash collision reused different surface parameters");
    groupedScene[0].emission += glm::vec3(1, 0, 0);
    render(groupedScene);
    require(renderer.GetFrameStats().materialPreparations == 4 &&
            readPixels(renderer.GetColorTexture()) != groupedPixels,
            "Frame-local material cache missed an edited glass surface");
    for (auto &draw : disjoint) draw.shadowBoundsRadius = -1;
    require(PlanGlassSnapshotGroup(disjoint, 0, glm::mat4(1), {}, 320, 180, false).end == 1,
            "Unknown glass bounds incorrectly shared a snapshot");
    groupedScene[0] = disjoint[0];
    groupedScene[1] = disjoint[1];
    render(groupedScene);
    require(renderer.GetFrameStats().glassSnapshots == 2, "Full snapshot reference unexpectedly grouped panes");
    const auto referencePixels = readPixels(renderer.GetColorTexture());
    require(referencePixels == groupedPixels, "Shared glass snapshot changed rendered pixels");
    disjoint = {groupedPane, groupedPane};
    require(PlanGlassSnapshotGroup(disjoint, 0, glm::mat4(1), {}, 320, 180, false).end == 1,
            "Overlapping glass lost its ordered snapshot");
    disjoint[1] = otherPane;
    disjoint[1].surfaceType = 0;
    require(PlanGlassSnapshotGroup(disjoint, 0, glm::mat4(1), {}, 320, 180, false).end == 1,
            "Glass grouping crossed an ordinary transparency draw");
    std::array revisited{groupedPane, groupedPane, otherPane};
    revisited[1].model[3].x = 0;
    revisited[1].shadowBoundsCenter.x = revisited[1].occlusionBoundsCenter.x = 0;
    revisited[1].emission = {0, 1, 0};
    render(revisited);
    require(renderer.GetFrameStats().materialPreparations == 2 &&
            renderer.GetFrameStats().materialPreparationHits == 1,
            "Non-consecutive glass material was not reused");
    const auto reusedMaterials = readPixels(renderer.GetColorTexture());
    // Glass emits no outline pass, but differing outline widths conservatively
    // split surface-cache keys. This forces independent material preparation.
    for (std::size_t i = 0; i < revisited.size(); ++i) revisited[i].outlineWidth = float(i + 1);
    render(revisited);
    require(renderer.GetFrameStats().materialPreparations == 3 &&
            readPixels(renderer.GetColorTexture()) == reusedMaterials,
            "Non-consecutive material reuse changed rendered glass");
    require(renderer.Resize(48,32), "Glass resize failed");
    require(render(layers)[0] > 0, "Glass failed after resize");
    require(renderer.Resize(96,64), "Glass test restore resize failed");
}
