#pragma once
#include "PlutoGE/render/rhi/Resource.h"
#include <glm/glm.hpp>
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
    // Current-frame reverse-Z occlusion. Owns GPU resources, never reads back
    // visibility for rendering decisions. Missing/uncertain bounds fail open.
    class OcclusionCulling
    {
    public:
        void Initialize(rhi::IRenderDevice &device, const OcclusionShaders &shaders);
        using SubmitDepth = std::function<void(const BasicDraw &)>;
        bool Record(rhi::IRenderDevice &device, std::span<const BasicDraw> draws,
                    const glm::mat4 &viewProjection, std::uint32_t width, std::uint32_t height,
                    OcclusionMode mode, const SubmitDepth &submit,
                    rhi::TextureHandle compatibleDepth = {});
        [[nodiscard]] rhi::BufferHandle Indirect() const { return m_indirect.Get(); }
        [[nodiscard]] OcclusionStats Stats() const { return *m_stats; }
        [[nodiscard]] static bool SafeOccluder(const BasicDraw &draw);
        static void SetRigidBounds(BasicDraw &draw, glm::vec3 minimum, glm::vec3 maximum);
    private:
        rhi::GraphicsPipeline m_depthPipeline, m_reducePipeline, m_testPipeline;
        rhi::Texture m_depth, m_color;
        rhi::Sampler m_sampler;
        rhi::Buffer m_hierarchy, m_candidates, m_indirect, m_counters;
        std::vector<rhi::Buffer> m_uniforms, m_models;
        std::uint32_t m_width = 0, m_height = 0;
        std::size_t m_capacity = 0;
        std::uint64_t m_frame = 0;
        std::shared_ptr<OcclusionStats> m_stats = std::make_shared<OcclusionStats>();
    };
}
