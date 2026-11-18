#include "VoxelSceneBinding.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        // Mirrors VoxelSceneParameters in VoxelSceneSampling.slang.
        struct alignas(16) VoxelSceneParameters
        {
            std::array<glm::vec4, 3> cascades{};
            glm::uvec4 counts{0u};
            glm::vec4 trace{0.0f};
        };
        static_assert(sizeof(VoxelSceneParameters) == 80);
    }

    void VoxelSceneBinding::Describe(rhi::ComputePipelineDescriptor &descriptor)
    {
        const auto bind = [&](std::uint32_t slot, rhi::ResourceBindingType type)
        { descriptor.resourceBindings.push_back({slot, 0, slot, type, rhi::ShaderStageMask::Compute}); };
        bind(ParametersSlot, rhi::ResourceBindingType::UniformBuffer);
        for (std::uint32_t direction = 0; direction < 6; ++direction)
            bind(FirstAtlasSlot + direction, rhi::ResourceBindingType::SampledTexture);
    }

    rhi::BufferHandle VoxelSceneBinding::Upload(ScreenSpaceUniforms &uniforms, const DdgiVoxelScene &scene,
                                                float coneSpread)
    {
        VoxelSceneParameters parameters;
        parameters.cascades = scene.cascades;
        parameters.counts = {scene.validCascades, scene.atlasCascades, scene.resolution,
                             static_cast<std::uint32_t>(std::floor(std::log2(std::max(scene.resolution, 1u))))};
        const float outerSize = scene.cascades[std::clamp(scene.validCascades, 1u, 3u) - 1].w;
        parameters.trace = {coneSpread, outerSize, 0.0f, 0.0f};
        return uniforms.Push(parameters);
    }

    void VoxelSceneBinding::Bind(rhi::ICommandContext &commands, rhi::BufferHandle parameters, const DdgiVoxelScene &scene)
    {
        commands.BindUniformBuffer(ParametersSlot, parameters);
        for (std::uint32_t direction = 0; direction < 6; ++direction)
            commands.BindTexture(FirstAtlasSlot + direction, scene.atlases[direction], scene.sampler);
    }
}
