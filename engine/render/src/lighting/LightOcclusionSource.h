#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <cstdint>

namespace PlutoGE::render
{
    using LightVisibilityShaderCode = rhi::ComputePipelineDescriptor::ShaderCode;

    // What the light visibility pass reads and writes (LightVisibilityCommon.slang): its parameters, the coverage
    // depth and the mask, which it fills for the whole view.
    struct LightVisibilityPassInputs
    {
        rhi::BufferHandle parameters;
        rhi::TextureHandle depth, mask;
        rhi::SamplerHandle pointSampler;
        std::uint32_t width = 0, height = 0;
    };

    // The bindings every light visibility pass shares (set 0: 0 parameters, 1 the mask as a storage image, 2 the
    // depth), so sources build their pipelines and record the pass alike.
    struct LightVisibilityPassLayout
    {
        static constexpr std::uint32_t ParametersSlot = 0, MaskSlot = 1, DepthSlot = 2;

        // A compute pipeline descriptor with the shared bindings; sources add their scene's.
        [[nodiscard]] static rhi::ComputePipelineDescriptor Describe(const LightVisibilityShaderCode &shader,
                                                                     const char *debugName);
        // Binds the shared inputs and dispatches one thread per pixel. The caller has bound the pipeline and its
        // scene, and orders the mask's writes before reads.
        static void Dispatch(rhi::ICommandContext &commands, const LightVisibilityPassInputs &inputs);
    };

    // A world representation the light visibility pass tests light segments against, so local lights without a shadow
    // map stop at walls, floors and terrain (LightVisibilityMask). Each source records the whole pass with its own
    // shader variant (LightVisibilityVoxel.slang, LightVisibilityRayQuery.slang), which runs the contact marches too.
    // The mask depends only on this contract, so representations can be added (signed distance fields, a dedicated
    // shadow scene) without touching it.
    class ILightOcclusionSource
    {
    public:
        virtual ~ILightOcclusionSource() = default;
        // A scene is available to test against this frame.
        [[nodiscard]] virtual bool Ready() const noexcept = 0;
        // For profiler scopes and frame statistics.
        [[nodiscard]] virtual const char *Name() const noexcept = 0;
        // Records the pass outside rendering: binds its pipeline and scene, then LightVisibilityPassLayout::Dispatch.
        virtual void Record(rhi::ICommandContext &commands, const LightVisibilityPassInputs &inputs) = 0;
    };
}
