#pragma once

#include "PlutoGE/render/FoliageWind.h"
#include "PlutoGE/render/VirtualShadowConfig.h"
#include "PlutoGE/render/VirtualShadowStats.h"
#include "PlutoGE/render/VirtualShadowPolicy.h"
#include "PlutoGE/render/ShadowGeometry.h"
#include "PlutoGE/render/rhi/Resource.h"
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace PlutoGE::render
{
    struct BasicDraw;
    struct BasicLighting;
    struct VirtualShadowShaders
    {
        // Shader modules of the raster vertex/fragment pairs, in `raster` order.
        static constexpr std::array<std::string_view, 9> RasterModules{
            "VSMReceiver", "VSMPage", "VSMClear", "VSMReceiverRigid", "VSMPageRigid", "VSMPageBatch",
            "VSMReceiverOpaque", "VSMReceiverRigidOpaque", "VSMRestore"};
        std::array<rhi::ComputePipelineDescriptor::ShaderCode, 7> compute;
        std::array<rhi::GraphicsPipelineDescriptor::ShaderCode, RasterModules.size() * 2> raster;
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
        // x: the directional root level, which is the number of active fine levels (VirtualShadowFineLevels).
        glm::uvec4 levels{};
        std::array<glm::uvec4, (PLUTO_VSM_LEVELS + 3) / 4> membershipEpochs{};
    };
    static_assert(sizeof(VirtualShadowParameters) == 256 + 96 * PLUTO_VSM_LEVELS + 16 * ((PLUTO_VSM_LEVELS + 3) / 4));

    // Read-only receiver bindings of one recorded VSM frame. A secondary view of
    // the same scene from the same viewpoint (a camera-stack overlay) samples
    // these instead of preparing and recording its own directional shadows.
    struct VirtualShadowReceivers
    {
        rhi::TextureHandle atlas, pageTable;
        rhi::BufferHandle parameters;
        // Normalised directional light the clipmaps were built for. A view lit
        // by a different light must not sample them.
        glm::vec3 lightDirection{0.0f};
        [[nodiscard]] explicit operator bool() const noexcept { return atlas && pageTable && parameters; }
        [[nodiscard]] bool MatchesLight(const glm::vec3 &direction) const noexcept
        {
            const float length = glm::length(direction);
            return length > 0.0f && glm::dot(direction / length, lightDirection) > 0.9999f;
        }
    };

    // Owns the complete GPU VSM frame graph. CPU work is limited to stable
    // clipmap policy and uploading caster/chunk inputs. Residency, invalidation,
    // request compaction and bounded indirect submission stay on the GPU.
    class GpuRecordTable;
    class GpuRecordHeap;

    class VirtualShadowMaps
    {
    public:
        VirtualShadowMaps();
        ~VirtualShadowMaps();
        VirtualShadowMaps(const VirtualShadowMaps &) = delete;
        VirtualShadowMaps &operator=(const VirtualShadowMaps &) = delete;
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
        // `sceneDepth`, when given, is the view's depth from `viewProjection` (reverse depth, width x height) with
        // every receiver in it, written on the GPU before RecordPlanning: planning reads it instead of rendering the
        // receivers itself.
        // `wind` deforms casters with a foliage wind response, as every other pass does; their `signatures` must
        // then change with it, so their pages are redrawn.
        bool Prepare(rhi::IRenderDevice &device, const BasicLighting &lighting, const glm::mat4 &viewProjection,
                     std::span<const BasicDraw> receivers, std::span<const BasicDraw> casters,
                     std::span<const std::uint64_t> signatures, std::span<const std::uint64_t> stillSignatures,
                     std::uint32_t width, std::uint32_t height,
                     rhi::TextureHandle sceneDepth = {}, const FoliageWindFrame &wind = {});
        // Recording has two phases that execute on the GPU in this order and share no CPU state, so they may record
        // concurrently into separate contexts: receiver depth (unless Prepare was given the scene's) and GPU
        // planning, then the caster pages (which read the planning results) and the final residency pass. Record
        // runs both on one context.
        void RecordPlanning(rhi::ICommandContext &commands, const SubmitMesh &submit);
        void RecordPages(rhi::ICommandContext &commands, const SubmitMesh &submit);
        void Record(rhi::ICommandContext &commands, const SubmitMesh &submit)
        {
            RecordPlanning(commands, submit);
            RecordPages(commands, submit);
        }
        [[nodiscard]] static VirtualShadowParameters BuildClipmaps(const BasicLighting &lighting,
                                                                  const VirtualShadowParameters *previous = nullptr,
                                                                  float resolutionScale = 1.0f);
        [[nodiscard]] static bool CanPrepare(std::span<const BasicDraw> receivers, std::span<const BasicDraw> casters);
        // Surfaces that sample virtual shadows: valid, non-blended and not glass.
        [[nodiscard]] static bool IsReceiver(const BasicDraw &draw) noexcept;
        // The depth planning reads this frame: the receiver depth pass's, or the scene depth given to Prepare.
        [[nodiscard]] auto ReceiverDepth() const { return m_planningDepth; }
        [[nodiscard]] auto Atlas() const { return m_depth.Get(); }
        [[nodiscard]] auto PageTable() const { return m_table.Get(); }
        [[nodiscard]] auto ParameterBuffer() const { return m_parameters.Get(); }
        [[nodiscard]] VirtualShadowReceivers Receivers(const glm::vec3 &lightDirection) const
        { return {Atlas(), PageTable(), ParameterBuffer(), lightDirection}; }
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
          // The mesh's vertex buffer: consecutive chunks sharing it (a geometry arena page) batch into one indirect draw.
          rhi::BufferHandle geometry;
          std::uint64_t meshRevision = 0, preparationRevision = 0;
          std::size_t firstInstance = 0, sourceIndex = 0;
          glm::vec4 bounds{0, 0, 0, -1};
          glm::vec4 extents{-1, -1, -1, 0};
          bool clustered = false;
      };
        void ResizePool(rhi::IRenderDevice &device, std::uint32_t tiles);
        // Allocates the static layer (m_staticDepth) and starts a new generation, so every page redraws into it once.
        void CreateStaticLayer(rhi::IRenderDevice &device);
        void BindCompute(rhi::ICommandContext &commands, std::size_t pipeline);
        // Records `count` chunks with pass-local binding state; returns the number of submissions issued.
        // Opaque (non alpha-tested) chunks use the opaque pipeline pair.
        struct ChunkPipelines { std::size_t instanced, rigid, opaqueInstanced, opaqueRigid; };
        std::uint32_t RecordChunks(rhi::ICommandContext &commands, const SubmitMesh &submit, const std::vector<Chunk> &chunks,
                                   std::size_t first, std::size_t end, ChunkPipelines pipelines);
        std::array<rhi::GraphicsPipeline, 7> m_compute;
        std::array<rhi::GraphicsPipeline, VirtualShadowShaders::RasterModules.size()> m_raster;
        // m_raster's tile pipelines (RasterModules order).
        static constexpr std::size_t ClearPipeline = 2, RestorePipeline = 8;
        static_assert(VirtualShadowShaders::RasterModules[ClearPipeline] == "VSMClear" &&
                      VirtualShadowShaders::RasterModules[RestorePipeline] == "VSMRestore");
        // Batched page draws' records (see RecordChunks), and their models: each caster packet's held where it was
        // first uploaded while the packet stays prepared.
        std::unique_ptr<GpuRecordTable> m_batchedDraws;
        std::unique_ptr<GpuRecordHeap> m_batchedModels;
        bool m_batchPages = false;
        rhi::Texture m_depth, m_color, m_table, m_requests, m_receiverDepth, m_white;
        // Static layer: the static casters' depth of each physical page, in m_depth's layout. Present once a frame has
        // dynamic casters; updates whose static casters are unchanged then restore it and redraw only the dynamic ones.
        rhi::Texture m_staticDepth;
        std::uint32_t m_staticGeneration = 0;
        // Caster chunks [0, m_staticCasterCount) are static, the rest dynamic; m_casterOrder lists casters so.
        std::size_t m_staticCasterCount = 0;
        std::vector<std::size_t> m_casterOrder;
        // m_receiverDepth, or the scene depth Prepare was given (then no receiver depth pass is recorded).
        rhi::TextureHandle m_planningDepth;
        bool m_sceneDepth = false;
        rhi::Buffer m_parameters, m_pages, m_casters, m_lists, m_indirect, m_requestList, m_counters;
        rhi::Buffer m_membership;
        // Masked chunks' model spheres (PLUTO_VSM_MASKED_INSTANCES per caster), and the CPU copy they upload from.
        rhi::Buffer m_instanceBounds;
        std::vector<std::byte> m_uploadedInstanceBounds;
        // Page list entries per caster (page budget x PLUTO_VSM_MASKED_INSTANCES) m_lists was allocated for.
        std::uint32_t m_listEntries = 0;
        rhi::Sampler m_sampler, m_materialSampler;
        // A caster packet's page chunks, held by its preparation revision rather than its place in the caster list,
        // so a packet keeps them as casters enter, leave or change LOD around it while the camera moves.
        struct CasterChunk
        {
            // indexOffset is relative to the mesh's base index (BasicMesh::BaseIndex).
            std::uint32_t indexCount = 0, indexOffset = 0, instances = 0, firstInstance = 0;
            glm::vec4 bounds{0, 0, 0, -1};
            glm::vec4 extents{-1, -1, -1, 0};
            // World sphere of each model of a masked chunk (several models, at most PLUTO_VSM_MASKED_INSTANCES), padded
            // by its wind reach. Pages plan, cost and sway these models individually.
            std::array<glm::vec4, PLUTO_VSM_MASKED_INSTANCES> instanceBounds{};
        };
        struct CasterPacket
        {
            const void *mesh = nullptr;
            std::uint64_t meshRevision = 0;
            std::uint32_t firstIndex = 0, indexCount = 0;
            // Placement the chunks were built for. Revisions identify immutable packets; this only guards against a
            // token reaching a different draw.
            glm::mat4 model{1};
            const void *instances = nullptr;
            std::size_t instanceCount = 0;
            // Whole-mesh cluster bounds of a rigid draw, and whether its clusters coalesce into one page chunk.
            ShadowGeometryCluster merged;
            glm::vec4 worldBounds{0, 0, 0, -1};
            bool planned = false, coalesce = false;
            std::uint64_t coalesceProjection = 0;
            bool chunksValid = false, clustered = false, coalesced = false;
            std::vector<CasterChunk> chunks;
            // First of this packet's records in m_batchedModels, or ~0u.
            std::uint32_t models = ~0u, modelCount = 0;
            std::uint64_t lastFrame = 0;
        };
        // `wind` pads swaying chunks' bounds; chunks are rebuilt when its settings change (m_chunkWind).
        void BuildCasterChunks(CasterPacket &packet, const BasicDraw &draw, bool clustered, bool coalesced,
                               const FoliageWindFrame &wind) const;
        void ReleaseCasterModels(CasterPacket &packet);
        std::unordered_map<std::uint64_t, CasterPacket> m_casterPackets;
        std::vector<CasterPacket *> m_casterPacketSlots;
        std::vector<glm::mat4> m_modelScratch;
        // The wind settings the cached caster chunks' sway padding was computed for.
        SceneWindSettings m_chunkWind{};
        std::uint64_t m_packetFrame = 0, m_projectionRevision = 0;
        struct PreparedPacket
        {
            const void *mesh = nullptr;
            std::uint64_t revision = 0, meshRevision = 0;
            std::size_t first = 0, count = 0;
        };
        std::vector<PreparedPacket> m_receiverPackets;
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
