#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"
#include "PlutoGE/render/rhi/Resource.h"
#include "../screenspace/ScreenSpacePass.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace PlutoGE::render
{
    // The voxel scene a frame's ray marches read: VCT's published radiance cascades (six directional atlases with the
    // cascades stacked along z) and the boxes they cover. DDGI's voxel ray source and the light visibility mask's
    // voxel occlusion both march it (VoxelSceneSampling.slang).
    struct DdgiVoxelScene
    {
        std::array<rhi::TextureHandle, 6> atlases{};
        // Each valid cascade's representative surfaces (R32UI: normal, albedo), for shading hits.
        std::array<rhi::TextureHandle, 3> surfaceRecords{};
        rhi::SamplerHandle sampler;
        // xyz origin, w size of each valid cascade, finest first.
        std::array<glm::vec4, 3> cascades{};
        std::uint32_t validCascades = 0, atlasCascades = 0, resolution = 0;

        [[nodiscard]] bool Valid() const noexcept { return validCascades > 0 && resolution > 0 && atlases[0]; }
        // The finest cascade's voxel, in world units (0 without a scene).
        [[nodiscard]] float FinestVoxel() const noexcept
        { return resolution ? cascades[0].w / static_cast<float>(resolution) : 0.0f; }
    };

    // How passes bind the voxel scene for VoxelSceneSampling.slang: its parameters at set 0 binding 4 and the six
    // atlases at 8-13 (command slots equal to the bindings).
    struct VoxelSceneBinding
    {
        static constexpr std::uint32_t ParametersSlot = 4, FirstAtlasSlot = 8;

        // Adds the scene's bindings to a compute pipeline.
        static void Describe(rhi::ComputePipelineDescriptor &descriptor);
        // Pushes `scene`'s parameters through `uniforms` (on the render thread: recording jobs must not allocate).
        // `coneSpread` is the footprint diameter per unit distance cone marches read (VoxelSceneSampling.slang
        // voxelTrace.x); segment tests ignore it.
        [[nodiscard]] static rhi::BufferHandle Upload(ScreenSpaceUniforms &uniforms, const DdgiVoxelScene &scene,
                                                      float coneSpread = 0.0f);
        // Binds `scene` with the parameters Upload returned.
        static void Bind(rhi::ICommandContext &commands, rhi::BufferHandle parameters, const DdgiVoxelScene &scene);
    };
}
