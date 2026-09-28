#pragma once

#include "PlutoGE/render/VirtualShadowConfig.h"
#include "PlutoGE/render/VirtualShadowStats.h"
#include "PlutoGE/render/VirtualShadowPolicy.h"
#include "PlutoGE/render/ShadowGeometry.h"
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
        // Receiver, page, clear, rigid receiver/page, and batched rigid page pairs.
        std::array<rhi::GraphicsPipelineDescriptor::ShaderCode, 12> raster;
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
        glm::uvec4 pool{}, scheduling{};
        glm::vec4 culling{};
        std::array<glm::uvec4, (PLUTO_VSM_LEVELS + 3) / 4> membershipEpochs{};
    };
    static_assert(sizeof(VirtualShadowParameters) == 240 + 96 * PLUTO_VSM_LEVELS + 16 * ((PLUTO_VSM_LEVELS + 3) / 4));

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
            std::uint32_t commandCount = 1;
        };
        // Bind vertex/index buffers and issue the draw only. Pipeline and
        // resource bindings are owned by Record's pass-local state cache.
        using SubmitMesh = std::function<void(const Submission &)>;
        void Initialize(rhi::IRenderDevice &device, const VirtualShadowShaders &shaders);
        bool Prepare(rhi::IRenderDevice &device, const BasicLighting &lighting, const glm::mat4 &viewProjection,
                     std::span<const BasicDraw> receivers, std::span<const BasicDraw> casters,
                     std::span<const std::uint64_t> signatures, std::uint32_t width, std::uint32_t height);
        void Record(rhi::ICommandContext &commands, const SubmitMesh &submit);
        [[nodiscard]] static VirtualShadowParameters BuildClipmaps(const BasicLighting &lighting,
                                                                  const VirtualShadowParameters *previous = nullptr,
                                                                  float resolutionScale = 1.0f);
        [[nodiscard]] static bool CanPrepare(std::span<const BasicDraw> receivers, std::span<const BasicDraw> casters);
        [[nodiscard]] auto ReceiverDepth() const { return m_receiverDepth.Get(); }
        [[nodiscard]] auto Atlas() const { return m_depth.Get(); }
        [[nodiscard]] auto PageTable() const { return m_table.Get(); }
        [[nodiscard]] auto ParameterBuffer() const { return m_parameters.Get(); }
        [[nodiscard]] VirtualShadowStats GetStats() const;
        void SetMembershipCachingEnabled(bool enabled) noexcept { m_cacheMembership = enabled; }
    private:
      struct Chunk
      {
          Submission submission;
          rhi::Buffer uniform;
          rhi::TextureHandle texture;
          std::vector<std::byte> uploaded;
          const void *mesh = nullptr;
          std::uint64_t meshRevision = 0, preparationRevision = 0;
          std::size_t firstInstance = 0, sourceIndex = 0;
          glm::vec4 bounds{0, 0, 0, -1};
          glm::vec4 extents{-1, -1, -1, 0};
          bool clustered = false;
      };
        void ResizePool(rhi::IRenderDevice &device, std::uint32_t tiles);
        void BindCompute(rhi::ICommandContext &commands, std::size_t pipeline);
        std::array<rhi::GraphicsPipeline, 7> m_compute;
        std::array<rhi::GraphicsPipeline, 6> m_raster;
        rhi::Buffer m_rigidDraws;
        std::vector<std::byte> m_uploadedRigidDraws;
        std::size_t m_rigidDrawCapacity = 0;
        bool m_batchRigid = false;
        rhi::Texture m_depth, m_color, m_table, m_requests, m_receiverDepth, m_receiverColor, m_white;
        rhi::Buffer m_parameters, m_pages, m_casters, m_lists, m_indirect, m_requestList, m_counters;
        rhi::Buffer m_membership;
        rhi::Sampler m_sampler, m_materialSampler;
        struct ClusterPlan
        {
            const void *mesh = nullptr;
            std::uint64_t meshRevision = 0, packetRevision = 0;
            std::uint32_t firstIndex = 0, indexCount = 0;
            ShadowGeometryCluster merged;
            glm::vec4 worldBounds{0, 0, 0, -1};
            bool coalesce = false;
        };
        std::vector<ClusterPlan> m_clusterPlans;
        struct PreparedPacket
        {
            const void *mesh = nullptr;
            std::uint64_t revision = 0, meshRevision = 0;
            std::size_t first = 0, count = 0;
            bool clustered = false, coalesced = false;
        };
        std::vector<PreparedPacket> m_receiverPackets, m_casterPackets;
        std::vector<Chunk> m_receiverChunks, m_casterChunks;
        std::size_t m_receiverCount = 0, m_casterCount = 0, m_capacity = 0;
        std::uint32_t m_width = 0, m_height = 0, m_frame = 0;
        VirtualShadowParameters m_previousClipmaps{}, m_previousInputs{};
        std::vector<std::byte> m_uploadedCasters;
        std::vector<std::uint64_t> m_preparationKey, m_preparationScratch;
        VirtualShadowParameters m_preparationProjection{};
        std::uint32_t m_inputChangeFrame = 0;
        bool m_reuseFrame = false;
        bool m_cacheMembership = true;
        VirtualShadowResolutionPolicy m_resolutionPolicy;
        VirtualShadowBudgetPolicy m_budgetPolicy;
        std::uint32_t m_poolTiles = 0;
        std::shared_ptr<VirtualShadowStats> m_stats = std::make_shared<VirtualShadowStats>();
    };
}
