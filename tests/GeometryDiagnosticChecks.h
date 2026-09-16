#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <stdexcept>
#include <set>
#include <string>

template<class Device, class ReadPixels>
void CheckGeometryDiagnostics(PlutoGE::render::BasicRenderer &renderer, Device &device, ReadPixels readPixels,
                              bool expectGpuTimings = true)
{
    using namespace PlutoGE::render;
    using Mode = GeometryDiagnosticMode;
    const std::array<BasicVertex, 4> vertices{{
        {{{-1,-1,0}}, {{0,0,1}}, {{0,0}}}, {{{1,-1,0}}, {{0,0,1}}, {{1,0}}},
        {{{1,1,0}}, {{0,0,1}}, {{1,1}}}, {{{-1,1,0}}, {{0,0,1}}, {{0,1}}}
    }};
    const std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    renderer.Resize(33, 17);
    BasicDraw draw;
    draw.mesh = &mesh;
    draw.model[3].z = .5f;
    draw.alphaMode = 1;
    draw.baseColor.a = 0;
    BasicLighting lighting;
    lighting.shadowsEnabled = false;
    const auto render = [&](Mode mode) {
        lighting.geometryDiagnosticMode = mode;
        renderer.Render(glm::mat4(1), lighting, std::span(&draw, 1));
        return readPixels(renderer.GetColorTexture());
    };
    const auto masked = render(Mode::None);
    for (auto mode : {Mode::MinimalShading, Mode::MaterialOnly, Mode::BypassDetailTextures})
        if (render(mode) != masked)
            throw std::runtime_error("Geometry diagnostics changed masked coverage");
    draw.alphaMode = 0;
    draw.baseColor.a = 1;
    const auto normal = render(Mode::None);
    if (normal == masked || render(Mode::DrawRanges) != normal)
        throw std::runtime_error("Draw range timing changed rendered pixels");
    constexpr std::array modes{Mode::None, Mode::MinimalShading, Mode::MaterialOnly,
        Mode::BypassDirectionalShadows, Mode::BypassDetailTextures,
        Mode::BypassPointLights, Mode::BypassSkyLighting};
    std::set<std::string> sweepScopes;
    for (std::size_t frame = 0; frame < 232; ++frame)
    {
        render(Mode::AutomaticSweep);
        if (renderer.GetFrameStats().geometryDiagnosticMode != modes[(frame / 32) % modes.size()])
            throw std::runtime_error("Geometry diagnostic sweep has incorrect attribution");
        for (const auto &scope : device.GetTimingStats("Scene").gpuScopes)
            if (scope.name.starts_with("RHI Geometry sweep / ")) sweepScopes.insert(scope.name);
    }
    for (auto mode : modes)
        if (expectGpuTimings && !sweepScopes.contains(std::string("RHI Geometry sweep / ") + GeometryDiagnosticName(mode)))
            throw std::runtime_error("Geometry diagnostic GPU timing missing a sweep variant");
    render(Mode::None);
    render(Mode::AutomaticSweep);
    if (renderer.GetFrameStats().geometryDiagnosticMode != Mode::None)
        throw std::runtime_error("Geometry diagnostic sweep did not reset");
    render(Mode::None);
}
