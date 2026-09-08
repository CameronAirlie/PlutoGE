#pragma once

#include "PlutoGE/render/VirtualShadowConfig.h"
#include "PlutoGE/render/VirtualShadowStats.h"
#include "PlutoGE/render/rhi/Resource.h"
#include <glm/glm.hpp>
#include <memory>

namespace PlutoGE::render
{
    struct BasicDraw;
    struct BasicLighting;
    struct VirtualShadowShaders
    {
        std::array<rhi::ComputePipelineDescriptor::ShaderCode, 7> compute;
        // Receiver, physical page, and tile-clear vertex/fragment pairs.
        std::array<rhi::GraphicsPipelineDescriptor::ShaderCode, 6> raster;
        [[nodiscard]] bool Complete() const;
    };
    struct alignas(16) VirtualShadowParameters
    {
        std::array<glm::mat4, PLUTO_VSM_LEVELS> matrices{};
        glm::mat4 inverseViewProjection{1.0f}, viewProjection{1.0f};
        std::array<glm::ivec4, PLUTO_VSM_LEVELS> origins{};
        std::array<glm::vec4, PLUTO_VSM_LEVELS> metrics{};
        glm::uvec4 viewport{}, limits{};
        glm::vec4 settings{}, camera{};
    };
    static_assert(sizeof(VirtualShadowParameters) == 672);

    // Owns the complete GPU VSM frame graph. CPU work is limited to stable
    // clipmap policy and uploading caster/chunk inputs. Residency, invalidation,
    // request compaction and bounded indirect submission stay on the GPU.
    class VirtualShadowMaps
    {
    public:
        struct Submission
        {
            const BasicDraw *draw = nullptr;
            std::uint32_t indexCount = 0, firstIndex = 0, instances = 0;
            rhi::BufferHandle indirect;
            std::size_t indirectOffset = 0;
        };
        using SubmitMesh = std::function<void(const Submission &)>;
        void Initialize(rhi::IRenderDevice &device, const VirtualShadowShaders &shaders);
        bool Prepare(rhi::IRenderDevice &device, const BasicLighting &lighting, const glm::mat4 &viewProjection,
                     std::span<const BasicDraw> receivers, std::span<const BasicDraw> casters,
                     std::span<const std::uint64_t> signatures, std::uint32_t width, std::uint32_t height);
        void Record(rhi::ICommandContext &commands, const SubmitMesh &submit);
        [[nodiscard]] static VirtualShadowParameters BuildClipmaps(const BasicLighting &lighting,
                                                                  const VirtualShadowParameters *previous = nullptr);
        [[nodiscard]] static bool CanPrepare(std::span<const BasicDraw> receivers, std::span<const BasicDraw> casters);
        [[nodiscard]] auto Atlas() const { return m_depth.Get(); }
        [[nodiscard]] auto PageTable() const { return m_table.Get(); }
        [[nodiscard]] auto ParameterBuffer() const { return m_parameters.Get(); }
        [[nodiscard]] VirtualShadowStats GetStats() const;
    private:
        struct Chunk { Submission submission; rhi::Buffer uniform; rhi::TextureHandle texture; };
        void BindCompute(rhi::ICommandContext &commands, std::size_t pipeline);
        std::array<rhi::GraphicsPipeline, 7> m_compute;
        std::array<rhi::GraphicsPipeline, 3> m_raster;
        rhi::Texture m_depth, m_color, m_table, m_requests, m_receiverDepth, m_receiverColor, m_white;
        rhi::Buffer m_parameters, m_pages, m_casters, m_lists, m_indirect, m_requestList, m_counters;
        rhi::Sampler m_sampler, m_materialSampler;
        std::vector<Chunk> m_receiverChunks, m_casterChunks;
        std::size_t m_receiverCount = 0, m_casterCount = 0, m_capacity = 0;
        std::uint32_t m_width = 0, m_height = 0, m_frame = 0;
        VirtualShadowParameters m_previousClipmaps{};
        std::shared_ptr<VirtualShadowStats> m_stats = std::make_shared<VirtualShadowStats>();
    };
}
