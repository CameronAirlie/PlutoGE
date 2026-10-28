#pragma once

#include "DdgiRaySource.h"
#include "../screenspace/ScreenSpacePass.h"

#include <glm/glm.hpp>

#include <array>

namespace PlutoGE::render
{
    // The voxel scene a frame's DDGI rays march: VCT's published radiance cascades (six directional atlases with the
    // cascades stacked along z) and the boxes they cover.
    struct DdgiVoxelScene
    {
        std::array<rhi::TextureHandle, 6> atlases{};
        // Each valid cascade's representative surfaces (R32UI: normal, albedo), for shading hits.
        std::array<rhi::TextureHandle, 3> surfaceRecords{};
        rhi::SamplerHandle sampler;
        // xyz origin, w size of each valid cascade, finest first.
        std::array<glm::vec4, 3> cascades{};
        std::uint32_t validCascades = 0, atlasCascades = 0, resolution = 0;
    };

    // Traces probe rays as narrow cones through the voxel scene (DDGIVoxelTrace.slang). Works on every backend; its
    // detail is bounded by the voxel size, which suits diffuse light.
    class VoxelDdgiRaySource final : public IDdgiRaySource
    {
    public:
        bool Initialize(rhi::IRenderDevice &device, const ScreenSpaceShaderCode &trace);
        [[nodiscard]] bool Available() const noexcept { return m_device && m_pipeline; }
        // This frame's scene; without a valid cascade the source is not ready.
        void SetScene(const DdgiVoxelScene &scene) noexcept { m_scene = scene; }

        [[nodiscard]] bool Ready() const noexcept override { return Available() && m_scene.validCascades > 0; }
        // The finest cascade's voxel.
        [[nodiscard]] float SurfaceTolerance() const noexcept override
        { return m_scene.resolution ? m_scene.cascades[0].w / static_cast<float>(m_scene.resolution) : 0.0f; }
        void Trace(rhi::ICommandContext &commands, const DdgiTraceInputs &inputs) override;

    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        ScreenSpaceUniforms m_uniforms;
        DdgiVoxelScene m_scene;
    };
}
