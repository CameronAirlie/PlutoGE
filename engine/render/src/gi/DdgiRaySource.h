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
        // DDGIVoxelTrace.slang documents. A ray that sees the sky reports PLUTO_DDGI_MISS_DISTANCE, which the
        // volume's reflection occlusion counts as sky visibility. Rays of inactive probes beyond the fixed ones are
        // left unwritten.
        rhi::TextureHandle rays;
        rhi::TextureHandle probeStates;
        // The volume's irradiance and visibility from its last update, for shading hits with the light the volume
        // holds there (multiple bounces, DDGICommon.slang's ddgiIrradiance; slots 14 and 15).
        rhi::TextureHandle irradiance, distance;
        // The probe occlusion volume, read with them (storage buffer, slot 16; DDGICommon.slang's ddgiOcclusion).
        rhi::BufferHandle occlusion;
        rhi::SamplerHandle pointSampler, linearSampler;
        std::uint32_t probes = 0;
        std::uint32_t tracedRays = 0;
    };

    // What a ray source is given to trace probe occlusion: whether each listed probe can see the sub-cells of the eight
    // probe cells it is a corner of. Bindings: slot 0 the volume parameters, 1 the listed probes, 3 the probe states
    // (relocated, after this frame's relocation), 16 the occlusion volume. One group of PLUTO_DDGI_VISIBILITY_GROUP
    // threads per (cell, listed probe): group (cell 0-7, list slot), cell c being the one whose base corner is the
    // probe's grid coordinate minus (c & 1, (c >> 1) & 1, c >> 2). Each thread writes the probe's four-bit visibility
    // (0-15, the share of a segment from its sub-cell's centre to the probe that is clear) into the probe's nibble
    // of its sub-cell, leaving the other nibbles to their probes (DDGICommon.slang ddgiOcclusionIndex and
    // ddgiOcclusionShift). Inactive probes may be skipped: lookups never weigh them.
    struct DdgiVisibilityInputs
    {
        rhi::BufferHandle volumeParameters;
        rhi::BufferHandle probeList;
        rhi::TextureHandle probeStates;
        rhi::BufferHandle occlusion;
        rhi::SamplerHandle pointSampler;
        std::uint32_t probes = 0;
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
        // Records the occlusion trace outside rendering (DdgiVisibilityInputs). Visibility must be exact rather than
        // statistical: it decides whether a lookup sees any probe at all, which the probes' distance moments
        // misjudge for points beside walls. The volume orders its writes before lookups read it.
        virtual void TraceVisibility(rhi::ICommandContext &commands, const DdgiVisibilityInputs &inputs) = 0;
    };
}
