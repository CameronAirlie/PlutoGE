#pragma once
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/rhi/Resource.h"
#include "SkinningLodLayout.h"
#include <memory>
#include <span>

namespace PlutoGE::render
{
    // Immutable topology is shared by actors; palette/output history is per actor.
    struct RhiGpuSkinningSource
    {
        rhi::Buffer vertices;
        std::uint32_t vertexCount = 0;
        // Vertex order, remapped indices and per-LOD deformation prefixes.
        SkinningLodLayout layout;
    };
    struct RhiGpuSkinningState
    {
        std::shared_ptr<const RhiGpuSkinningSource> source;
        std::vector<glm::vec4> paletteRows; // Three affine rows per joint.
        std::uint32_t jointCount = 0;
        std::uint32_t firstVertex = 0;      // Layout slots [firstVertex, endVertex) to deform.
        std::uint32_t endVertex = 0;
        bool hasHistory = false;
        bool pending = false;
    };
    struct RhiGpuSkinningDispatch
    {
        RhiGpuSkinningState *state = nullptr;
        rhi::BufferHandle output;
    };

    // Records deformation into existing vertex/storage buffers. Does not own
    // frame submission, camera history or visibility decisions.
    class RhiGpuSkinning
    {
    public:
        // The packed joint index reserves 0xffff as "no joint".
        static constexpr std::size_t MaxJoints = 0xffff;

        bool Initialize(rhi::IRenderDevice &, const rhi::ComputePipelineDescriptor::ShaderCode &);
        // An empty layout keeps the source order and deforms every vertex.
        std::shared_ptr<RhiGpuSkinningSource> CreateSource(std::span<const MeshVertexData>, SkinningLodLayout layout = {});
        std::shared_ptr<RhiGpuSkinningState> CreateState(std::shared_ptr<const RhiGpuSkinningSource>);
        // Deforms layout slots [firstVertex, endVertex). hasHistory: those
        // slots still hold this actor's previous-frame deformation, which
        // becomes the motion-vector history; otherwise history is the current
        // pose. Queuing again before recording widens the range and, because
        // the slots no longer share one history, restarts it.
        void Queue(RhiGpuSkinningState &, std::span<const glm::mat4> palette, std::uint32_t firstVertex,
                   std::uint32_t endVertex, bool hasHistory);
        // Call once per recorded frame before Record. Dispatch parameters come
        // from a per-frame pool, so every dispatch in the frame (and every view
        // sharing an actor's state) reads its own constants.
        void BeginFrame() noexcept { m_nextParameters = 0; }
        // Uploads every queued palette in one transfer, then records one
        // dispatch per pending state. Caller brackets the batch with
        // ShaderMemoryBarrier, outside rendering. Returns the dispatch count.
        std::size_t Record(std::span<const RhiGpuSkinningDispatch>);

        // The exact source values the kernel reads after packing (normalized,
        // 16-bit normal/tangent frame and 16-bit joints); used by reference oracles.
        [[nodiscard]] static MeshVertexData QuantizeSource(const MeshVertexData &);
    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        rhi::Buffer m_palettes;
        std::size_t m_paletteCapacity = 0; // In rows.
        std::vector<glm::vec4> m_paletteUpload;
        std::vector<RhiGpuSkinningDispatch> m_batch;
        std::vector<rhi::Buffer> m_parameters;
        std::size_t m_nextParameters = 0;
    };
}
