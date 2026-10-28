#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <cstdint>

namespace PlutoGE::render
{
    // What a DDGI ray source is given to trace one frame's probe updates. Bindings follow DDGIVoxelTrace.slang:
    // slot 0 the volume parameters (DDGICommon.slang), 1 the listed probes, 2 the ray buffer, 3 the probe states.
    struct DdgiTraceInputs
    {
        rhi::BufferHandle volumeParameters;
        rhi::BufferHandle probeList;
        // Storage image: texel (ray, list slot) receives radiance (rgb) and the signed hit distance (a), as
        // DDGIVoxelTrace.slang documents. Rays of inactive probes beyond the fixed ones are left unwritten.
        rhi::TextureHandle rays;
        rhi::TextureHandle probeStates;
        // The volume's irradiance and visibility from its last update, for shading hits with the light the volume
        // holds there (multiple bounces, DDGICommon.slang's ddgiIrradiance; slots 14 and 15).
        rhi::TextureHandle irradiance, distance;
        rhi::SamplerHandle pointSampler, linearSampler;
        std::uint32_t probes = 0;
        std::uint32_t tracedRays = 0;
    };

    // The scene representation DDGI probes trace against. The probe volume (DdgiVolume) owns scheduling, blending,
    // relocation, classification and sampling, and depends only on this contract, so a source can be replaced (for
    // example by hardware ray queries) without touching it.
    class IDdgiRaySource
    {
    public:
        virtual ~IDdgiRaySource() = default;
        // A scene is available to trace this frame. Probes are not updated (and keep their history) until it is.
        [[nodiscard]] virtual bool Ready() const noexcept = 0;
        // World distance within which the source cannot place surfaces precisely (a voxel; near zero for triangle
        // ray tracing). Visibility tests are no sharper, so the volume never offsets its lookups by less.
        [[nodiscard]] virtual float SurfaceTolerance() const noexcept = 0;
        // Records the trace outside rendering. The volume orders the ray buffer's writes before its own reads.
        virtual void Trace(rhi::ICommandContext &commands, const DdgiTraceInputs &inputs) = 0;
    };
}
