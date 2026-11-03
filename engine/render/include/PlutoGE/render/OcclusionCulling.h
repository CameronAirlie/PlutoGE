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
        std::vector<rhi::Buffer> m_uniforms, m_models;
        std::uint32_t m_width = 0, m_height = 0;
        std::size_t m_capacity = 0;
        std::uint64_t m_frame = 0;
        OcclusionDepthHierarchy m_lastHierarchy;
        std::shared_ptr<OcclusionStats> m_stats = std::make_shared<OcclusionStats>();
    };
}
