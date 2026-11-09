#pragma once
#include "PlutoGE/render/rhi/Resource.h"
#include <glm/glm.hpp>
#include <array>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    struct BasicDraw;
    enum class OcclusionMode : std::uint8_t { Off, Measure, Cull };
    struct OcclusionStats
    {
        std::uint32_t tested = 0, rejected = 0, rejectedTriangles = 0;
        std::uint64_t frame = 0;
        bool available = false;
        std::uint32_t unsupported = 0, invalidBounds = 0, clipped = 0;
        std::uint32_t refined = 0, refinementRejected = 0, budgetExceeded = 0;
        std::uint32_t tightBounds = 0;
    };
    // GPU instance culling's diagnostics (GpuInstanceCulling, InstanceCulling.slang), read back without waiting, so
    // they describe `frame`, an earlier render. Instances are counted once each, whatever their LOD crossfade.
    struct InstanceCullingStats
    {
        std::uint64_t frame = 0;
        bool available = false;
        // Instances tested; outside the view, beyond the draw distance or the covered size; hidden by the previous
        // frame's depth (phase 0); of those, drawn after all by the second phase; drawn; and drawn on both sides of a
        // LOD crossfade.
        std::uint32_t instances = 0, outside = 0, occluded = 0, recovered = 0, drawn = 0, crossfading = 0;
        bool secondPhase = false;
    };
    struct OcclusionShaders
    {
        rhi::GraphicsPipelineDescriptor::ShaderCode vertex, fragment, reduce, test;
    };
    // The reverse-Z depth hierarchy a Record built: a storage buffer of floats holding every level (each level's width,
    // height and offset), the farthest depth per cell, and the view-projection of the depth it reduced. Consumers that
    // cull before this frame's depth exists (GpuInstanceCulling) test against the previous frame's.
    struct OcclusionDepthHierarchy
    {
        rhi::BufferHandle buffer;
        glm::mat4 viewProjection{1.0f};
        std::uint32_t width = 0, height = 0, levelCount = 0;
        std::uint32_t flags = 0; // bit 0 zero-to-one clip depth, bit 1 Vulkan clip y
        std::array<glm::uvec4, 32> levels{};
        std::uint64_t frame = 0; // the Record that built it; 0 before the first
    };
    // Current-frame reverse-Z occlusion. Owns GPU resources, never reads back
    // visibility for rendering decisions. Missing/uncertain bounds fail open.
    class OcclusionCulling
    {
    public:
        void Initialize(rhi::IRenderDevice &device, const OcclusionShaders &shaders);
        using SubmitDepth = std::function<void(const BasicDraw &)>;
        // Records into `commands` (the frame's context, or a parallel one).
        bool Record(rhi::IRenderDevice &device, rhi::ICommandContext &commands, std::span<const BasicDraw> draws,
                    const glm::mat4 &viewProjection, std::uint32_t width, std::uint32_t height,
                    OcclusionMode mode, const SubmitDepth &submit,
                    rhi::TextureHandle compatibleDepth = {});
        [[nodiscard]] rhi::BufferHandle Indirect() const { return m_indirect.Get(); }
        // The hierarchy the last Record built (frame counts Records). Valid until the next Record at another size.
        [[nodiscard]] const OcclusionDepthHierarchy &Hierarchy() const { return m_lastHierarchy; }
        [[nodiscard]] std::uint64_t Frame() const { return m_frame; }
        // A hierarchy of a depth target partway through a frame, beside the one Record builds: two-phase instance
        // culling re-tests what the previous frame's hierarchy rejected against the depth drawn so far. Prepare
        // allocates and uploads on the calling thread; RecordIntermediateHierarchy only records commands, so it may run
        // in a parallel recording job. `viewProjection` is the one the depth was drawn with.
        bool PrepareIntermediateHierarchy(rhi::IRenderDevice &device, const glm::mat4 &viewProjection,
                                          std::uint32_t width, std::uint32_t height);
        void RecordIntermediateHierarchy(rhi::ICommandContext &commands, rhi::TextureHandle depth);
        // Valid once recorded, for this frame only.
        [[nodiscard]] const OcclusionDepthHierarchy &IntermediateHierarchy() const { return m_intermediate; }
        [[nodiscard]] OcclusionStats Stats() const { return *m_stats; }
        [[nodiscard]] static bool SafeOccluder(const BasicDraw &draw);
        static void SetRigidBounds(BasicDraw &draw, glm::vec3 minimum, glm::vec3 maximum);
        // Accepts rigid mesh bounds or a conservative current-pose skinned box.
        static void SetLocalBounds(BasicDraw &draw, glm::vec3 minimum, glm::vec3 maximum);
    private:
        rhi::GraphicsPipeline m_depthPipeline, m_reducePipeline, m_testPipeline;
        rhi::Texture m_depth, m_color;
        rhi::Sampler m_sampler;
        rhi::Buffer m_hierarchy, m_candidates, m_indirect, m_counters;
        rhi::Buffer m_intermediateBuffer;
        std::vector<rhi::Buffer> m_intermediateUniforms;
        OcclusionDepthHierarchy m_intermediate;
        std::uint32_t m_intermediateWidth = 0, m_intermediateHeight = 0;
        std::vector<rhi::Buffer> m_uniforms, m_models;
        std::uint32_t m_width = 0, m_height = 0;
        std::size_t m_capacity = 0;
        std::uint64_t m_frame = 0;
        OcclusionDepthHierarchy m_lastHierarchy;
        std::shared_ptr<OcclusionStats> m_stats = std::make_shared<OcclusionStats>();
    };
}
